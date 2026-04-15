#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <syslog.h> 
#include <unistd.h>

#include "utils.hh"
#include "Tasks.hh"
#include "Jobs.hh"

// Configuration options and other constants
static constexpr ssize_t MAX_REQUEST_SZ = 0x10000;
static constexpr size_t READ_BUFFER_SZ = 0x1000;

// Global variables (are evil)
int server_socket;
std::string pid_path;

// Reply to the client with an HTTP status line and a human-readable
// response body. Library functions used:
// - std::to_string()
void reply(int client, const char *status_line, const char *body,
	   const char *extra)
{
  const size_t body_len = std::strlen(body);
  
  std::string response;
  response.reserve(256 + body_len);
  response.append(status_line);
  response.append(RN);
  response.append("Content-Type: text/plain; charset=utf-8");
  response.append(RN);
  response.append("Content-Length: ");
  response.append(std::to_string(body_len));
  response.append(RN);
  response.append("Connection: close");
  if (extra) {
    response.append(RN);
    response.append(extra);
  }
  response.append(END_OF_HEADER);
  response.append(body, body_len);

  ::write(client, response.data(), response.size());
  ::close(client);
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
  server_socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    syslog(LOG_ERR, "Socket: %s", strerror(errno));
    return -1;
  }

  int opt = 1;
  ::setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  
  struct sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;           // IPv4
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all available interfaces
  server_addr.sin_port = htons(port);         // Set the port

  if (::bind(server_socket, reinterpret_cast<sockaddr *>(&server_addr),
	   sizeof(server_addr)) < 0) {
    syslog(LOG_ERR, "Bind: %s", strerror(errno));
    ::close(server_socket);
    return -1;
  }
  
  if (::listen(server_socket, SOMAXCONN) != 0) {
    syslog(LOG_ERR, "Listen: %s", strerror(errno));
    ::close(server_socket);
    return -1;
  }

  // Prevent leaking server_socket into child processes
  if(-1 == ::fcntl(server_socket, F_SETFD, FD_CLOEXEC)) {
    syslog(LOG_WARNING, "fcntl: %s", strerror(errno));
  }
  
  return 0;
}

// Given the headers, extract the context length (only for
// POST). Library functions used:
// - std::stoi()

static ssize_t parse_content_length(int client, std::string headers)
{
  constexpr char CL[] = "Content-Length: ";
  size_t pos = headers.find(CL);
  if (pos == std::string::npos) {
    reply(client, "HTTP/1.1 411 Length Required", "Length Required");
    return -1;
  }
  
  std::string rest = headers.substr(pos + sizeof CL - 1);
  size_t content_length_end = rest.find("\n");
  if (content_length_end == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return -1;
  }
  
  std::string content_length_str = headers.substr(pos + sizeof CL - 1,
						  content_length_end);
  size_t content_length;
  try {
    content_length = std::stoi(content_length_str);
  } catch (...) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return -1;    
  }
  
  if (content_length > MAX_REQUEST_SZ) {
    reply(client, "HTTP/1.1 413 Content Too Large", "Content Too Large");
    return -1;
  }

  return content_length;
}

// Given the content length and the pre-read body, read the whole
// body. Only for POST. Library functions used:
// - read()
std::string read_body(int client, ssize_t content_length, std::string body)
{
  if (body.size() < static_cast<size_t>(content_length)) {
    body.reserve(content_length);
  }
  
  size_t remaining = static_cast<size_t>(content_length) - body.length(); 
  char buffer[READ_BUFFER_SZ];
  
  while (remaining > 0) {
    const size_t to_read = std::min(remaining, sizeof(buffer));
    const ssize_t chunk_sz = ::read(client, buffer, to_read);
    if (chunk_sz > 0) {
      body.append(buffer, static_cast<size_t>(chunk_sz));
      remaining -= static_cast<size_t>(chunk_sz);
    }
  }
  return body;
}

// Accept a connection, read the request, parse the headers and the
// body (for POST). The function returns a new Task on success and
// nullptr on failure. Library functions used:
// - accept()
// - read()

Task *request2task()
{
  struct sockaddr_in client_addr;
  socklen_t addrlen = sizeof client_addr;

  const int client = ::accept(server_socket,
			      reinterpret_cast<struct sockaddr*>(&client_addr),
			      &addrlen);
  if(client < 0) {
    syslog(LOG_ERR, "Accept: %s", strerror(errno));
    return nullptr;
  }
  
  char buffer[READ_BUFFER_SZ];
  std::string request;
  request.reserve(READ_BUFFER_SZ * 2);
  size_t header_end_pos = std::string::npos;
  
  // This code may read more data than MAX_REQUEST_SZ
  // but by no more than the buffer size
  while (request.size() < static_cast<size_t>(MAX_REQUEST_SZ)) {
    const ssize_t chunk_size = ::read(client, buffer, sizeof(buffer));
    if (chunk_size <= 0) {
      break;
    }
    
    request.append(buffer, static_cast<size_t>(chunk_size));
    header_end_pos = request.find(END_OF_HEADER);
    if (header_end_pos != std::string::npos) {
      break;
    }
  }
  
  if (request.size() > static_cast<size_t>(MAX_REQUEST_SZ)) {
    reply(client, "HTTP/1.1 413 Content Too Large", "Content Too Large");
    return nullptr;
  }
  
  if (header_end_pos == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return nullptr;
  }
  
  const std::string headers = request.substr(0, header_end_pos);
 
  if(request.compare(0, 4, "GET ") == 0) {
    Task *task = Task::construct(client, headers);
    if(!task) {
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return nullptr;
    }
    
    return task;
  }

  if(request.compare(0, 5, "POST ") == 0) {
    ssize_t content_length = parse_content_length(client, headers);
      
    if (content_length < 0) {
      return nullptr;
    }

    std::string body = request.substr(header_end_pos + sizeof END_OF_HEADER - 1);
    body = read_body(client, content_length, body);

    Task *task = Task::construct(client, headers, body);    
    if(!task) {
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return nullptr;
    }

    return task;
  }
  
  reply(client, "HTTP/1.1 405 Method Not Allowed",
	(request.substr(0, 0x10) + "...").c_str());
  return nullptr;
}

// SIGINT handler. Will cause a graceful shutdown. Library functions used:
// - close()
// - unlink()
// - syslog()
// - exit()
void graceful_shutdown(int /* sig_num */)
{
  Script::terminate_all();
  
  ::close(server_socket);
  if (unlink(pid_path.c_str()) != 0) {
    syslog(LOG_WARNING, "Unlink(%s): %s", pid_path.c_str(), strerror(errno));
  }
  syslog(LOG_USER, "Terminated.");
  ::exit(EXIT_SUCCESS);
}

// The main workhorse. Library functions used:
// - none
int main(int argc, char **argv)
{
  // Select the server port
  uint16_t server_port = select_port(argc, argv);

  // Initialize the server socket
  if(init_socket(server_port) < 0 || server_socket < 0) {
    return EXIT_FAILURE;
  }

  // Create $(PSIRVER_HOME)/psirver.pid
  pid_path = init_pid_file();
  
  // Register a graceful shutdown handler on SIGINT
  add_sigint_handler();

  // The main loop
  while(true) { // Not really, but close
    Task *task = request2task();
    
    // Main processing happens here
    if (task) {
      if(task->execute_as_thread) {
	std::thread([task]() {
	  task->execute();
	  delete task;
	}).detach();
      } else {
	task->execute();
	delete task;
      }
    }
  }

  return EXIT_SUCCESS;
}
