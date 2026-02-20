#ifndef XMPPD_AUTH_H
#define XMPPD_AUTH_H

#include "session.h"
#include <libxml/tree.h>

void auth_handle_sasl(session_t *s, xmlNodePtr stanza);

#endif
