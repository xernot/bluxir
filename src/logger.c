/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>

struct Logger {
    FILE *fp;
    char filepath[512];
    int max_bytes;
    int backup_count;
    int current_size;
    pthread_mutex_t lock;
};

static const char *level_str(LogLevel level)
{
    switch (level) {
    case LOG_DEBUG:   return "DEBUG";
    case LOG_INFO:    return "INFO";
    case LOG_WARNING: return "WARNING";
    case LOG_ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

static int file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return (int)st.st_size;
    return 0;
}

static void rotate(Logger *log)
{
    if (log->current_size < log->max_bytes) return;
    fclose(log->fp);

    for (int i = log->backup_count; i >= 1; i--) {
        char old_name[520], new_name[520];
        if (i == 1) {
            snprintf(old_name, sizeof(old_name), "%s", log->filepath);
        } else {
            snprintf(old_name, sizeof(old_name), "%s.%d", log->filepath, i - 1);
        }
        snprintf(new_name, sizeof(new_name), "%s.%d", log->filepath, i);
        rename(old_name, new_name);
    }

    log->fp = fopen(log->filepath, "w");
    log->current_size = 0;
}

Logger *logger_create(const char *filepath, int max_bytes, int backup_count)
{
    Logger *log = calloc(1, sizeof(Logger));
    if (!log) return NULL;
    strncpy(log->filepath, filepath, sizeof(log->filepath) - 1);
    log->max_bytes = max_bytes;
    log->backup_count = backup_count;
    pthread_mutex_init(&log->lock, NULL);

    log->fp = fopen(filepath, "a");
    if (!log->fp) {
        free(log);
        return NULL;
    }
    log->current_size = file_size(filepath);
    return log;
}

void logger_destroy(Logger *log)
{
    if (!log) return;
    if (log->fp) fclose(log->fp);
    pthread_mutex_destroy(&log->lock);
    free(log);
}

void logger_log(Logger *log, LogLevel level, const char *fmt, ...)
{
    if (!log || !log->fp) return;

    pthread_mutex_lock(&log->lock);
    rotate(log);

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    va_list args;
    va_start(args, fmt);
    int n = fprintf(log->fp, "%s - %s - ", timebuf, level_str(level));
    n += vfprintf(log->fp, fmt, args);
    n += fprintf(log->fp, "\n");
    va_end(args);

    fflush(log->fp);
    log->current_size += n;
    pthread_mutex_unlock(&log->lock);
}
