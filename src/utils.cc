#include <climits> // for USHRT_MAX
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <syslog.h> 
#include <charconv>
#include "utils.hh"

static constexpr char PID_FILE_NAME[] = "psirver.pid";
static constexpr char RC_FILE_NAME[] = "psirver.rc";
static constexpr char HOME_VAR[] = "PSIRVER_HOME";
static constexpr uint16_t DEFAULT_PORT = 8000;

// Show usage and terminate
[[noreturn]] void usage(const char* prog)
{
  std::cerr << "Usage: " << prog << " [PORT]\n"
	    << "\t PORT: 1-" << USHRT_MAX << " (default: "
	    << DEFAULT_PORT << ")\n";
  ::exit(EXIT_FAILURE); // No return
};

// Parse command line arguments to find the port number. Library
// functions used:
uint16_t select_port(int argc, char **argv)
{
  if (argc > 2) {		// Too many parameters
    usage(argv[0]);		// No return
  }

  if (argc == 1) {
    return DEFAULT_PORT;
  }

  const char* s = argv[1];
  unsigned value = 0;

  const auto result = std::from_chars(s, s + std::strlen(s), value);
  if (result.ec != std::errc{} || *result.ptr != '\0' ||
      value == 0 || value > USHRT_MAX) {
    usage(argv[0]);
  }

  return static_cast<uint16_t>(value);
}

// Create $(PSIRVER_HOME)/psirver.pid.
// 
// To setup the variable:
//   export PSIRVER_HOME=$HOME/Psirver
// To terminate the server gracefully:
//   kill -INT `cat $PSIRVER_HOME/psirver.pid`
// 
// Library functions used:
// - exit()
// - syslog()
// - open()
// - getpid()
// - write()
// - close()
// - std::to_string()
// - strerror()
std::string init_pid_file()
{
  const char *home = getenv(HOME_VAR);
  if(!home || !(*home)) {
    syslog(LOG_ERR, "%s: not set", HOME_VAR);
    ::exit(EXIT_FAILURE);
  }

  if (::chdir(home) != 0) {
    syslog(LOG_ERR, "%s: %s", HOME_VAR, strerror(errno));
    ::exit(EXIT_FAILURE);
  }
  
  std::string pid_file_path(PID_FILE_NAME);    
  int flags = O_WRONLY | O_CREAT | O_TRUNC;
  int mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
  int fd = ::open(pid_file_path.c_str(), flags, mode);

  if(fd < 0) {
    syslog(LOG_ERR, "%s: %s", pid_file_path.c_str(), ::strerror(errno));
    ::exit(EXIT_FAILURE);   
  }
  const std::string pid_str = std::to_string(getpid()) + "\n";
  ssize_t w = ::write(fd, pid_str.c_str(), pid_str.size());
  if (w < 0 || ::close(fd) != 0) {
    syslog(LOG_ERR, "%s: %s", pid_file_path.c_str(), ::strerror(errno));
    ::exit(EXIT_FAILURE);   
  }

  return pid_file_path;
}
  
// Register a graceful shutdown handler on SIGINT. Library functions used:
// - sigemptyset()
// - sigaction()
// - syslog()
// - strerror()
// - exit()
void add_sigint_handler()
{
  struct sigaction sa{};
  sa.sa_handler = graceful_shutdown;
  ::sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  
  if(::sigaction(SIGINT, &sa, nullptr) != 0) {
    syslog(LOG_ERR, "Sigaction: %s", ::strerror(errno));
    ::exit(EXIT_FAILURE);
  }
}

