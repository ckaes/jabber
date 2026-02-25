#ifndef XMPPD_STANZA_H
#define XMPPD_STANZA_H

#include "session.h"
#include <libxml/tree.h>

/* Route a complete stanza to the appropriate handler */
void stanza_route(session_t *s, xmlNodePtr stanza);

/* Serialize an xmlNode to a malloc'd string */
char *stanza_serialize(xmlNodePtr node, size_t *out_len);

/* Serialize and send a stanza via session_write */
void stanza_send(session_t *s, xmlNodePtr node);

/* Build and send a stanza-level error response */
void stanza_send_error(session_t *s, xmlNodePtr original,
                       const char *error_type, const char *condition);

#endif
