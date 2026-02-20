#ifndef XMPPD_REGISTER_H
#define XMPPD_REGISTER_H
#include "session.h"
#include <libxml/tree.h>
void register_handle_iq(session_t *s, xmlNodePtr stanza);
#endif
