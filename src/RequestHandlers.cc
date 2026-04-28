#include "Requests.hh"
#include "ServerState.hh"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void HealthRequest::handle(ServerState &)
{
  Request::reply(client_socket, "HTTP/1.1 200 OK", "Running");
}

void TeapotRequest::handle(ServerState &)
{
  Request::reply(client_socket, "HTTP/1.1 418 I'm a Teapot", "Running");
}

void ListRequest::handle(ServerState &state)
{
  std::string body;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    for (std::map<int, JobRecord>::iterator it = state.jobs.begin(); it != state.jobs.end(); ++it) {
      state.refresh_job(it->second);
      body += std::to_string(it->first);
      body += ":";
      body += state.state_to_cstr(it->second.state);
      body += "\n";
    }
  }

  if (body.empty()) {
    body = "\n";
  }

  Request::reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
}

void ListScriptsRequest::handle(ServerState &state)
{
  std::string body;
  std::vector<ScriptListRecord> script_records = state.scan_scripts_from_disk();
  for (size_t i = 0; i < script_records.size(); ++i) {
    body += std::to_string(script_records[i].id);
    body += ",";
    body += script_records[i].filename;
    body += ",";
    body += state.format_mtime(script_records[i].mtime);
    if (i + 1 < script_records.size()) {
      body += "\n";
    }
  }

  Request::reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
}

void DeleteRequest::handle(ServerState &state)
{
  ScriptRecord rec;
  if (!state.get_script_record(id, rec)) {
    Request::reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    return;
  }

  std::string script_dir = state.get_scripts_root() + "/" + std::to_string(id);
  if (chmod(script_dir.c_str(), 0700) != 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }
  if (unlink(rec.path.c_str()) != 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }
  if (rmdir(script_dir.c_str()) != 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    state.scripts.erase(id);
  }

  std::string body_out = std::to_string(id);
  Request::reply(client_socket, "HTTP/1.1 200 OK", body_out.c_str());
}

void JobStatusRequest::handle(ServerState &state)
{
  std::string body;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    std::map<int, JobRecord>::iterator it = state.jobs.find(id);
    if (it != state.jobs.end()) {
      state.refresh_job(it->second);
      body = std::string("id=") + std::to_string(it->second.id) +
             " status=" + state.state_to_cstr(it->second.state);
      found = true;
    }
  }

  if (!found) {
    Request::reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    return;
  }

  Request::reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
}

void TerminateRequest::handle(ServerState &state)
{
  bool found = false;
  pid_t pid_to_kill = -1;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    std::map<int, JobRecord>::iterator it = state.jobs.find(id);
    if (it != state.jobs.end()) {
      state.refresh_job(it->second);
      if (it->second.state == JobState::RUNNING) {
        pid_to_kill = it->second.pid;
      }
      found = true;
    }
  }

  if (!found) {
    Request::reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    return;
  }

  if (pid_to_kill > 0) {
    kill(pid_to_kill, SIGTERM);
    std::lock_guard<std::mutex> lock(state.script_mutex);
    std::map<int, JobRecord>::iterator it = state.jobs.find(id);
    if (it != state.jobs.end()) {
      state.refresh_job(it->second);
    }
  }

  Request::reply(client_socket, "HTTP/1.1 200 OK", "OK");
}

void StdoutRequest::handle(ServerState &state)
{
  std::string stdout_path;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    std::map<int, JobRecord>::iterator it = state.jobs.find(id);
    if (it != state.jobs.end()) {
      state.refresh_job(it->second);
      stdout_path = it->second.stdout_path;
      found = true;
    }
  }

  if (!found) {
    Request::reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    return;
  }

  std::string body = state.read_text_file(stdout_path);
  Request::reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
}

void StderrRequest::handle(ServerState &state)
{
  std::string stderr_path;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    std::map<int, JobRecord>::iterator it = state.jobs.find(id);
    if (it != state.jobs.end()) {
      state.refresh_job(it->second);
      stderr_path = it->second.stderr_path;
      found = true;
    }
  }

  if (!found) {
    Request::reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    return;
  }

  std::string body = state.read_text_file(stderr_path);
  Request::reply(client_socket, "HTTP/1.1 200 OK", body.c_str());
}

void UploadRequest::handle(ServerState &state)
{
  if (!state.ensure_scripts_root()) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }

  int id = state.find_smallest_available_script_id();
  if (id <= 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }

  ScriptRecord rec;
  rec.id = id;
  rec.filename = state.original_filename_basename(filename);
  if (rec.filename.empty()) {
    Request::reply(client_socket, "HTTP/1.1 400 Bad Request", "Bad Request");
    return;
  }

  std::string script_dir = state.get_scripts_root() + "/" + std::to_string(id);
  if (mkdir(script_dir.c_str(), 0700) != 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }

  rec.path = script_dir + "/" + rec.filename;
  if (!state.write_text_file(rec.path, script, 0600)) {
    rmdir(script_dir.c_str());
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }

  chmod(rec.path.c_str(), 0400);
  chmod(script_dir.c_str(), 0500);
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    state.scripts[id] = rec;
  }

  std::string body_out = std::to_string(id);
  Request::reply(client_socket, "HTTP/1.1 200 OK", body_out.c_str());
}

void RunRequest::handle(ServerState &state)
{
  ScriptRecord script_record;
  if (!state.get_script_record(id, script_record)) {
    Request::reply(client_socket, "HTTP/1.1 404 Not Found", "Not Found");
    return;
  }

  int job_id;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    job_id = state.next_job_id++;
  }

  JobRecord job;
  job.id = job_id;
  job.script_id = script_record.id;
  job.exit_code = -1;
  job.state = JobState::RUNNING;
  job.stdout_path = state.get_psirver_home() + "/job_" + std::to_string(job_id) + ".out";
  job.stderr_path = state.get_psirver_home() + "/job_" + std::to_string(job_id) + ".err";

  pid_t pid = fork();
  if (pid < 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
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

    std::vector<std::string> arg_values = state.split_args(args);
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>("python3"));
    argv.push_back(const_cast<char *>(script_record.path.c_str()));
    for (size_t i = 0; i < arg_values.size(); ++i) {
      argv.push_back(const_cast<char *>(arg_values[i].c_str()));
    }
    argv.push_back(nullptr);

    execvp("python3", argv.data());
    _exit(127);
  }

  job.pid = pid;
  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    state.jobs[job_id] = job;
  }

  int status = 0;
  if (wait4(pid, &status, 0, nullptr) < 0) {
    Request::reply(client_socket, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(state.script_mutex);
    std::map<int, JobRecord>::iterator it = state.jobs.find(job_id);
    if (it != state.jobs.end()) {
      it->second.state = JobState::COMPLETED;
    }
  }

  std::string body_out = std::to_string(job_id);
  Request::reply(client_socket, "HTTP/1.1 200 OK", body_out.c_str());
}
