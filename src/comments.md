- Line 66: `std::cerr << "Error: PSIRVER_HOME is not set." << std::endl;`
  * Do not use `std::ends`; '\n' or "\n" are more efficient
  * According to your logic, PSIRVER_HOME is either not set or empty; be consistent
- Lines 72, 78, 86:
  * Use `perror` or `strettor(errno)` for accurate error reporting
- Line 106: Call to `exit()` missing!
- Line 152: `log_error("fcntl(FD_CLOEXEC) failed");`
  * Not an error, just a warning/notice.
- Line 216:  `if (shutdown_requested) {`
  * This is yet to be decided.  
- Line 323: `65535`
  * Do not use "magic" (unexplained) constants
