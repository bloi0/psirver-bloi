#include <sys/wait.h>
#include "Tasks.hh"
#include "Jobs.hh"

std::map<pid_t, std::unique_ptr<Job>> Job::jobs;
// Used for locking and unlocking the `jobs` vector
std::mutex Job::mutex;

// Configuration
size_t Job::MAX_JOBS = 100;
unsigned Job::GRACE_MSEC_LIMIT = 250;
unsigned Job::RUN_SEC_LIMIT = 3;
std::size_t Job::OUTPUT_LIMIT = 1024;

void Job::terminate()
{
  ::kill(pid, SIGTERM);
  std::this_thread::sleep_for(std::chrono::milliseconds(GRACE_MSEC_LIMIT));
  ::kill(pid, SIGKILL);
}

void Job::timer_fn()
{
  std::unique_lock<std::mutex> lock(state_mutex);

  // Wait for the duration of the time limit OR termination,
  // whatever comes first.
  if (state_cv.wait_for(lock,
			std::chrono::seconds(RUN_SEC_LIMIT),
			[this] { return finished.load(); })) {
    return;
  }

  timed_out.store(true);
  lock.unlock();
  
  terminate();
}

void Job::monitor_fn()
{
  int wstatus;
  
  int wpstatus = ::waitpid(pid, &wstatus, 0);
  // Release the script
  //script->n_jobs--;		// atomic!

  // Explore the termination reasons
  {
    std::lock_guard<std::mutex> lock(state_mutex);
    
    finished.store(true);
    // Wake up the timer thread
    state_cv.notify_all();
    
    if (wpstatus == -1) {
      status = JobStatus::FAILED;
    } else if (timed_out.load()) {
      status = JobStatus::TIMED_OUT;
    } else if (output_limit_exceeded.load()) {
      status = JobStatus::OUTPUT_LIMITED;
    } else if (WIFEXITED(wstatus) &&
	       WEXITSTATUS(wstatus) != Job::COMMAND_NOT_RUNNABLE) {
      result = WEXITSTATUS(wstatus);
      status = JobStatus::FINISHED;
    } else {
      status = JobStatus::TERMINATED;
    }
  }

  // Stop all threads
  stdout_reader.join();
  stderr_reader.join();
  timer.join();
 
  ::close(stdout_fd);
  ::close(stderr_fd);
}

void Job::read_all(int fd, std::string& out)
{
  char buffer[1024];
  ssize_t n;
  bool should_terminate = false;
  
  while ((n = ::read(fd, buffer, sizeof(buffer))) > 0) {
    if (out.size() > OUTPUT_LIMIT) {
      should_terminate = true;
      break;
    }
    std::size_t remaining = OUTPUT_LIMIT - out.size();
    std::size_t to_append = std::min(remaining, static_cast<std::size_t>(n));
    
    out.append(buffer, to_append);
    
    if (static_cast<std::size_t>(n) > remaining) {
      should_terminate = true;
      break;
    }
  }
  
  if (should_terminate) {
    output_limit_exceeded.store(true);
    terminate();
  }
}

void Job::exec_child(const std::string& script_filename,
                     const std::vector<std::string>& args,
                     int stdout_pipe[2],
                     int stderr_pipe[2])
{
  ::close(stdout_pipe[0]);
  ::close(stderr_pipe[0]);
  
  if (::dup2(stdout_pipe[1], STDOUT_FILENO) == -1 ||
      ::dup2(stderr_pipe[1], STDERR_FILENO) == -1) {
    ::_exit(COMMAND_NOT_RUNNABLE);
  }
  
  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);
  
  std::vector<char*> argv;
  argv.reserve(args.size() + 3);
  argv.push_back(const_cast<char*>("python3"));
  argv.push_back(const_cast<char*>(script_filename.c_str()));
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  
  ::execvp("python3", argv.data());
  ::_exit(COMMAND_NOT_RUNNABLE);
}

std::pair<JobLaunchResult, Job*> Job::launch(
    Script* script,
    const std::string& script_filename,
    const std::vector<std::string>& args)
{
  {
    std::lock_guard<std::mutex> lock(Job::mutex);
    
    if (Job::jobs.size() >= MAX_JOBS) {
      return {JobLaunchResult::TOO_MANY_JOBS, nullptr};
    }
  }

  int stdout_pipe[2];
  int stderr_pipe[2];

  if (::pipe(stdout_pipe) == -1) {
    return {JobLaunchResult::PIPE_ERROR, nullptr};
  }

  if (::pipe(stderr_pipe) == -1) {
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    return {JobLaunchResult::PIPE_ERROR, nullptr};
  }

  pid_t pid = ::fork();

  if (pid < 0) {
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);
    return {JobLaunchResult::FORK_ERROR, nullptr};
  }

  if (pid == 0) {
    exec_child(script_filename, args, stdout_pipe, stderr_pipe);
    ::_exit(COMMAND_NOT_RUNNABLE);
  }

  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);

  auto new_job =
    std::make_unique<Job>(pid, stdout_pipe[0], stderr_pipe[0], script);
  Job* job = new_job.get();
  Job::jobs.emplace(pid, std::move(new_job));

  job->start_monitors();

  return {JobLaunchResult::OK, job};
}
