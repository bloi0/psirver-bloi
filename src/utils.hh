#pragma once
#include <mutex>
#include <thread>

// Utility and initialization functions
void usage(const char* prog);
int16_t select_port(int argc, char **argv);
void graceful_shutdown(int /*signum*/);
std::string init_pid_file();
void add_sigint_handler();

// Global variables
extern int server_socket;
extern std::string pid_path;

static constexpr char HOME_VAR[] = "PSIRVER_HOME";
static constexpr char SCRIPTS_PATH[] = "scripts/";
static constexpr char JOBS_PATH[] = "jobs/";

class Script {
private:
  std::size_t script_id;
  std::string name;
  std::size_t jobs;
  std::mutex jobs_mutex;
public:
  Script(std::size_t script_id, std::string& f) :
    script_id(script_id), name(f) {
    jobs = 0;
  }
  const std::string& get_name() const { return name; }
  const std::string format() const;  
  static void terminate_all();
};
