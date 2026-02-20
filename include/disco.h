#ifndef XMPPD_DISCO_H
#define XMPPD_DISCO_H

#include "session.h"
#include <libxml/tree.h>

void disco_handle_info(session_t *s, xmlNodePtr stanza);
void disco_handle_items(session_t *s, xmlNodePtr stanza);

#endif
