#include "logging.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
void start_log(void) { openlog("sysu-auth", LOG_PID, LOG_DAEMON); }
void close_log(void) { closelog(); }
void print_log(const char *level, const char *func, const char *format, ...) {
    if (level[0] == 'D' && !get_program_config()->debug) return;
    va_list args;
    va_start(args, format);
    vsyslog(level[0] == 'E' ? LOG_ERR : level[0] == 'W' ? LOG_WARNING :
        level[0] == 'D' ? LOG_DEBUG : LOG_INFO, format, args);
    va_end(args);
}
void print_log_raw(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}
