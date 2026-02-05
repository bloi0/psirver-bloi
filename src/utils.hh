#pragma once

// Utility and initialization functions
void usage(const char* prog);
int16_t select_port(int argc, char **argv);
void on_sigint(int /*signum*/);
std::string init_pid_file();
void add_sigint_handler();

// Global variables
extern int server_socket;
extern std::string pid_path;
