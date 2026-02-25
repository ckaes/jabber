#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE *log_fp = NULL;
static int   log_level = LOG_INFO;

static const char *level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

static void log_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm);
}

int log_init(const char *path, int level) {
    log_level = level;
    log_fp = fopen(path, "a");
    if (!log_fp) {
        fprintf(stderr, "Failed to open log file: %s\n", path);
        return -1;
    }
    return 0;
}

void log_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

void log_write(int level, const char *fmt, ...) {
    if (level < log_level || !log_fp)
        return;

    char ts[32];
    log_timestamp(ts, sizeof(ts));

    fprintf(log_fp, "[%s] [%s] ", ts, level_names[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    va_end(ap);

    fprintf(log_fp, "\n");
    fflush(log_fp);
}

void log_xml_in(const char *data, size_t len) {
    if (LOG_DEBUG < log_level || !log_fp)
        return;

    char ts[32];
    log_timestamp(ts, sizeof(ts));
    fprintf(log_fp, "[%s] [DEBUG] <-- %.*s\n", ts, (int)len, data);
    fflush(log_fp);
}

void log_xml_out(const char *data, size_t len) {
    if (LOG_DEBUG < log_level || !log_fp)
        return;

    char ts[32];
    log_timestamp(ts, sizeof(ts));
    fprintf(log_fp, "[%s] [DEBUG] --> %.*s\n", ts, (int)len, data);
    fflush(log_fp);
}
