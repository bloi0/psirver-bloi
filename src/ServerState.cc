#include "ServerState.hh"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

static bool has_python_extension(const std::string &name)
{
  return name.size() >= 3 && name.substr(name.size() - 3) == ".py";
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

static bool read_script_record_from_disk(const ServerState &state, int id, ScriptRecord &record)
{
  std::string script_dir = state.get_scripts_root() + "/" + std::to_string(id);
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

std::string ServerState::get_psirver_home() const
{
  const char *psirver_home = std::getenv("PSIRVER_HOME");
  if (psirver_home == nullptr || *psirver_home == '\0') {
    return ".";
  }
  return psirver_home;
}

std::string ServerState::get_scripts_root() const
{
  return get_psirver_home() + "/scripts";
}

bool ServerState::ensure_scripts_root() const
{
  return ensure_directory(get_scripts_root());
}

int ServerState::find_smallest_available_script_id() const
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

std::string ServerState::original_filename_basename(const std::string &name) const
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

std::vector<ScriptListRecord> ServerState::scan_scripts_from_disk() const
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

bool ServerState::get_script_record(int id, ScriptRecord &record)
{
  std::lock_guard<std::mutex> lock(script_mutex);

  std::map<int, ScriptRecord>::iterator it = scripts.find(id);
  if (it != scripts.end()) {
    struct stat st;
    if (stat(it->second.path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      record = it->second;
      return true;
    }
  }

  if (!read_script_record_from_disk(*this, id, record)) {
    return false;
  }

  scripts[id] = record;
  return true;
}

void ServerState::initialize_next_script_id()
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

void ServerState::cleanup_all_scripts()
{
  std::lock_guard<std::mutex> lock(script_mutex);

  for (std::map<int, ScriptRecord>::iterator it = scripts.begin(); it != scripts.end(); ++it) {
    const ScriptRecord &rec = it->second;
    std::string script_dir = get_scripts_root() + "/" + std::to_string(rec.id);
    chmod(script_dir.c_str(), 0700);
    unlink(rec.path.c_str());
    rmdir(script_dir.c_str());
  }
  scripts.clear();
}

bool ServerState::write_text_file(const std::string &path, const std::string &content, mode_t mode) const
{
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) {
    return false;
  }
  ssize_t written = write(fd, content.data(), content.size());
  close(fd);
  return written >= 0 && static_cast<size_t>(written) == content.size();
}

std::string ServerState::read_text_file(const std::string &path) const
{
  std::string result;
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return result;
  }

  char buffer[4096];
  ssize_t n = 0;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
    result.append(buffer, n);
  }
  close(fd);
  return result;
}

std::vector<std::string> ServerState::split_args(const std::string &raw) const
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

std::string ServerState::format_mtime(time_t value) const
{
  std::tm tm_value{};
  localtime_r(&value, &tm_value);
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%m/%d/%Y %H:%M:%S", &tm_value) == 0) {
    return "01/01/1970 00:00:00";
  }
  return buffer;
}

const char *ServerState::state_to_cstr(JobState state) const
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

void ServerState::refresh_job(JobRecord &job) const
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
