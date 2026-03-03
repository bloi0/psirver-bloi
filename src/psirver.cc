#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <csignal>
#include <limits.h>
#include <iostream>
#include <sys/wait.h>
#include <map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <ctime>

#include "Requests.hh"

// Configuration options and other constants
static constexpr uint16_t DEFAULT_PORT = 8000;
static constexpr uint16_t MAX_PORT = 65535;
static constexpr ssize_t MAX_REQUEST_SZ = 0x10000;
static constexpr size_t BUFFER_SZ = 4096;
static constexpr char HEADER_END[] = "\r\n\r\n";

struct ScriptRecord {
  int id;
  std::string filename;
  std::string path;
};

struct ScriptListRecord {
  int id;
  std::string filename;
  std::string path;
  time_t mtime;
};

enum class JobState {
  RUNNING,
  COMPLETED,
  TERMINATED,
  FAILED
};

struct JobRecord {
  int id;
  int script_id;
  pid_t pid;
  int exit_code;
  JobState state;
  std::string stdout_path;
  std::string stderr_path;
};

// Global server socket
int server_socket = -1;
static char pid_file_path[PATH_MAX];
static volatile sig_atomic_t shutdown_requested = 0;
static int next_script_id = 1;
static int next_job_id = 1;
static std::map<int, ScriptRecord> scripts;
static std::map<int, JobRecord> jobs;

static bool parse_positive_id(const std::string &text, int &value)
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
    long parsed = std::stol(text, &idx, 10);
    if (idx != text.size() || parsed <= 0) {
      return false;
    }
    value = static_cast<int>(parsed);
  } catch (...) {
    return false;
  }

  return true;
}

static std::string get_psirver_home()
{
  const char *psirver_home = std::getenv("PSIRVER_HOME");
  if (psirver_home == nullptr || *psirver_home == '\0') {
    return ".";
  }
  return psirver_home;
}

static std::string original_filename_basename(const std::string &name)
{
  if (name.empty()) {
    return "";
  }

  size_t slash = name.find_last_of("/");
  size_t backslash = name.find_last_of("\\");
  size_t cut = std::string::npos;
  if (slash != std::string::npos && backslash != std::string::npos) {
    cut = std::max(slash, backslash);
  } else if (slash != std::string::npos) {
    cut = slash;
  } else {
    cut = backslash;
  }

  if (cut == std::string::npos) {
    return name;
  }
  if (cut + 1 >= name.size()) {
    return "";
  }
  return name.substr(cut + 1);
}

static std::string get_scripts_root()
{
  return get_psirver_home() + "/scripts";
}

static bool ensure_directory(const std::string &path)
{
  if (mkdir(path.c_str(), 0755) == 0) {
    return true;
  }
  if (errno != EEXIST) {
    return false;
  }

  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

static bool ensure_scripts_root()
{
  return ensure_directory(get_scripts_root());
}

static int find_smallest_available_script_id()
{
  if (!ensure_scripts_root()) {
    return -1;
  }

  DIR *root = opendir(get_scripts_root().c_str());
  if (root == nullptr) {
    return -1;
  }

  std::vector<int> used_ids;
  struct dirent *entry = nullptr;
  while ((entry = readdir(root)) != nullptr) {
    std::string dir_name(entry->d_name);
    if (dir_name == "." || dir_name == "..") {
      continue;
    }

    int id = -1;
    if (!parse_positive_id(dir_name, id)) {
      continue;
    }

    std::string script_dir = get_scripts_root() + "/" + dir_name;
    struct stat st;
    if (stat(script_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      continue;
    }

    used_ids.push_back(id);
  }

  closedir(root);

  std::sort(used_ids.begin(), used_ids.end());
  used_ids.erase(std::unique(used_ids.begin(), used_ids.end()), used_ids.end());

  int expected = 1;
  for (size_t i = 0; i < used_ids.size(); ++i) {
    if (used_ids[i] == expected) {
      expected++;
      continue;
    }
    if (used_ids[i] > expected) {
      break;
    }
  }

  return expected;
}

static bool has_python_extension(const std::string &name)
{
  return name.size() >= 3 && name.substr(name.size() - 3) == ".py";
}

static std::string format_mtime(time_t value)
{
  std::tm tm_value{};
  localtime_r(&value, &tm_value);
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%m/%d/%Y %H:%M:%S", &tm_value) == 0) {
    return "01/01/1970 00:00:00";
  }
  return buffer;
}

static bool read_script_file_from_dir(const std::string &dir_path,
                                      std::string &filename,
                                      std::string &path,
                                      time_t &mtime)
{
  DIR *dir = opendir(dir_path.c_str());
  if (dir == nullptr) {
    return false;
  }

  bool found = false;
  std::string chosen_name;
  std::string chosen_path;
  time_t chosen_mtime = 0;

  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name(entry->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    if (!has_python_extension(name)) {
      continue;
    }

    std::string full_path = dir_path + "/" + name;
    struct stat st;
    if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }

    if (!found || name < chosen_name) {
      found = true;
      chosen_name = name;
      chosen_path = full_path;
      chosen_mtime = st.st_mtime;
    }
  }

  closedir(dir);

  if (!found) {
    return false;
  }

  filename = chosen_name;
  path = chosen_path;
  mtime = chosen_mtime;
  return true;
}

static bool read_script_record_from_disk(int id, ScriptRecord &record)
{
  std::string script_dir = get_scripts_root() + "/" + std::to_string(id);
  struct stat st;
  if (stat(script_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    return false;
  }

  time_t mtime = 0;
  std::string filename;
  std::string path;
  if (!read_script_file_from_dir(script_dir, filename, path, mtime)) {
    return false;
  }

  record.id = id;
  record.filename = filename;
  record.path = path;
  return true;
}

static std::vector<ScriptListRecord> scan_scripts_from_disk()
{
  std::vector<ScriptListRecord> records;
  if (!ensure_scripts_root()) {
    return records;
  }

  DIR *root = opendir(get_scripts_root().c_str());
  if (root == nullptr) {
    return records;
  }

  struct dirent *entry = nullptr;
  while ((entry = readdir(root)) != nullptr) {
    std::string dir_name(entry->d_name);
    if (dir_name == "." || dir_name == "..") {
      continue;
    }

    int id = -1;
    if (!parse_positive_id(dir_name, id)) {
      continue;
    }

    std::string script_dir = get_scripts_root() + "/" + dir_name;
    struct stat st;
    if (stat(script_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      continue;
    }

    ScriptListRecord rec;
    rec.id = id;
    if (!read_script_file_from_dir(script_dir, rec.filename, rec.path, rec.mtime)) {
      continue;
    }
    records.push_back(rec);
  }

  closedir(root);

  std::sort(records.begin(), records.end(), [](const ScriptListRecord &a, const ScriptListRecord &b) {
    return a.id < b.id;
  });
  return records;
}

static bool get_script_record(int id, ScriptRecord &record)
{
  std::map<int, ScriptRecord>::iterator it = scripts.find(id);
  if (it != scripts.end()) {
    struct stat st;
    if (stat(it->second.path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      record = it->second;
      return true;
    }
  }

  if (!read_script_record_from_disk(id, record)) {
    return false;
  }

  scripts[id] = record;
  return true;
}

static void initialize_next_script_id()
{
  int max_id = 0;
  std::vector<ScriptListRecord> records = scan_scripts_from_disk();
  for (size_t i = 0; i < records.size(); ++i) {
    if (records[i].id > max_id) {
      max_id = records[i].id;
    }
  }
  next_script_id = max_id + 1;
}

static bool write_text_file(const std::string &path, const std::string &content, mode_t mode = 0644)
{
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) {
    return false;
  }
  ssize_t written = write(fd, content.data(), content.size());
  close(fd);
  return written >= 0 && static_cast<size_t>(written) == content.size();
}

static std::string read_text_file(const std::string &path)
{
  std::string result;
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return result;
  }

  char buffer[BUFFER_SZ];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    result.append(buffer, n);
  }
  close(fd);
  return result;
}

static std::vector<std::string> split_args(const std::string &raw)
{
  std::vector<std::string> out;
  if (raw.empty()) {
    return out;
  }

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    out.push_back(token);
  }
  return out;
}

static const char *state_to_cstr(JobState state)
{
  switch (state) {
  case JobState::RUNNING:
    return "running";
  case JobState::COMPLETED:
    return "completed";
  case JobState::TERMINATED:
    return "terminated";
  case JobState::FAILED:
    return "failed";
  }
  return "failed";
}

static void refresh_job(JobRecord &job)
{
  if (job.state != JobState::RUNNING) {
    return;
  }

  int status = 0;
  pid_t res = waitpid(job.pid, &status, WNOHANG);
  if (res == 0) {
    return;
  }
  if (res < 0) {
    job.state = JobState::FAILED;
    return;
  }

  if (WIFEXITED(status)) {
    job.exit_code = WEXITSTATUS(status);
    job.state = (job.exit_code == 0) ? JobState::COMPLETED : JobState::FAILED;
    return;
  }

  if (WIFSIGNALED(status)) {
    job.exit_code = 128 + WTERMSIG(status);
    job.state = JobState::TERMINATED;
    return;
  }

  job.state = JobState::FAILED;
}

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
  server_addr.sin_family = AF_INET;           // IPv4
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all available interfaces
  server_addr.sin_port = htons(port);         // Set the port

  if (bind(server_socket, reinterpret_cast<sockaddr *>(&server_addr),
	   sizeof(server_addr)) < 0) {
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
  if(-1 == fcntl(server_socket, F_SETFD, FD_CLOEXEC)) {
    syslog(LOG_WARNING, "fcntl(FD_CLOEXEC) warning: %s", strerror(errno));
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
    // Try lowercase
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
    // No newline found, use rest of string
    content_length_end = rest.length();
  }
  
  std::string content_length_str = rest.substr(0, content_length_end);
  // Trim any whitespace
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
  if (content_length < 0) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return "";
  }

  size_t expected = static_cast<size_t>(content_length);
  if (body.size() > expected) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return "";
  }

  size_t remaining = content_length - body.length();
  
  char buffer[BUFFER_SZ];
  while (remaining > 0) {
    ssize_t read_len = std::min((ssize_t)remaining, (ssize_t)sizeof(buffer));
    ssize_t chunk_sz = read(client, buffer, read_len);
    if (chunk_sz > 0) {
      body.append(buffer, chunk_sz);
      remaining -= chunk_sz;
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

// Accept a connection, read the request, parse the headers and the
// body (for POST). The function returns 0 on success and -1 on
// failure. Library functions used:
// - accept()
// - read()
int process_request()
{
  if (shutdown_requested) {
    return 0;
  }
  struct sockaddr_in client_addr;
  socklen_t addrlen = sizeof client_addr;

  int client = accept(server_socket, (struct sockaddr *)&client_addr, &addrlen);
  if(client < 0) {
    if (shutdown_requested || errno == EINTR || errno == EBADF) {
      return 0;
    }
    log_error("accept failed");
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

  if (header_end_pos == std::string::npos) {
    reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
    return -1;
  }
  
  if(request.compare(0, strlen("GET "), "GET ") == 0) {
    std::string headers = request.substr(0, header_end_pos);

    Request *rq = Request::make_get_request(client, headers);
    if(rq == nullptr) {
      // Error already sent by make_get_request
      return -1;
    }

    if (dynamic_cast<HealthRequest *>(rq) != nullptr) {
      reply(client, "HTTP/1.1 200 OK", "Running");
    } else if (dynamic_cast<TeapotRequest *>(rq) != nullptr) {
      reply(client, "HTTP/1.1 418 I'm a Teapot", "Running");
    } else if (dynamic_cast<ListRequest *>(rq) != nullptr) {
      std::string body;
      for (std::map<int, JobRecord>::iterator it = jobs.begin(); it != jobs.end(); ++it) {
        refresh_job(it->second);
        body += std::to_string(it->first);
        body += ":";
        body += state_to_cstr(it->second.state);
        body += "\n";
      }
      if (body.empty()) {
        body = "\n";
      }
      reply(client, "HTTP/1.1 200 OK", body.c_str());
    } else if (dynamic_cast<ListScriptsRequest *>(rq) != nullptr) {
      std::string body;
      std::vector<ScriptListRecord> script_records = scan_scripts_from_disk();
      for (size_t i = 0; i < script_records.size(); ++i) {
        body += std::to_string(script_records[i].id);
        body += ",";
        body += script_records[i].filename;
        body += ",";
        body += format_mtime(script_records[i].mtime);
        if (i + 1 < script_records.size()) {
          body += "\n";
        }
      }
      reply(client, "HTTP/1.1 200 OK", body.c_str());
    } else if (JobStatusRequest *r = dynamic_cast<JobStatusRequest *>(rq)) {
      std::map<int, JobRecord>::iterator it = jobs.find(r->id);
      if (it == jobs.end()) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      } else {
        refresh_job(it->second);
        std::string body = std::string("id=") + std::to_string(it->second.id) +
                           " status=" + state_to_cstr(it->second.state);
        reply(client, "HTTP/1.1 200 OK", body.c_str());
      }
    } else if (TerminateRequest *r = dynamic_cast<TerminateRequest *>(rq)) {
      std::map<int, JobRecord>::iterator it = jobs.find(r->id);
      if (it == jobs.end()) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      } else {
        refresh_job(it->second);
        if (it->second.state == JobState::RUNNING) {
          kill(it->second.pid, SIGTERM);
          refresh_job(it->second);
        }
        reply(client, "HTTP/1.1 200 OK", "OK");
      }
    } else if (StdoutRequest *r = dynamic_cast<StdoutRequest *>(rq)) {
      std::map<int, JobRecord>::iterator it = jobs.find(r->id);
      if (it == jobs.end()) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      } else {
        refresh_job(it->second);
        std::string body = read_text_file(it->second.stdout_path);
        reply(client, "HTTP/1.1 200 OK", body.c_str());
      }
    } else if (StderrRequest *r = dynamic_cast<StderrRequest *>(rq)) {
      std::map<int, JobRecord>::iterator it = jobs.find(r->id);
      if (it == jobs.end()) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      } else {
        refresh_job(it->second);
        std::string body = read_text_file(it->second.stderr_path);
        reply(client, "HTTP/1.1 200 OK", body.c_str());
      }
    } else if (DeleteRequest *r = dynamic_cast<DeleteRequest *>(rq)) {
      ScriptRecord rec;
      if (!get_script_record(r->id, rec)) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      } else {
        std::string script_dir = get_scripts_root() + "/" + std::to_string(r->id);
        if (chmod(script_dir.c_str(), 0700) != 0) {
          reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
        } else if (unlink(rec.path.c_str()) != 0) {
          reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
        } else if (rmdir(script_dir.c_str()) != 0) {
          reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
        } else {
          scripts.erase(r->id);
          std::string body_out = std::to_string(r->id);
          reply(client, "HTTP/1.1 200 OK", body_out.c_str());
        }
      }
    } else {
      reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    }
    delete rq;
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
    if (body.size() != static_cast<size_t>(content_length)) {
      return -1;
    }

    Request *rq = Request::make_post_request(client, headers, body);
    if(rq == nullptr) {
      // Error already sent by make_post_request
      return -1;
    }

    if (UploadRequest *r = dynamic_cast<UploadRequest *>(rq)) {
      if (!ensure_scripts_root()) {
        reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
        delete rq;
        return -1;
      }

      int id = find_smallest_available_script_id();
      if (id <= 0) {
        reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
        delete rq;
        return -1;
      }

      ScriptRecord rec;
      rec.id = id;
      rec.filename = original_filename_basename(r->filename);
      if (rec.filename.empty()) {
        reply(client, "HTTP/1.1 400 Bad Request", "Bad Request");
        delete rq;
        return -1;
      }

      std::string script_dir = get_scripts_root() + "/" + std::to_string(id);
      if (mkdir(script_dir.c_str(), 0700) != 0) {
        reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
        delete rq;
        return -1;
      }

      rec.path = script_dir + "/" + rec.filename;
      if (!write_text_file(rec.path, r->script, 0600)) {
        rmdir(script_dir.c_str());
        reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
      } else {
        chmod(rec.path.c_str(), 0400);
        chmod(script_dir.c_str(), 0500);
        scripts[id] = rec;
        std::string body_out = std::to_string(id);
        reply(client, "HTTP/1.1 200 OK", body_out.c_str());
      }
    } else if (RunRequest *r = dynamic_cast<RunRequest *>(rq)) {
      ScriptRecord script;
      if (!get_script_record(r->id, script)) {
        reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      } else {
        int job_id = next_job_id++;
        JobRecord job;
        job.id = job_id;
        job.script_id = script.id;
        job.exit_code = -1;
        job.state = JobState::RUNNING;
        job.stdout_path = get_psirver_home() + "/job_" + std::to_string(job_id) + ".out";
        job.stderr_path = get_psirver_home() + "/job_" + std::to_string(job_id) + ".err";

        pid_t pid = fork();
        if (pid < 0) {
          reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
          delete rq;
          return -1;
        }

        if (pid == 0) {
          int out_fd = open(job.stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
          int err_fd = open(job.stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (out_fd < 0 || err_fd < 0) {
            _exit(127);
          }

          dup2(out_fd, STDOUT_FILENO);
          dup2(err_fd, STDERR_FILENO);
          close(out_fd);
          close(err_fd);

          std::vector<std::string> arg_values = split_args(r->args);
          std::vector<char *> argv;
          argv.push_back(const_cast<char *>("python3"));
          argv.push_back(const_cast<char *>(script.path.c_str()));
          for (size_t i = 0; i < arg_values.size(); ++i) {
            argv.push_back(const_cast<char *>(arg_values[i].c_str()));
          }
          argv.push_back(nullptr);

          execvp("python3", argv.data());
          _exit(127);
        }

        job.pid = pid;
        jobs[job_id] = job;
        std::string body_out = std::to_string(job_id);
        reply(client, "HTTP/1.1 200 OK", body_out.c_str());
      }
    } else {
      reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    }
    delete rq;
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
  // Parse port number from command line or use default
  uint16_t port = DEFAULT_PORT;
  if (argc > 2) {
    print_usage(argv[0], "Error: too many arguments.");
    return EXIT_FAILURE;
  }

  if (argc == 1) {
    port = DEFAULT_PORT;
  } else if (argc == 2) {
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
  
  // Create PID file in PSIRVER_HOME
  if (!create_pid_file()) {
    return EXIT_FAILURE;
  }

  // Register graceful shutdown handler for SIGINT and SIGTERM
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

  if (!ensure_scripts_root()) {
    std::cerr << "Error: could not create scripts directory under PSIRVER_HOME.\n";
    if (server_socket >= 0) {
      close(server_socket);
    }
    remove_pid_file();
    return EXIT_FAILURE;
  }

  initialize_next_script_id();
    
  while(true) { // Not really, but close
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
  remove_pid_file();
  return EXIT_SUCCESS;
}
