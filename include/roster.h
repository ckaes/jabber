#ifndef XMPPD_ROSTER_H
#define XMPPD_ROSTER_H

#include "session.h"
#include <libxml/tree.h>

/* Load roster from disk into session's roster cache */
int roster_load(session_t *s);

/* Save session's roster cache to disk */
int roster_save(session_t *s);

/* Load/save roster for a user who may or may not be online */
int roster_load_for_user(const char *username, roster_t *r);
int roster_save_for_user(const char *username, roster_t *r);

/* Find an item in a roster by JID */
roster_item_t *roster_find_item(roster_t *r, const char *jid);

/* Add or update an item in a roster */
int roster_add_item(roster_t *r, const char *jid, const char *name,
                    const char *subscription, int ask_subscribe);

/* Remove an item from a roster */
int roster_remove_item(roster_t *r, const char *jid);

/* Handle roster IQ stanzas (get/set) */
void roster_handle_iq(session_t *s, xmlNodePtr stanza);

/* Send a roster push for a single item to a session */
void roster_push(session_t *s, roster_item_t *item);

#endif
