#include <climits> // for USHRT_MAX
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <syslog.h> 

#include "utils.hh"

static constexpr char PID_FILE_NAME[] = "psirver.pid";
// static constexpr char HOME_VAR[] = "PSIRVER_HOME";

static constexpr uint16_t DEFAULT_PORT = 8000;

// Show usage and terminate
void usage(const char* prog)
{
  std::cerr << "Usage: " << prog << " [PORT]\n"
	    << "\t PORT: 1-" << USHRT_MAX << " (default: "
	    << DEFAULT_PORT << ")\n";
  exit(EXIT_FAILURE); // No return
};

// Parse command line arguments to find the port number. Library
// functions used:
// - std::stoi()
int16_t select_port(int argc, char **argv)
{
  int16_t server_port_tmp = DEFAULT_PORT;
  if (argc > 2) {		// Too many parameters
    usage(argv[0]);		// No return
  }
  
  if (argc == 2) {
    try { 
      server_port_tmp = std::stoi(argv[1]);
      if (server_port_tmp <= 0 || server_port_tmp > USHRT_MAX) {
				// Illegal parameter
	usage(argv[0]);		// No return
      }
    } catch (std::invalid_argument const& ex) { // Not a number
      usage(argv[0]);				// No return
    } catch (std::out_of_range const& ex) {	// Too big
      usage(argv[0]);				// No return
    }
  }
  
  return static_cast<uint16_t>(server_port_tmp);
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
    exit(EXIT_FAILURE);
  }

  if (::chdir(home) != 0) {
    syslog(LOG_ERR, "%s: %s", HOME_VAR, strerror(errno));
    exit(EXIT_FAILURE);
  }
  
  std::string pid_file_path(PID_FILE_NAME);
  // pid_file_path.append(home);
  // pid_file_path.push_back('/');
  // pid_file_path.append(PID_FILE_NAME);
    
  int flags = O_WRONLY | O_CREAT | O_TRUNC;
  int mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
  int fd = open(pid_file_path.c_str(), flags, mode);

  if(fd < 0) {
    syslog(LOG_ERR, "%s: %s", pid_file_path.c_str(), strerror(errno));
    exit(EXIT_FAILURE);   
  }
  const std::string pid_str = std::to_string(getpid()) + "\n";
  ssize_t w = write(fd, pid_str.c_str(), pid_str.size());
  if (w < 0 || close(fd) != 0) {
    syslog(LOG_ERR, "%s: %s", pid_file_path.c_str(), strerror(errno));
    exit(EXIT_FAILURE);   
  }

  return pid_file_path;
}
  
// Register a graceful shutdown handler on SIGINT. Library functions used:
// - memset()
// - sigemptyset()
// - sigaction()
// - syslog()
// - strerror()
// - exit()
void add_sigint_handler()
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = graceful_shutdown;
  sigemptyset(&sa.sa_mask);
  
  if(sigaction(SIGINT, &sa, nullptr) != 0) {
    syslog(LOG_ERR, "Sigaction: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

