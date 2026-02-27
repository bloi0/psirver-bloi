#include "Requests.hh"
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <cstring>

static bool parse_strict_id(const std::string &text, int &id)
{
  if (text.empty()) {
    return false;
  }

  for (char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
  }

  try {
    size_t idx = 0;
    long value = std::stol(text, &idx, 10);
    if (idx != text.size() || value <= 0) {
      return false;
    }
    id = static_cast<int>(value);
  } catch (...) {
    return false;
  }

  return true;
}

// Helper function to send HTTP response
void Request::reply(int client, const char *status_line, const char *body)
{
  std::string headers;
  headers.reserve(256);
  headers.append(status_line);
  headers.append("\r\n");
  headers.append("Content-Type: text/plain; charset=utf-8\r\n");
  headers.append("Content-Length: ");
  headers.append(std::to_string(strlen(body)));
  headers.append("\r\n");
  headers.append("Connection: close\r\n");
  headers.append("\r\n");

  write(client, headers.data(), headers.size());
  write(client, body, strlen(body));
}

UploadRequest::UploadRequest(int client, std::string headers, std::string body) {
  std::cout << "DEBUG: UploadRequest constructed" << std::endl;
  this->client_socket = client;
  
  // Extract boundary from Content-Type header
  size_t boundary_pos = headers.find("boundary=");
  if (boundary_pos == std::string::npos) {
    return;
  }
  
  std::string boundary = "--";
  size_t boundary_start = boundary_pos + 9; // Length of "boundary="
  size_t boundary_end = headers.find('\r', boundary_start);
  if (boundary_end == std::string::npos) boundary_end = headers.find('\n', boundary_start);
  if (boundary_end == std::string::npos) boundary_end = headers.find(';', boundary_start);
  if (boundary_end == std::string::npos) {
    boundary.append(headers.substr(boundary_start));
  } else {
    boundary.append(headers.substr(boundary_start, boundary_end - boundary_start));
  }
  
  // Find the start of the file data
  size_t file_header_start = body.find(boundary);
  if (file_header_start == std::string::npos) {
    return;
  }
  
  file_header_start += boundary.length();
  
  // Find the Content-Disposition header to extract filename
  size_t disposition_pos = body.find("Content-Disposition:", file_header_start);
  if (disposition_pos != std::string::npos) {
    size_t filename_pos = body.find("filename=\"", disposition_pos);
    if (filename_pos != std::string::npos) {
      filename_pos += 10; // Length of "filename=\""
      size_t filename_end = body.find("\"", filename_pos);
      if (filename_end != std::string::npos) {
        this->filename = body.substr(filename_pos, filename_end - filename_pos);
      }
    }
  }
  
  // Find the blank line that separates headers from data
  size_t data_start = body.find("\r\n\r\n", file_header_start);
  if (data_start == std::string::npos) {
    data_start = body.find("\n\n", file_header_start);
    if (data_start != std::string::npos) {
      data_start += 2;
    }
  } else {
    data_start += 4;
  }
  
  if (data_start == std::string::npos) {
    return;
  }
  
  // Find the ending boundary
  std::string end_boundary = "\r\n" + boundary + "--";
  size_t data_end = body.find(end_boundary, data_start);
  if (data_end == std::string::npos) {
    end_boundary = "\n" + boundary + "--";
    data_end = body.find(end_boundary, data_start);
  }
  if (data_end == std::string::npos) {
    // Try without the --
    end_boundary = "\r\n" + boundary;
    data_end = body.find(end_boundary, data_start);
    if (data_end == std::string::npos) {
      end_boundary = "\n" + boundary;
      data_end = body.find(end_boundary, data_start);
    }
  }
  
  if (data_end != std::string::npos) {
    this->script = body.substr(data_start, data_end - data_start);
  } else {
    this->script = body.substr(data_start);
  }
}

RunRequest::RunRequest(int client, std::string headers, std::string body) {
  std::cout << "DEBUG: RunRequest constructed" << std::endl;
  this->client_socket = client;
  this->id = -1;
  
  // Extract ID from path in headers
  // Path is like: POST /scripts/123/run HTTP/1.1
  size_t path_start = headers.find(' ');
  if (path_start != std::string::npos) {
    path_start++;
    size_t path_end = headers.find(' ', path_start);
    if (path_end != std::string::npos) {
      std::string path = headers.substr(path_start, path_end - path_start);
      
      // Extract ID from /scripts/<id>/run
      if (path.find("/scripts/") == 0) {
        std::string rest = path.substr(9); // Skip "/scripts/"
        size_t slash_pos = rest.find('/');
        if (slash_pos != std::string::npos) {
          int parsed_id = -1;
          if (parse_strict_id(rest.substr(0, slash_pos), parsed_id)) {
            this->id = parsed_id;
          }
        }
      }
    }
  }
  
  // Parse the body for args=value
  // Body format: args=val1,val2 (URL-encoded)
  if (body.find("args=") == 0) {
    this->args = body.substr(5); // Skip "args="
    
    // URL decode the args (decode %XX sequences)
    std::string decoded;
    for (size_t i = 0; i < this->args.length(); i++) {
      if (this->args[i] == '%' && i + 2 < this->args.length()) {
        // Decode %XX
        std::string hex = this->args.substr(i + 1, 2);
        try {
          char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
          decoded += ch;
          i += 2;
        } catch (...) {
          decoded += this->args[i];
        }
      } else if (this->args[i] == '+') {
        decoded += ' ';
      } else {
        decoded += this->args[i];
      }
    }
    this->args = decoded;
  }
}

// This function parses the headers and returns one of the GET request
// objects
Request *Request::make_get_request(int client, std::string headers)
{
  // Extract the path from "GET /path HTTP/1.1\r\n..."
  size_t path_start = headers.find(' ');
  if (path_start == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return nullptr;
  }
  path_start++; // Skip the space after GET
  
  size_t path_end = headers.find(' ', path_start);
  if (path_end == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return nullptr;
  }
  
  std::string path = headers.substr(path_start, path_end - path_start);
  
  // Route to appropriate Request class
  if (path == "/health") {
    return new HealthRequest(client);
  }
  else if (path == "/teapot") {
    return new TeapotRequest(client);
  }
  else if (path == "/jobs") {
    return new ListRequest(client);
  }
  else if (path == "/scripts") {
    return new ListScriptsRequest(client);
  }
  else if (path.find("/jobs/") == 0) {
    // Extract ID and check for sub-paths
    std::string rest = path.substr(6); // Skip "/jobs/"
    size_t slash_pos = rest.find('/');
    
    if (slash_pos == std::string::npos) {
      // /jobs/<id>
      int id = -1;
      if (parse_strict_id(rest, id)) {
        return new JobStatusRequest(client, id);
      }

      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return nullptr;
    }
    else {
      // Extract ID and sub-path
      std::string id_str = rest.substr(0, slash_pos);
      std::string subpath = rest.substr(slash_pos);
      int id = -1;
      if (!parse_strict_id(id_str, id)) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
        return nullptr;
      }

      if (subpath == "/terminate") {
        return new TerminateRequest(client, id);
      }
      else if (subpath == "/stdout") {
        return new StdoutRequest(client, id);
      }
      else if (subpath == "/stderr") {
        return new StderrRequest(client, id);
      }
      else {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
        return nullptr;
      }
    }
  }
  else if (path.find("/scripts/") == 0) {
    // Extract ID and check for /delete
    std::string rest = path.substr(9); // Skip "/scripts/"
    size_t slash_pos = rest.find('/');
    
    if (slash_pos == std::string::npos) {
      // /scripts/<id> without /delete - not a valid GET endpoint
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return nullptr;
    }
    else {
      std::string id_str = rest.substr(0, slash_pos);
      std::string subpath = rest.substr(slash_pos);
      
      if (subpath == "/delete") {
        int id = -1;
        if (parse_strict_id(id_str, id)) {
          return new DeleteRequest(client, id);
        }

        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
        return nullptr;
      }
      else {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
        return nullptr;
      }
    }
  }
  else {
    // Unknown path
    reply(client, "HTTP/1.1 404 Not Found", "Not Found");
    return nullptr;
  }
  
  return nullptr;
}

// This function parses the headers and the body returns one of the
// POST request objects
Request *Request::make_post_request(int client, std::string headers, std::string body)
{
  // Extract the path from "POST /path HTTP/1.1\r\n..."
  size_t path_start = headers.find(' ');
  if (path_start == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return nullptr;
  }
  path_start++; // Skip the space after POST
  
  size_t path_end = headers.find(' ', path_start);
  if (path_end == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return nullptr;
  }
  
  std::string path = headers.substr(path_start, path_end - path_start);
  
  // Check Content-Type (case-insensitive)
  std::string content_type;
  std::string headers_lower = headers;
  std::transform(headers_lower.begin(), headers_lower.end(), headers_lower.begin(), ::tolower);
  
  size_t ct_pos = headers_lower.find("content-type:");
  
  if (ct_pos != std::string::npos) {
    size_t ct_start = ct_pos + 13; // Length of "content-type:"
    // Skip whitespace
    while (ct_start < headers.length() && (headers[ct_start] == ' ' || headers[ct_start] == '\t')) {
      ct_start++;
    }
    size_t ct_end = headers.find('\r', ct_start);
    if (ct_end == std::string::npos) ct_end = headers.find('\n', ct_start);
    if (ct_end == std::string::npos) ct_end = headers.length(); // Use end of string if no newline found
    if (ct_end != std::string::npos) {
      content_type = headers.substr(ct_start, ct_end - ct_start);
    }
  }
  
  // Route to appropriate POST handler
  if (path == "/scripts/upload") {
    // Expect multipart/form-data
    if (content_type.find("multipart/form-data") == std::string::npos) {
      reply(client, "HTTP/1.1 415 Unsupported Media Type", "Unsupported Media Type");
      return nullptr;
    }
    UploadRequest *task = new UploadRequest(client, headers, body);
    if (task->filename.empty() || task->script.empty()) {
      delete task;
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return nullptr;
    }
    if (task->filename.size() < 3 || task->filename.substr(task->filename.size() - 3) != ".py") {
      delete task;
      reply(client, "HTTP/1.1 400 Bad Request", "Only Python scripts are allowed");
      return nullptr;
    }
    return task;
  }
  else if (path.find("/scripts/") == 0 && path.find("/run") != std::string::npos) {
    // Extract ID from /scripts/<id>/run
    std::string rest = path.substr(9); // Skip "/scripts/"
    size_t slash_pos = rest.find('/');
    
    if (slash_pos == std::string::npos || rest.substr(slash_pos) != "/run") {
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return nullptr;
    }

    std::string id_str = rest.substr(0, slash_pos);
    int id = -1;
    if (!parse_strict_id(id_str, id)) {
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return nullptr;
    }
    
    // Expect application/x-www-form-urlencoded
    if (content_type.find("application/x-www-form-urlencoded") == std::string::npos) {
      reply(client, "HTTP/1.1 415 Unsupported Media Type", "Unsupported Media Type");
      return nullptr;
    }

    if (!body.empty() && body.find("args=") != 0) {
      reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
      return nullptr;
    }

    RunRequest *task = new RunRequest(client, headers, body);
    if (task->id < 0) {
      delete task;
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return nullptr;
    }

    return task;
  }
  else {
    // Unknown POST path
    reply(client, "HTTP/1.1 404 Not Found", "Not Found");
    return nullptr;
  }
  
  return nullptr;
}

