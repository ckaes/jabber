#ifndef XMPPD_LOG_H
#define XMPPD_LOG_H

#include <stddef.h>

enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
};

int  log_init(const char *path, int level);
void log_close(void);
void log_write(int level, const char *fmt, ...);
void log_xml_in(const char *data, size_t len);
void log_xml_out(const char *data, size_t len);

#endif
