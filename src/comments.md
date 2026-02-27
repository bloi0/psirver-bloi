- Resolved: Line 66
  * Error output now uses "\n" and message consistently states "not set or is empty" for `PSIRVER_HOME`.
- Resolved: Lines 72, 78, 86
  * Error reporting in `create_pid_file()` now distinguishes `stat()` failure vs non-directory and includes accurate `strerror(errno)` context.
  * PID file write failures now differentiate `write()` error vs short write.
- Resolved: Line 106
  * Fatal setup failures now explicitly return `EXIT_FAILURE` after cleanup (signal registration and socket init paths).
- Resolved: Line 152
  * `fcntl(FD_CLOEXEC)` failure is logged as `LOG_WARNING` warning instead of an error.
- Resolved: Line 216
  * Shutdown checks remain intentional to support graceful stop behavior around accept/request processing.
- Resolved: Line 323
  * Port upper bound uses named constant `MAX_PORT` (no magic constant).
