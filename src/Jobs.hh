#pragma once
#include <map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <signal.h>

class Script {
private:
  std::size_t script_id;
  std::string name;
public:
  std::mutex jobs_mutex;
  std::atomic<std::size_t> n_jobs;
  Script(std::size_t script_id, std::string f) :
    script_id(script_id), name(f) {
    n_jobs = 0;
  }
  std::size_t get_id() const { return script_id; }
  const std::string& get_name() const { return name; }
  std::string format() const;  
  static void terminate_all();
  static std::vector<std::unique_ptr<Script>> scripts;
  static std::mutex mutex;
};

enum class JobStatus {
  RUNNING,
  FINISHED,
  FAILED,
  TERMINATED,
  TIMED_OUT,
  OUTPUT_LIMITED
};

enum class JobLaunchResult {
  OK,
  TOO_MANY_JOBS,
  PIPE_ERROR,
  FORK_ERROR
};
  
class Job {
private:
  pid_t pid;
public:
  Script *script = nullptr;

private:
  int stderr_fd = -1;
  int stdout_fd = -1;
  std::atomic<bool> output_limit_exceeded{false};
  std::atomic<bool> timed_out{false};

  // These variables are needed to control the timer thread
  std::atomic<bool> finished{false};
  std::mutex state_mutex;
  std::condition_variable state_cv;
  
  std::string stderr_data;
  std::string stdout_data;
  JobStatus status;
  int result;

  // I/O and time monitoring threads
  std::thread stderr_reader, stdout_reader, timer;
 
  void read_all(int fd, std::string& out);  
  void stderr_reader_fn() { read_all(stderr_fd, stderr_data); }  
  void stdout_reader_fn() { read_all(stdout_fd, stdout_data); }
  void timer_fn();
  void monitor_fn();
 
public:
  Job(pid_t pid, int out_fd, int err_fd, Script *s)
    : pid(pid),
      script(s),
      stderr_fd(err_fd),
      stdout_fd(out_fd),
      status(JobStatus::RUNNING),
      result(0) {}

  pid_t get_pid() const { return pid; }
  const std::string get_stderr() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return stderr_data;
  }
  const std::string get_stdout() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return stdout_data;
  }
  JobStatus get_status() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return status;
  };
  int get_result() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return result;
  };
  Script *get_script() const { return script; }
  void terminate();

  static void exec_child(const std::string& script_filename,
		  const std::vector<std::string>& args,
		  int stdout_pipe[2],
		  int stderr_pipe[2]);
  static std::pair<JobLaunchResult, Job*> launch(Script* script,
	 const std::string& script_filename,
	 const std::vector<std::string>& args);
  
  void start_monitors() {
    stderr_reader = std::thread(&Job::stderr_reader_fn, this);
    stdout_reader = std::thread(&Job::stdout_reader_fn, this);
    timer = std::thread(&Job::timer_fn, this);
    std::thread(&Job::monitor_fn, this).detach();
  }

  static std::map<pid_t, std::unique_ptr<Job>> jobs;
  static std::mutex mutex;
  static constexpr int COMMAND_NOT_RUNNABLE = 127;
  
  static size_t MAX_JOBS;
  static unsigned GRACE_MSEC_LIMIT;
  static unsigned RUN_SEC_LIMIT;
  static std::size_t OUTPUT_LIMIT;
};
