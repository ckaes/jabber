#ifndef XMPPD_SERVER_H
#define XMPPD_SERVER_H

#include "config.h"
#include "session.h"
#include <poll.h>

#define MAX_CLIENTS 16   /* 1 listener + 15 client slots */

int  server_init(config_t *cfg);
void server_run(void);
void server_shutdown(void);

/* Access to poll/session arrays (needed by session module) */
struct pollfd *server_get_pollfd(int index);
session_t    **server_get_sessions(void);
int            server_get_nfds(void);

/* Remove a session from the poll set (called during teardown) */
void server_remove_session(session_t *s);

#endif
