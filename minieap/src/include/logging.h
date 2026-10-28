#ifndef SYSU_LOGGING_H
#define SYSU_LOGGING_H
#include <string.h>
#include <errno.h>
#define PR_ERRNO(desc) PR_ERR(desc ": %s (%d)", strerror(errno), errno)
#define PR_ERR(...) print_log("E", "", __VA_ARGS__)
#define PR_WARN(...) print_log("W", "", __VA_ARGS__)
#define PR_INFO(...) print_log("I", "", __VA_ARGS__)
#define PR_DBG(...) do {} while (0)
#define PR_RAW(...) print_log_raw(__VA_ARGS__)
void print_log(const char *level, const char *func, const char *format, ...) __attribute__((format(printf, 3, 4)));
void print_log_raw(const char *format, ...) __attribute__((format(printf, 1, 2)));
void start_log(void);
void close_log(void);
#endif
