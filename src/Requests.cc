#include "Requests.hh"
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>
#include <fstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <signal.h>

// Global data structures
struct Script {
  int id;
  std::string name;
  std::string path;
};

struct Job {
  int id;
  int script_id;
  std::string script_name;
  std::string args;
  pid_t pid;
  std::string status; // running, finished, failed, timed_out, output_limited
  std::string stdout_path;
  std::string stderr_path;
};

static std::map<int, Script> scripts;
static std::map<int, Job> jobs;
static std::mutex data_mutex;
static int next_script_id = 1;
static int next_job_id = 1;

// Helper function to get PSIRVER_HOME
static std::string get_psirver_home() {
  const char *home = std::getenv("PSIRVER_HOME");
  if (home == nullptr || *home == '\0') {
    return "";
  }
  return std::string(home);
}

// Helper function to ensure directory exists
static bool ensure_directory(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return mkdir(path.c_str(), 0755) == 0;
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

// Helper function to send HTTP response with Location header (for 303 redirects)
void Request::reply_with_location(int client, const char *status_line, const char *location, const char *body)
{
  std::string headers;
  headers.reserve(256);
  headers.append(status_line);
  headers.append("\r\n");
  headers.append("Location: ");
  headers.append(location);
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

int HealthRequest::execute()
{
  reply(client_socket, "HTTP/1.1 200 OK", "OK");
  close(client_socket);
  return 0;
}

int TeapotRequest::execute()
{
  reply(client_socket, "HTTP/1.1 418 I'm a teapot", "418 I'm a teapot");
  close(client_socket);
  return 0;
}

int ListScriptsRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  std::stringstream ss;
  for (const auto &pair : scripts) {
    const Script &s = pair.second;
    ss << s.id << "," << s.name << "\n";
  }
  std::string body = ss.str();
  reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
  close(client_socket);
  return 0;
}

int StderrRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  auto it = jobs.find(this->id);
  if (it == jobs.end()) {
    reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    close(client_socket);
    return 0;
  }
  
  Job &job = it->second;
  
  // Check if process is still running
  if (job.status == "running") {
    int status;
    pid_t result = waitpid(job.pid, &status, WNOHANG);
    if (result == 0) {
      // Still running
      reply(client_socket, "HTTP/1.1 202 Accepted", "Job still running");
      close(client_socket);
      return 0;
    } else if (result > 0) {
      // Process terminated - update status
      if (WIFEXITED(status)) {
        job.status = (WEXITSTATUS(status) == 0) ? "finished" : "failed";
      } else {
        job.status = "failed";
      }
    }
  }
  
  // Read stderr file
  std::ifstream file(job.stderr_path);
  if (!file) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Cannot read stderr file");
    close(client_socket);
    return 0;
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  
  reply(client_socket, "HTTP/1.1 200 OK", content.c_str());
  close(client_socket);
  return 0;
}

int DeleteRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  auto it = scripts.find(this->id);
  if (it == scripts.end()) {
    reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    close(client_socket);
    return 0;
  }
  
  // Delete the file
  unlink(it->second.path.c_str());
  
  // Remove from map
  scripts.erase(it);
  
  reply(client_socket, "HTTP/1.1 200 OK", "OK");
  close(client_socket);
  return 0;
}

int RunRequest::execute()
{
  std::string home = get_psirver_home();
  if (home.empty()) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "PSIRVER_HOME not set");
    close(client_socket);
    return 0;
  }
  
  data_mutex.lock();
  auto it = scripts.find(this->id);
  if (it == scripts.end()) {
    data_mutex.unlock();
    reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    close(client_socket);
    return 0;
  }
  
  Script script = it->second;
  int job_id = next_job_id++;
  data_mutex.unlock();
  
  // Create output directory
  std::string output_dir = home + "/output";
  if (!ensure_directory(output_dir)) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Cannot create output directory");
    close(client_socket);
    return 0;
  }
  
  std::string stdout_path = output_dir + "/" + std::to_string(job_id) + ".stdout";
  std::string stderr_path = output_dir + "/" + std::to_string(job_id) + ".stderr";
  
  // Fork and execute the script
  pid_t pid = fork();
  if (pid < 0) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Fork failed");
    close(client_socket);
    return 0;
  }
  
  if (pid == 0) {
    // Child process
    // Redirect stdout and stderr
    int stdout_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int stderr_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    if (stdout_fd >= 0) {
      dup2(stdout_fd, STDOUT_FILENO);
      close(stdout_fd);
    }
    if (stderr_fd >= 0) {
      dup2(stderr_fd, STDERR_FILENO);
      close(stderr_fd);
    }
    
    // Parse args (comma-separated)
    std::vector<std::string> arg_list;
    std::stringstream ss(this->args);
    std::string arg;
    while (std::getline(ss, arg, ',')) {
      // Trim whitespace
      size_t start = arg.find_first_not_of(" \t\r\n");
      size_t end = arg.find_last_not_of(" \t\r\n");
      if (start != std::string::npos && end != std::string::npos) {
        arg_list.push_back(arg.substr(start, end - start + 1));
      } else if (!arg.empty()) {
        arg_list.push_back(arg);
      }
    }
    
    // Build argv
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("python3"));
    argv.push_back(const_cast<char*>(script.path.c_str()));
    for (auto &a : arg_list) {
      argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);
    
    execvp("python3", argv.data());
    exit(1); // If exec fails
  }
  
  // Parent process - store job info
  Job job;
  job.id = job_id;
  job.script_id = script.id;
  job.script_name = script.name;
  job.args = this->args;
  job.pid = pid;
  job.status = "running";
  job.stdout_path = stdout_path;
  job.stderr_path = stderr_path;
  
  data_mutex.lock();
  jobs[job_id] = job;
  data_mutex.unlock();
  
  // Return 303 See Other with Location: /jobs/<job_id>
  std::string location = "/jobs/" + std::to_string(job_id);
  std::string body = std::to_string(job_id);
  reply_with_location(client_socket, "HTTP/1.1 303 See Other", 
                     location.c_str(), body.c_str());
  close(client_socket);
  return 0;
}

int JobStatusRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  auto it = jobs.find(this->id);
  if (it == jobs.end()) {
    reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    close(client_socket);
    return 0;
  }
  
  Job &job = it->second;
  
  // Check if process is still running
  if (job.status == "running") {
    int status;
    pid_t result = waitpid(job.pid, &status, WNOHANG);
    if (result > 0) {
      // Process has terminated
      if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0) {
          job.status = "finished";
        } else {
          job.status = "failed";
        }
      } else if (WIFSIGNALED(status)) {
        job.status = "failed";
      }
    }
  }
  
  // Format: script_id,script_name,status
  std::stringstream ss;
  ss << job.script_id << "," << job.script_name << "," << job.status;
  std::string body = ss.str();
  
  reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
  close(client_socket);
  return 0;
};

int TerminateRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  auto it = jobs.find(this->id);
  if (it == jobs.end()) {
    reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    close(client_socket);
    return 0;
  }
  
  Job &job = it->second;
  
  // Check if process is still running
  if (job.status == "running") {
    int status;
    pid_t result = waitpid(job.pid, &status, WNOHANG);
    if (result == 0) {
      // Still running - terminate it
      kill(job.pid, SIGTERM);
      // Wait a bit for graceful termination
      usleep(100000); // 100ms
      result = waitpid(job.pid, &status, WNOHANG);
      if (result == 0) {
        // Still running - force kill
        kill(job.pid, SIGKILL);
        waitpid(job.pid, &status, 0);
      }
      job.status = "failed";
    } else if (result > 0) {
      // Already terminated
      if (WIFEXITED(status)) {
        job.status = (WEXITSTATUS(status) == 0) ? "finished" : "failed";
      } else {
        job.status = "failed";
      }
    }
  }
  
  reply(client_socket, "HTTP/1.1 200 OK", "OK");
  close(client_socket);
  return 0;
}

int StdoutRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  auto it = jobs.find(this->id);
  if (it == jobs.end()) {
    reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    close(client_socket);
    return 0;
  }
  
  Job &job = it->second;
  
  // Check if process is still running
  if (job.status == "running") {
    int status;
    pid_t result = waitpid(job.pid, &status, WNOHANG);
    if (result == 0) {
      // Still running
      reply(client_socket, "HTTP/1.1 202 Accepted", "Job still running");
      close(client_socket);
      return 0;
    } else if (result > 0) {
      // Process terminated - update status
      if (WIFEXITED(status)) {
        job.status = (WEXITSTATUS(status) == 0) ? "finished" : "failed";
      } else {
        job.status = "failed";
      }
    }
  }
  
  // Read stdout file
  std::ifstream file(job.stdout_path);
  if (!file) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Cannot read stdout file");
    close(client_socket);
    return 0;
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  
  reply(client_socket, "HTTP/1.1 200 OK", content.c_str());
  close(client_socket);
  return 0;
} 

int UploadRequest::execute()
{
  if (this->script.empty() || this->filename.empty()) {
    reply(client_socket, "HTTP/1.1 400 Bad Request", "Bad Request");
    close(client_socket);
    return 0;
  }
  
  // Validate that it's a Python script
  if (this->filename.size() < 3 || this->filename.substr(this->filename.size() - 3) != ".py") {
    reply(client_socket, "HTTP/1.1 400 Bad Request", "Only Python scripts are allowed");
    close(client_socket);
    return 0;
  }
  
  std::string home = get_psirver_home();
  if (home.empty()) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "PSIRVER_HOME not set");
    close(client_socket);
    return 0;
  }
  
  std::string scripts_dir = home + "/scripts";
  if (!ensure_directory(scripts_dir)) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Cannot create scripts directory");
    close(client_socket);
    return 0;
  }
  
  std::lock_guard<std::mutex> lock(data_mutex);
  int script_id = next_script_id++;
  std::string script_path = scripts_dir + "/" + std::to_string(script_id) + ".py";
  
  // Write script to file
  std::ofstream file(script_path, std::ios::binary);
  if (!file) {
    reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Cannot write script file");
    close(client_socket);
    return 0;
  }
  file.write(this->script.c_str(), this->script.size());
  file.close();
  
  // Make script executable
  chmod(script_path.c_str(), 0755);
  
  // Store script metadata
  Script s;
  s.id = script_id;
  s.name = this->filename;
  s.path = script_path;
  scripts[script_id] = s;
  
  std::string body = std::to_string(script_id);
  reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
  close(client_socket);
  return 0;
}

int ListRequest::execute()
{
  std::lock_guard<std::mutex> lock(data_mutex);
  std::stringstream ss;
  for (const auto &pair : jobs) {
    const Job &j = pair.second;
    ss << j.id << "," << j.script_id << "," << j.script_name << "\n";
  }
  std::string body = ss.str();
  reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
  close(client_socket);
  return 0;
}
 
UploadRequest::UploadRequest(int client, std::string headers, std::string body) {
  this->client_socket = client;
  
  // Extract boundary from Content-Type header
  size_t boundary_pos = headers.find("boundary=");
  if (boundary_pos == std::string::npos) {
    return; // Will be handled as error in execute
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
  this->client_socket = client;
  
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
          try {
            this->id = std::stoi(rest.substr(0, slash_pos));
          } catch (...) {
            this->id = -1; // Invalid ID
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
      try {
        int id = std::stoi(rest);
        return new JobStatusRequest(client, id);
      } catch (...) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
        return nullptr;
      }
    }
    else {
      // Extract ID and sub-path
      std::string id_str = rest.substr(0, slash_pos);
      std::string subpath = rest.substr(slash_pos);
      
      try {
        int id = std::stoi(id_str);
        
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
      } catch (...) {
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
        try {
          int id = std::stoi(id_str);
          return new DeleteRequest(client, id);
        } catch (...) {
          reply(client, "HTTP/1.1 404 Not Found", "Not Found");
          return nullptr;
        }
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
    return new UploadRequest(client, headers, body);
  }
  else if (path.find("/scripts/") == 0 && path.find("/run") != std::string::npos) {
    // Extract ID from /scripts/<id>/run
    std::string rest = path.substr(9); // Skip "/scripts/"
    size_t slash_pos = rest.find('/');
    
    if (slash_pos == std::string::npos || rest.substr(slash_pos) != "/run") {
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return nullptr;
    }
    
    // Expect application/x-www-form-urlencoded
    if (content_type.find("application/x-www-form-urlencoded") == std::string::npos) {
      reply(client, "HTTP/1.1 415 Unsupported Media Type", "Unsupported Media Type");
      return nullptr;
    }
    
    return new RunRequest(client, headers, body);
  }
  else {
    // Unknown POST path
    reply(client, "HTTP/1.1 404 Not Found", "Not Found");
    return nullptr;
  }
  
  return nullptr;
}

