#ifndef XMPPD_MESSAGE_H
#define XMPPD_MESSAGE_H

#include "session.h"
#include <libxml/tree.h>

void handle_message(session_t *s, xmlNodePtr stanza);
void message_store_offline(const char *username, xmlNodePtr stanza);
void message_deliver_offline(session_t *s);

#endif
