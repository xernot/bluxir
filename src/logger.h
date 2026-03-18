/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

/* Initialize logger — call once per log file. Returns opaque handle. */
typedef struct Logger Logger;
Logger *logger_create(const char *filepath, int max_bytes, int backup_count);

/* Destroy logger and close file */
void logger_destroy(Logger *log);

/* Log a message at the given level */
void logger_log(Logger *log, LogLevel level, const char *fmt, ...);

/* Convenience macros */
#define LOG_DEBUG(log, ...) logger_log(log, LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(log, ...) logger_log(log, LOG_INFO, __VA_ARGS__)
#define LOG_WARN(log, ...) logger_log(log, LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR(log, ...) logger_log(log, LOG_ERROR, __VA_ARGS__)

#endif /* LOGGER_H */
