#ifndef XMPPD_STREAM_H
#define XMPPD_STREAM_H

#include "session.h"

void stream_handle_open(session_t *s, const char *to, const char *xmlns);
void stream_handle_close(session_t *s);
void stream_send_error(session_t *s, const char *condition);

#endif
