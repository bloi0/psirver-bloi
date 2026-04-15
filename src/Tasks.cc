#include <syslog.h> 
#include <sys/stat.h>
#include <fstream>

#include "utils.hh"
#include "Tasks.hh"
#include "Jobs.hh"
static constexpr char SCRIPTS_PATH[] = "scripts/";

std::vector<std::unique_ptr<Script>> Script::scripts(1);

// Used for locking and unlocking the `scripts` vector
std::mutex Script::Script::mutex;

/**
 * Delete all uploaded scripts and their storage directories.
 *
 * This function iterates over the global `scripts` table and, for each
 * script entry, reconstructs the corresponding directory path
 * `SCRIPTS_PATH/<id>` and file path `SCRIPTS_PATH/<id>/<filename>`.
 * It then attempts to make the directory user-accessible and the file
 * user-writable, removes the script file, and removes the script's
 * subdirectory.
 *
 * The function performs best-effort cleanup only: it does not check
 * or report failures from `chmod()`, `remove()`, or `rmdir()`, and it
 * does not send any client response. Its intended use is bulk
 * shutdown-time cleanup rather than request-time error handling.
 *
 */

void Script::terminate_all()
{
  for (std::size_t id = 0 ; id < Script::scripts.size(); ++id) {
    if(!Script::scripts[id]) {
      continue;
    }
    const std::string script_dir =
      std::string(SCRIPTS_PATH) + std::to_string(id);
    const std::string script_filename =
      script_dir + "/" + Script::scripts[id]->get_name();
    
    ::chmod(script_dir.c_str(), S_IRWXU);
    ::chmod(script_filename.c_str(), S_IWUSR);
    ::remove(script_filename.c_str());
    ::rmdir(script_dir.c_str());
  }
}

// Do the job, reply to the client, and return to the main loop

int HealthTask::execute()
{
  reply(client, "HTTP/1.1 200 OK", "OK");
  return 0;
}
 
int TeapotTask::execute()
{
  reply(client, "HTTP/1.1 418 I am a teapot", "I am a teapot (maybe)");
  return 0;
}

/**
 * Format this script as one line of the `/scripts` listing.
 *
 * The returned line contains three comma-separated fields: the
 * numeric script ID, the original script filename, and the file's
 * last modification timestamp (`mtime`) formatted as `MM/DD/YYYY
 * HH:mm:ss` in local time. The method obtains `mtime` by calling
 * `stat()` on the script file located at `SCRIPTS_PATH/<id>/<name>`.
 *
 * If the file cannot be stat'ed, the method returns an empty string.
 *
 * The timestamp is initialized to `"00/00/0000 00:00:00"` and is
 * replaced with the formatted local modification time if that time
 * can be converted successfully via `localtime_r()`. If local-time
 * conversion fails, the method still returns a valid listing line,
 * but with the default placeholder timestamp.
 *
 * @return A comma-separated listing line for this script, or an empty
 *         string on failure.
 */

std::string Script::format() const
{
  const std::string script_filename =
    std::string(SCRIPTS_PATH) + std::to_string(script_id) + "/" + name;
  
  struct stat st;
  if (::stat(script_filename.c_str(), &st) != 0) {
    return "";
  }
  
  char buf[20] = "00/00/0000 00:00:00";
  std::tm tm_result;
  if (::localtime_r(&st.st_mtime, &tm_result) != nullptr) {
    ::strftime(buf, sizeof(buf), "%m/%d/%Y %H:%M:%S", &tm_result);
  }
  
  return std::to_string(script_id) + ',' + name + ',' + buf;
}

/**
 * Roll back a failed script upload and report the failure to the client.
 *
 * This helper removes the in-memory script entry identified by `which`,
 * logs the failure to syslog together with the current `errno` message,
 * attempts to delete the partially created script file from
 * `SCRIPTS_PATH/<which>/<filename>`, and sends an HTTP 500 response to the
 * client.
 *
 * The file removal is best-effort: failure to delete the file is ignored.
 * The directory itself is not removed by this method.
 *
 * The `msg` argument supplies context for the log entry, typically the path
 * or operation that failed.
 *
 * @param which Index of the script slot to clear from the global `scripts`
 *              table.
 * @param msg   Context string to include in the error log.
 */

void UploadTask::cleanup(std::size_t which, const std::string &msg)
{
  Script::scripts[which].reset();
  
  const std::string script_filename =
    std::string(SCRIPTS_PATH) + std::to_string(which) + "/" + filename;
  
  syslog(LOG_ERR, "%s: %s", msg.c_str(), strerror(errno));
  ::remove(script_filename.c_str()); // OK to fail
  reply(client, "HTTP/1.1 500 Internal Server Error",
	"Internal Server Error");
}

/**
 * Upload the current script to the server-side script repository.
 *
 * This method assigns the uploaded script the smallest non-negative
 * available script ID, stores metadata for that script in the global
 * `scripts` table, and writes the script body to the corresponding file
 * under `SCRIPTS_PATH/<script_id>/`.
 *
 * The method first locates the first empty slot in `scripts`; if no such
 * slot exists, it appends a new entry. It then ensures that the target
 * script directory exists and is temporarily writable by the user. If the
 * directory does not exist, it is created; if it exists but is not a
 * directory, the upload fails.
 *
 * The script contents are written to a file named `filename` inside that
 * directory. If the file already exists and cannot be opened because it is
 * read-only, the method attempts to restore user write permission and retry
 * the open. After a successful write, the file permissions are restricted
 * to user-read only, and the directory permissions are restricted to
 * user-read and user-execute only.
 *
 * On success, the method sends an HTTP 200 response whose body contains the
 * assigned script ID and returns 0.
 *
 * On any failure, the method invokes `cleanup()` to remove partially written
 * state, sends an HTTP 500 response, and returns 1.
 *
 * @return 0 on success; 1 on failure.
 */

int UploadTask::execute()
{
  Script::mutex.lock();

  // Find the next available POSITIVE script ID
  std::size_t script_id = 1;
  for (; script_id < Script::scripts.size() && Script::scripts[script_id];
       ++script_id) {
    // Do nothing
  }

  Script *s = new Script(script_id, filename);
  if(script_id == Script::scripts.size()) {
    Script::scripts.emplace_back(s);
  } else {
    Script::scripts[script_id].reset(s);
  }
  
  Script::mutex.unlock();

  const std::string script_dir =
    std::string(SCRIPTS_PATH) + std::to_string(script_id);
  const std::string script_filename = script_dir + "/" + filename;

  // If the directory already exists, make it writable
  // Else, create it
  struct stat st;
  if (::stat(script_dir.c_str(), &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      cleanup(script_id, script_dir + " not a directory");
      return 1;
    }
    if (::chmod(script_dir.c_str(), S_IRWXU) != 0) {
      cleanup(script_id, script_dir);
      return 1;
    }
  } else {  
    if (::mkdir(script_dir.c_str(), S_IRWXU) != 0) {
      cleanup(script_id, script_dir);
      return 1;
    }
  }

  {
    // Create the script file
    std::ofstream out(script_filename.c_str(), std::ios::out | std::ios::trunc);
    if (!out) {
      // The script file may already exist and be read-only
      ::chmod(script_filename.c_str(), S_IWUSR);
      out.clear();
      out.open(script_filename.c_str(), std::ios::out | std::ios::trunc);
      if(!out) {
	cleanup(script_id, script_filename);
	return 1;
      }
    }
    
    out << script;
    if (!out) {
      cleanup(script_id, script_filename);
      return 1;
    }
  } // close file before chmod
  
  // Make the file read-only
  if (::chmod(script_filename.c_str(), S_IRUSR) != 0) {
    cleanup(script_id, script_filename);
    return 1;
  }
  
  // Make the directory read-only
  if(::chmod(script_dir.c_str(), S_IRUSR | S_IXUSR) != 0) {
    cleanup(script_id, script_dir);
    return 1;
  }
  
  reply(client, "HTTP/1.1 200 OK", std::to_string(script_id).c_str());
  return 0;
}

/**
 * Return the current script listing to the client.
 *
 * This method scans the global `scripts` table in increasing order of
 * script ID and builds a plain-text listing of all currently
 * registered scripts. Each non-null script entry contributes one
 * output line produced by `Script::format()`. Non-empty formatted
 * lines are appended to the response body, separated by newline
 * characters.
 *
 * If no scripts are currently registered, the response body is empty.
 * In both the empty and non-empty cases, the method replies with
 * HTTP status 200 OK.
 *
 * Concurrency note:
 * Iteration over the shared `scripts` table must be protected by the
 * surrounding lock so that the listing reflects a consistent snapshot.
 *
 * @return Always returns 0.
 */

int ScriptListTask::execute()
{
  std::string listing;

  {
    std::lock_guard<std::mutex> lock(Script::mutex);

    for (std::size_t i = 1; i < Script::scripts.size(); ++i) {
      Script* script = Script::scripts[i].get();
      if (!script) {
        continue;
      }

      std::string line = script->format();
      if (!line.empty()) {
        listing += line;
        listing += '\n';
      }
    }
  }

  reply(client, "HTTP/1.1 200 OK", listing.c_str());
  return 0;
}

/**
 * Delete a previously uploaded script.
 *
 * This method handles the `/scripts/<id>/delete` endpoint. It first
 * checks whether `script_id` designates an existing script entry in
 * the global `scripts` table. If the ID is out of range or the
 * corresponding slot is empty, the method replies with HTTP 404 Not
 * Found and returns 1.
 *
 * For an existing script, the method removes the in-memory metadata
 * entry from `scripts`.
 *
 * After releasing the lock, the method restores write/search
 * permissions as needed, deletes the script file, and removes the
 * script subdirectory. If any filesystem operation fails, the method
 * logs the error with `syslog`, replies with HTTP 500 Internal Server
 * Error, and returns 1.
 *
 * On success, the method replies with HTTP 200 OK and returns the
 * deleted script ID as the response body.
 *
 * Concurrency note: Access to the shared `scripts` table must be
 * protected by the surrounding lock so that existence checking and
 * removal of the in-memory entry are atomic with respect to other
 * tasks.
 *
 * @return 0 on success; 1 if the script does not exist or if deletion
 * fails.
 */

int DeleteTask::execute()
{
  std::unique_ptr<Script> removed_script;
  std::string script_dir;
  std::string script_filename;

  {
    std::lock_guard<std::mutex> lock(Script::mutex);

    if (script_id >= Script::scripts.size() || !Script::scripts[script_id]) {
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return 1;
    }

    Script* script = Script::scripts[script_id].get();

    if (script->n_jobs > 0) {
      reply(client, "HTTP/1.1 425 Too Early", "Script still has running jobs");
      return 1;
    }

    script_dir = std::string(SCRIPTS_PATH) + std::to_string(script_id);
    script_filename = script_dir + "/" + script->get_name();

    // Make the script unavailable immediately so no new jobs can start.
    removed_script = std::move(Script::scripts[script_id]);
  }
  
  if (::chmod(script_dir.c_str(), S_IRWXU) != 0 ||
      ::chmod(script_filename.c_str(), S_IWUSR) != 0 ||
      ::remove(script_filename.c_str()) != 0 ||
      ::rmdir(script_dir.c_str()) != 0) {
    syslog(LOG_ERR, "%s: %s", script_filename.c_str(), strerror(errno));
  }

  reply(client, "HTTP/1.1 200 OK", std::to_string(script_id).c_str());
  return 0;
}

// Main program
//    |
//    | creates
//    v
//  Job object -------------------- launches ------------------> Child process
//    |                                                          (runs script)
//    |
//    +--> stdout reader thread  <--------- stdout pipe ---------+
//    |
//    +--> stderr reader thread  <--------- stderr pipe ---------+
//    |
//    +--> timer thread ---------(timeout -> terminate child)----+
//    |
//    +--> monitor thread -------(waitpid -> final status)-------+

int RunTask::execute()
{
  Script* script = nullptr;
  std::string script_filename;

  {
    std::lock_guard<std::mutex> lock(Script::mutex);
    
    if (script_id >= Script::scripts.size() || !Script::scripts[script_id]) {
      reply(client, "HTTP/1.1 404 Not Found", "Not Found");
      return 1;
    }
    
    script = Script::scripts[script_id].get();
    script_filename =
      std::string(SCRIPTS_PATH) + std::to_string(script_id) + "/" +
      script->get_name();
    
    ++script->n_jobs;
  }
  
  auto launched = Job::launch(script, script_filename, args);
  JobLaunchResult launch_result = launched.first;
  Job* job = launched.second;
 
  switch (launch_result) {
  case JobLaunchResult::OK: {
    const std::string location =
      "Location: /jobs/" + std::to_string(job->get_pid());
    reply(client, "HTTP/1.1 303 See Other", location.c_str(), location.c_str());
    return 0;
  }
    
  case JobLaunchResult::TOO_MANY_JOBS:
    --script->n_jobs;
    reply(client, "HTTP/1.1 503 Service Unavailable", "Too many jobs");
    return 1;
    
  case JobLaunchResult::PIPE_ERROR:
  case JobLaunchResult::FORK_ERROR:
    --script->n_jobs;
    reply(client, "HTTP/1.1 500 Internal Server Error",
	  "Internal Server Error");
    return 1;
  }

  --script->n_jobs;
  reply(client, "HTTP/1.1 500 Internal Server Error", "Internal Server Error");
  return 1;
}

int JobStatusTask::execute()
{
  // 1. Lock Job::mutex.
  // 2. Search for job_id in Job::jobs.
  // 3. If not found, reply 404 Not Found and return.
  // 4. If found, save a pointer to the corresponding Job.
  // 5. Unlock Job::mutex.
  // 6. Call job->get_status() and convert the result to a string:
  //    RUNNING, FAILED, TERMINATED, TIMED_OUT, OUTPUT_LIMITED,
  //    or the numeric exit code if the status is FINISHED.
  // 7. Obtain the corresponding Script with job->get_script().
  // 8. Build the response body as "script_id,status".
  // 9. Reply with HTTP/1.1 200 OK and that body.
  
  reply(client, "HTTP/1.1 200 OK", "355,FAILED");
  return 0;

};

int JobListTask::execute()
{
  // 1. Atomically (using Job::mutex):
  // 2. Iterate over all entries in Job::jobs.
  // 3. For each entry:
  //    - obtain the job ID from the map key
  //    - obtain the Job object
  //    - obtain the corresponding Script with job->get_script()
  //    - append one line in the format
  //      "job_id,script_id,script_name\n"
  
  reply(client, "HTTP/1.1 200 OK", "12345,355,missing.py\n");
  return 0;
}

int TerminateTask::execute()
{
  // 1. Atomically (using Lock Job::mutex):
  // 2. Search for job_id in Job::jobs.
  // 3. If not found, reply 404 Not Found and return.
  // 4. Call job->terminate() to stop the job.
  
  reply(client, "HTTP/1.1 200 OK", "OK");
  return 0;
}

int StderrTask::execute()
{
  // 1. Atomically (using Job::mutex):
  // 2. Search for job_id in Job::jobs.
  // 3. If not found, reply 404 Not Found and return.
  // 4. Obtain the job's standard-error output by calling job->get_stderr().
  
  reply(client, "HTTP/1.1 200 OK", "Mary had a little lamb");
  return 0;
}

int StdoutTask::execute()
{
  // 1. Atomically (using Job::mutex):
  // 2. Search for job_id in Job::jobs.
  // 3. If not found, reply 404 Not Found and return.
  // 4. Obtain the job's standard output by calling job->get_stdout().

  reply(client, "HTTP/1.1 200 OK", "Its fleece was white as snow");
  return 0;
}

int PurgeJobTask::execute()
{
  // 1. Lock Job::mutex.
  // 2. Search for job_id in Job::jobs.
  // 3. If not found, reply 404 Not Found and return.
  // 4. If found:
  //    - save a pointer to the corresponding Script
  //    - move the Job object out of the map into a local std::unique_ptr<Job>
  //    - erase the map entry
  // 5. Unlock Job::mutex.
  // 6. If the removed job is still RUNNING, terminate it.
  // 8. Decrement script->n_jobs (the variable is "atomic")
      
  reply(client, "HTTP/1.1 200 OK", "OK");
  return 0;
}
