#pragma once
#include <cstdint>

// Utility and initialization functions
uint16_t select_port(int argc, char **argv);
void graceful_shutdown(int /*signum*/);
std::string init_pid_file();
void add_sigint_handler();

