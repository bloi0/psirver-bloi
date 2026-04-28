#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <ctime>
#include <sys/types.h>

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

class ServerState {
public:
  bool ensure_scripts_root() const;
  std::string get_psirver_home() const;
  std::string get_scripts_root() const;
  int find_smallest_available_script_id() const;
  std::string original_filename_basename(const std::string &name) const;

  std::vector<ScriptListRecord> scan_scripts_from_disk() const;
  bool get_script_record(int id, ScriptRecord &record);
  void initialize_next_script_id();
  void cleanup_all_scripts();

  bool write_text_file(const std::string &path, const std::string &content, mode_t mode = 0644) const;
  std::string read_text_file(const std::string &path) const;
  std::vector<std::string> split_args(const std::string &raw) const;
  std::string format_mtime(time_t value) const;

  const char *state_to_cstr(JobState state) const;
  void refresh_job(JobRecord &job) const;

  int next_script_id = 1;
  int next_job_id = 1;
  std::map<int, ScriptRecord> scripts;
  std::map<int, JobRecord> jobs;
  mutable std::mutex script_mutex;
};
