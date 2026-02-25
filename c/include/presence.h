#ifndef XMPPD_PRESENCE_H
#define XMPPD_PRESENCE_H

#include "session.h"
#include <libxml/tree.h>

/* Main presence dispatcher */
void handle_presence(session_t *s, xmlNodePtr stanza);

/* Broadcast unavailable on disconnect */
void presence_broadcast_unavailable(session_t *s);

/* Re-deliver pending subscribe requests to a newly-online user */
void presence_redeliver_pending_subscribes(session_t *s);

#endif
