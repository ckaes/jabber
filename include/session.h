#ifndef XMPPD_SESSION_H
#define XMPPD_SESSION_H

#include <stddef.h>
#include <libxml/tree.h>
#include <libxml/parser.h>

#define READ_BUF_SIZE  8192
#define WRITE_BUF_INIT 8192
#define MAX_ROSTER_ITEMS 128

enum session_state {
    STATE_CONNECTED = 0,
    STATE_STREAM_OPENED,
    STATE_AUTHENTICATED,
    STATE_BOUND,
    STATE_SESSION_ACTIVE,
    STATE_DISCONNECTED
};

typedef struct roster_item {
    char jid[512];
    char name[256];
    char subscription[16];
    int  ask_subscribe;
} roster_item_t;

typedef struct roster {
    roster_item_t items[MAX_ROSTER_ITEMS];
    int count;
    int loaded;
} roster_t;

typedef struct session {
    int fd;
    int state;
    int poll_index;             /* index into pollfds array */

    /* TCP buffers */
    char   read_buf[READ_BUF_SIZE];
    size_t read_len;
    char  *write_buf;
    size_t write_len;
    size_t write_cap;

    /* JID */
    char jid_local[256];
    char jid_domain[256];
    char jid_resource[256];

    /* XML parser */
    xmlParserCtxtPtr xml_ctx;
    xmlNodePtr       current_stanza;
    xmlNodePtr       current_node;
    int              stanza_depth;

    /* Auth state */
    int authenticated;
    int parser_reset_pending;   /* set by auth to defer parser reset */

    /* Presence */
    int        available;
    int        initial_presence_sent;
    xmlNodePtr presence_stanza;

    /* Roster cache */
    roster_t roster;
} session_t;

session_t *session_create(int fd);
void       session_destroy(session_t *s);
void       session_write(session_t *s, const char *data, size_t len);
void       session_write_str(session_t *s, const char *str);
int        session_flush(session_t *s);
session_t *session_find_by_jid(const char *bare_jid);

/* Called by server event loop */
void session_on_readable(session_t *s);
void session_on_writable(session_t *s);

/* Mark a session for teardown (called from various modules) */
void session_teardown(session_t *s);

#endif
