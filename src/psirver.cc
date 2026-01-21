#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h> 
#include <fcntl.h>

#include "Requests.hh"

// Configuration options and other constants
static constexpr uint16_t DEFAULT_PORT = 8000;
static constexpr ssize_t MAX_REQUEST_SZ = 0x10000;
static constexpr size_t BUFFER_SZ = 4096;
static constexpr char HEADER_END[] = "\r\n\r\n";

// Global server socket
int server_socket;

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
  close(client);
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
    // --> TODO Call syslog here
    return -1;
  }
    
  struct sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;           // IPv4
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all available interfaces
  server_addr.sin_port = htons(port);         // Set the port

  if (bind(server_socket, reinterpret_cast<sockaddr *>(&server_addr),
	   sizeof(server_addr)) < 0) {
    // --> TODO Call syslog here
    close(server_socket);
    return -1;
  }
  
  if (listen(server_socket, SOMAXCONN) != 0) {
    // --> TODO Call syslog here
    close(server_socket);
    return -1;
  }

  // Prevent leaking server_socket into child processes
  if(-1 == fcntl(server_socket, F_SETFD, FD_CLOEXEC)) {
    // --> TODO Call syslog here but do not fail
  }
  
  return 0;
}

// Given the headers, extract the context length (only for
// POST). Library functions used:
// - none

ssize_t parse_content_length(int client, std::string headers)
{
  constexpr char CL[] = "Content-Length: ";
  size_t pos = headers.find(CL);
  if (pos == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
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
  size_t content_length = std::stoi(content_length_str);
  
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
  size_t remaining = content_length - body.length();
  
  char buffer[BUFFER_SZ];
  while (remaining > 0) {
    ssize_t read_len = std::min((ssize_t)remaining, (ssize_t)sizeof(buffer));
    ssize_t chunk_sz = read(client, buffer, read_len);
    if (chunk_sz > 0) {
      body.append(buffer, chunk_sz);
      remaining -= chunk_sz;
    }
  }
  return body;
}

// Accept a connection, read the request, parse the headers and the
// body (for POST). The function returns 0 on success and -1 on
// failure. Library functions used:
// - accept()
// - read()
int process_request()
{
  struct sockaddr_in client_addr;
  socklen_t addrlen = sizeof client_addr;

  int client = accept(server_socket, (struct sockaddr *)&client_addr, &addrlen);
  if(client < 0) {
    // --> TODO Call syslog here
    return -1;
  }
  
  char buffer[BUFFER_SZ]; 
  size_t header_end_pos = std::string::npos;
  ssize_t chunk_sz;
  
  std::string request;
  
  // This code may read more data than MAX_REQUEST_SZ
  // but by no more than the buffer size
  while (request.size() < MAX_REQUEST_SZ &&
	 (chunk_sz = read(client, buffer, sizeof(buffer))) > 0) {
    request.append(buffer, chunk_sz);
    header_end_pos = request.find(HEADER_END);
    if (header_end_pos != std::string::npos) {
      break;
    }
  }
  
  if (request.size() > MAX_REQUEST_SZ) {
    reply(client, "HTTP/1.1 413 Content Too Large", "Content Too Large");
    return -1;
  }
  
  if(request.compare(0, strlen("GET "), "GET ") == 0) {
    std::string headers = request.substr(0, header_end_pos);

    Request *rq = Request::make_get_request(headers);
    /*
    if(rq == nullptr) {
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return -1;
    }
    */
    
    reply(client, "HTTP/1.1 200 OK", "Hello from Psirver!");
    return 0;
  }

  if(request.compare(0, strlen("POST "), "POST ") == 0) {
    std::string headers = request.substr(0, header_end_pos);

    ssize_t content_length = parse_content_length(client, headers);
      
    if (content_length < 0) {
      return -1;
    }

    std::string body = request.substr(header_end_pos + sizeof HEADER_END - 1);
    body = read_body(client, content_length, body);

    Request *rq = Request::make_post_request(headers, body);
    /*
    if(rq == nullptr) {
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return -1;
    }
    */
    
    reply(client, "HTTP/1.1 200 OK", "Hello from Psirver!");
    return 0;
  }
  
  reply(client, "HTTP/1.1 405 Method Not Allowed",
	(request.substr(0, 10) + "...").c_str());
  return -1;
}

// The main workhorse. Library functions used:
// - close()
int main(int argc, char **argv)
{

  // --> TODO If command-line parameter is provided, treat it as the
  // --> port number. (Make sure it is != 0.) Otherwise, use the
  // --> default port number.
  
  // --> TODO Insert code here that creates
  // --> $(PSIRVER_HOME)/psirver.pid

  // --> TODO Insert code here that registers a graceful shutdown
  // --> handler on SIGINT

  init_socket(DEFAULT_PORT);
  if(server_socket < 0) {
    return EXIT_FAILURE;
  }
    
  while(true) { // Not really, but close
    process_request(); 
  }

  close(server_socket);
  return EXIT_SUCCESS;
}
