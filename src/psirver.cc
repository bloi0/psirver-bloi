#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

#include "Requests.hh"
#include "ServerState.hh"

// Configuration options and other constants
static constexpr uint16_t DEFAULT_PORT = 8000;
static constexpr uint16_t MAX_PORT = 65535;
static constexpr ssize_t MAX_REQUEST_SZ = 0x10000;
static constexpr size_t BUFFER_SZ = 4096;
static constexpr char HEADER_END[] = "\r\n\r\n";

// Global server state
int server_socket = -1;
static char pid_file_path[PATH_MAX];
static volatile sig_atomic_t shutdown_requested = 0;
static ServerState server_state;

// Reply to the client with an HTTP status line and a human-readable
// response body
void reply(int client, const char *status_line, const char *body)
{
  std::string headers;
  headers.reserve(256);
  headers.append(status_line);
  headers.append("\r\n");
  headers.append("Content-Type: text/plain; charset=utf-8\r\n");
  headers.append("Content-Length: ");
  headers.append(std::to_string(strlen(body)));
  headers.append("\r\n");
  headers.append("Connection: close");
  headers.append(HEADER_END);

  write(client, headers.data(), headers.size());
  write(client, body, strlen(body));
}

// Log an error message with the current errno details
void log_error(const char *context)
{
  syslog(LOG_ERR, "%s: %s", context, strerror(errno));
}

void print_usage(const char *program, const char *message)
{
  if (message != nullptr) {
    std::cerr << message << "\n";
  }
  std::cerr << "Usage: " << program << " [port]\n";
}

bool create_pid_file()
{
  const char *psirver_home = std::getenv("PSIRVER_HOME");
  if (psirver_home == nullptr || *psirver_home == '\0') {
    std::cerr << "Error: PSIRVER_HOME is not set or is empty.\n";
    return false;
  }

  struct stat st;
  if (stat(psirver_home, &st) != 0) {
    std::cerr << "Error: could not stat PSIRVER_HOME ('" << psirver_home << "'): "
              << strerror(errno) << "\n";
    return false;
  }

  if (!S_ISDIR(st.st_mode)) {
    std::cerr << "Error: PSIRVER_HOME is not a directory: " << psirver_home << "\n";
    return false;
  }

  std::string pid_path = std::string(psirver_home) + "/psirver.pid";
  if (pid_path.size() >= sizeof(pid_file_path)) {
    std::cerr << "Error: PSIRVER_HOME path is too long.\n";
    return false;
  }
  std::strncpy(pid_file_path, pid_path.c_str(), sizeof(pid_file_path));
  pid_file_path[sizeof(pid_file_path) - 1] = '\0';

  int fd = open(pid_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    std::cerr << "Error: could not create PID file: " << strerror(errno) << "\n";
    return false;
  }

  std::string pid_text = std::to_string(getpid());
  ssize_t written = write(fd, pid_text.c_str(), pid_text.size());
  close(fd);
  if (written < 0) {
    std::cerr << "Error: write to PID file failed: " << strerror(errno) << "\n";
    return false;
  }
  if (static_cast<size_t>(written) != pid_text.size()) {
    std::cerr << "Error: short write when writing PID file.\n";
    return false;
  }

  return true;
}

void handle_sigint(int)
{
  shutdown_requested = 1;
  if (server_socket >= 0) {
    close(server_socket);
  }
}

void remove_pid_file()
{
  if (pid_file_path[0] != '\0') {
    unlink(pid_file_path);
  }
}

// Open the main server socket and prepare it for accepting
// connections. Library functions used:
// - htonl()/htons()
// - bind()
// - listen()
// - fcntl()
// - close()
int init_socket(uint16_t port)
{
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    log_error("socket failed");
    return -1;
  }

  struct sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);

  if (bind(server_socket, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
    log_error("bind failed");
    close(server_socket);
    return -1;
  }

  if (listen(server_socket, SOMAXCONN) != 0) {
    log_error("listen failed");
    close(server_socket);
    return -1;
  }

  // Prevent leaking server_socket into child processes
  if (-1 == fcntl(server_socket, F_SETFD, FD_CLOEXEC)) {
    syslog(LOG_WARNING, "fcntl(FD_CLOEXEC) warning: %s", strerror(errno));
  }

  return 0;
}

// Given the headers, extract the content length (only for POST).
ssize_t parse_content_length(int client, const std::string &headers)
{
  constexpr char CL[] = "Content-Length: ";
  size_t pos = headers.find(CL);
  if (pos == std::string::npos) {
    pos = headers.find("content-length: ");
    if (pos == std::string::npos) {
      reply(client, "HTTP/1.1 411 Length Required", "Length Required");
      return -1;
    }
  }

  std::string rest = headers.substr(pos + sizeof CL - 1);
  size_t content_length_end = rest.find("\r");
  if (content_length_end == std::string::npos) {
    content_length_end = rest.find("\n");
  }
  if (content_length_end == std::string::npos) {
    content_length_end = rest.length();
  }

  std::string content_length_str = rest.substr(0, content_length_end);
  size_t start = content_length_str.find_first_not_of(" \t\r\n");
  size_t end = content_length_str.find_last_not_of(" \t\r\n");
  if (start != std::string::npos && end != std::string::npos) {
    content_length_str = content_length_str.substr(start, end - start + 1);
  }

  size_t content_length = 0;
  try {
    size_t idx = 0;
    content_length = std::stoul(content_length_str, &idx, 10);
    if (idx != content_length_str.size()) {
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return -1;
    }
  } catch (...) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return -1;
  }

  if (content_length > static_cast<size_t>(MAX_REQUEST_SZ)) {
    reply(client, "HTTP/1.1 413 Content Too Large", "Content Too Large");
    return -1;
  }

  return static_cast<ssize_t>(content_length);
}

// Given the content length and the pre-read body, read the whole body.
std::string read_body(int client, ssize_t content_length, std::string body)
{
  if (content_length < 0) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return "";
  }

  size_t expected = static_cast<size_t>(content_length);
  if (body.size() > expected) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return "";
  }

  size_t remaining = expected - body.length();

  char buffer[BUFFER_SZ];
  while (remaining > 0) {
    ssize_t read_len = std::min(static_cast<ssize_t>(remaining), static_cast<ssize_t>(sizeof(buffer)));
    ssize_t chunk_sz = read(client, buffer, read_len);
    if (chunk_sz > 0) {
      body.append(buffer, chunk_sz);
      remaining -= static_cast<size_t>(chunk_sz);
    } else {
      break;
    }
  }

  if (body.size() != expected) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return "";
  }

  return body;
}

// Accept a connection, read the request, parse the headers and body,
// then dispatch to a polymorphic request handler.
static void handle_client_request(int client)
{
  char buffer[BUFFER_SZ];
  size_t header_end_pos = std::string::npos;
  ssize_t chunk_sz;

  std::string request;

  while (request.size() < static_cast<size_t>(MAX_REQUEST_SZ) &&
         (chunk_sz = read(client, buffer, sizeof(buffer))) > 0) {
    request.append(buffer, chunk_sz);
    header_end_pos = request.find(HEADER_END);
    if (header_end_pos != std::string::npos) {
      break;
    }
  }

  if (request.size() > static_cast<size_t>(MAX_REQUEST_SZ)) {
    reply(client, "HTTP/1.1 413 Content Too Large", "Content Too Large");
    close(client);
    return;
  }

  if (header_end_pos == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    close(client);
    return;
  }

  if (request.compare(0, strlen("GET "), "GET ") == 0) {
    std::string headers = request.substr(0, header_end_pos);
    Request *rq = Request::make_get_request(client, headers);
    if (rq != nullptr) {
      rq->handle(server_state);
      delete rq;
    }
    close(client);
    return;
  }

  if (request.compare(0, strlen("POST "), "POST ") == 0) {
    std::string headers = request.substr(0, header_end_pos);

    ssize_t content_length = parse_content_length(client, headers);
    if (content_length < 0) {
      close(client);
      return;
    }

    std::string body = request.substr(header_end_pos + sizeof HEADER_END - 1);
    body = read_body(client, content_length, body);
    if (body.size() != static_cast<size_t>(content_length)) {
      close(client);
      return;
    }

    Request *rq = Request::make_post_request(client, headers, body);
    if (rq != nullptr) {
      rq->handle(server_state);
      delete rq;
    }
    close(client);
    return;
  }

  reply(client, "HTTP/1.1 405 Method Not Allowed", (request.substr(0, 10) + "...").c_str());
  close(client);
}

int process_request()
{
  if (shutdown_requested) {
    return 0;
  }

  struct sockaddr_in client_addr;
  socklen_t addrlen = sizeof client_addr;
  int client = accept(server_socket, reinterpret_cast<sockaddr *>(&client_addr), &addrlen);
  if (client < 0) {
    if (shutdown_requested || errno == EINTR || errno == EBADF) {
      return 0;
    }
    log_error("accept failed");
    return -1;
  }

  std::thread worker(handle_client_request, client);
  worker.detach();
  return 0;
}

// The main workhorse.
int main(int argc, char **argv)
{
  uint16_t port = DEFAULT_PORT;
  if (argc > 2) {
    print_usage(argv[0], "Error: too many arguments.");
    return EXIT_FAILURE;
  }

  if (argc == 2) {
    const char *port_str = argv[1];
    if (port_str == nullptr || *port_str == '\0') {
      print_usage(argv[0], "Error: port must be a positive integer.");
      return EXIT_FAILURE;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long parsed = std::strtoul(port_str, &end, 10);
    if (errno != 0 || end == port_str || *end != '\0' || parsed == 0 || parsed > MAX_PORT) {
      print_usage(argv[0], "Error: port must be a positive integer.");
      return EXIT_FAILURE;
    }

    port = static_cast<uint16_t>(parsed);
  }

  if (!create_pid_file()) {
    return EXIT_FAILURE;
  }

  struct sigaction sa{};
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, nullptr) != 0) {
    log_error("sigaction(SIGINT) failed");
    remove_pid_file();
    return EXIT_FAILURE;
  }
  if (sigaction(SIGTERM, &sa, nullptr) != 0) {
    log_error("sigaction(SIGTERM) failed");
    remove_pid_file();
    return EXIT_FAILURE;
  }

  if (init_socket(port) != 0) {
    remove_pid_file();
    return EXIT_FAILURE;
  }

  if (!server_state.ensure_scripts_root()) {
    std::cerr << "Error: could not create scripts directory under PSIRVER_HOME.\n";
    if (server_socket >= 0) {
      close(server_socket);
    }
    remove_pid_file();
    return EXIT_FAILURE;
  }

  server_state.initialize_next_script_id();

  while (true) {
    if (shutdown_requested) {
      break;
    }
    process_request();
    if (shutdown_requested) {
      break;
    }
  }

  if (server_socket >= 0) {
    close(server_socket);
  }
  syslog(LOG_NOTICE, "Psirver shutting down on SIGINT");
  server_state.cleanup_all_scripts();
  remove_pid_file();
  return EXIT_SUCCESS;
}
