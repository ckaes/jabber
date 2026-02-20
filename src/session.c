#include "session.h"
#include "server.h"
#include "stanza.h"
#include "stream.h"
#include "presence.h"
#include "config.h"
#include "xml.h"
#include "log.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

session_t *session_create(int fd) {
    session_t *s = calloc(1, sizeof(session_t));
    if (!s)
        return NULL;

    s->fd = fd;
    s->state = STATE_CONNECTED;

    s->write_buf = malloc(WRITE_BUF_INIT);
    if (!s->write_buf) {
        free(s);
        return NULL;
    }
    s->write_cap = WRITE_BUF_INIT;
    s->write_len = 0;

    /* Create XML push parser for this session */
    xml_parser_create(s);

    return s;
}

void session_destroy(session_t *s) {
    if (!s)
        return;

    if (s->xml_ctx) {
        xmlFreeParserCtxt(s->xml_ctx);
        s->xml_ctx = NULL;
    }
    if (s->current_stanza) {
        xmlFreeNode(s->current_stanza);
        s->current_stanza = NULL;
    }
    if (s->presence_stanza) {
        xmlFreeNode(s->presence_stanza);
        s->presence_stanza = NULL;
    }

    free(s->write_buf);

    if (s->fd >= 0)
        close(s->fd);

    free(s);
}

void session_write(session_t *s, const char *data, size_t len) {
    if (!s || !data || len == 0)
        return;

    /* Grow buffer if needed */
    if (s->write_len + len > s->write_cap) {
        size_t new_cap = s->write_cap * 2;
        if (new_cap < s->write_len + len)
            new_cap = s->write_len + len;
        char *new_buf = realloc(s->write_buf, new_cap);
        if (!new_buf) {
            log_write(LOG_ERROR, "Failed to grow write buffer for fd %d", s->fd);
            session_teardown(s);
            return;
        }
        s->write_buf = new_buf;
        s->write_cap = new_cap;
    }

    memcpy(s->write_buf + s->write_len, data, len);
    s->write_len += len;

    log_xml_out(data, len);

    /* Set POLLOUT on this session's pollfd */
    struct pollfd *pfd = server_get_pollfd(s->poll_index);
    if (pfd)
        pfd->events |= POLLOUT;
}

void session_write_str(session_t *s, const char *str) {
    session_write(s, str, strlen(str));
}

int session_flush(session_t *s) {
    if (!s || s->write_len == 0)
        return 0;

    ssize_t n = write(s->fd, s->write_buf, s->write_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        log_write(LOG_ERROR, "Write error on fd %d: %s", s->fd, strerror(errno));
        return -1;
    }

    if ((size_t)n < s->write_len) {
        memmove(s->write_buf, s->write_buf + n, s->write_len - (size_t)n);
    }
    s->write_len -= (size_t)n;

    /* Clear POLLOUT if buffer drained */
    if (s->write_len == 0) {
        struct pollfd *pfd = server_get_pollfd(s->poll_index);
        if (pfd)
            pfd->events &= ~POLLOUT;
    }

    return 0;
}

session_t *session_find_by_jid(const char *bare_jid) {
    session_t **sessions = server_get_sessions();
    int nfds = server_get_nfds();

    for (int i = 1; i < nfds; i++) {
        session_t *s = sessions[i];
        if (!s || s->jid_local[0] == '\0')
            continue;

        char jid[512];
        jid_bare(s->jid_local, s->jid_domain, jid, sizeof(jid));
        if (strcmp(jid, bare_jid) == 0)
            return s;
    }
    return NULL;
}

void session_on_readable(session_t *s) {
    ssize_t n = read(s->fd, s->read_buf + s->read_len,
                     sizeof(s->read_buf) - s->read_len);
    if (n <= 0) {
        if (n < 0)
            log_write(LOG_WARN, "Read error on fd %d: %s", s->fd, strerror(errno));
        else
            log_write(LOG_INFO, "Client fd %d closed connection", s->fd);
        session_teardown(s);
        return;
    }

    log_xml_in(s->read_buf + s->read_len, (size_t)n);
    s->read_len += (size_t)n;

    /* Check for read buffer overflow */
    if (s->read_len >= sizeof(s->read_buf)) {
        log_write(LOG_WARN, "Read buffer overflow on fd %d", s->fd);
        session_teardown(s);
        return;
    }

    /* Feed to XML parser if initialized */
    if (s->xml_ctx) {
        xmlParseChunk(s->xml_ctx, s->read_buf, (int)s->read_len, 0);
        s->read_len = 0;
    }

    /* Handle deferred parser reset after SASL success.
     * The reset couldn't happen inside the SAX callback because
     * xmlParseChunk was still on the stack. Now it's safe. */
    if (s->parser_reset_pending) {
        s->parser_reset_pending = 0;
        xml_parser_reset(s);
    }
}

void session_on_writable(session_t *s) {
    if (session_flush(s) < 0) {
        session_teardown(s);
    }
}

/* --- Resource Binding (RFC 6120 §7) --- */

void session_handle_bind(session_t *s, xmlNodePtr stanza) {
    if (s->state != STATE_AUTHENTICATED && s->state != STATE_STREAM_OPENED) {
        stanza_send_error(s, stanza, "cancel", "not-allowed");
        return;
    }

    xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");

    /* Extract requested resource */
    xmlNodePtr bind_el = xml_find_child(stanza, "bind");
    xmlNodePtr res_el = bind_el ? xml_find_child(bind_el, "resource") : NULL;
    char resource[256];
    if (res_el) {
        xmlChar *content = xmlNodeGetContent(res_el);
        if (content) {
            snprintf(resource, sizeof(resource), "%s", (char *)content);
            xmlFree(content);
        } else {
            generate_id(resource, 8);
        }
    } else {
        generate_id(resource, 8);
    }

    /* Check for conflict — existing session with same bare JID */
    char bare[512];
    jid_bare(s->jid_local, s->jid_domain, bare, sizeof(bare));
    session_t *existing = session_find_by_jid(bare);
    if (existing && existing != s) {
        log_write(LOG_INFO, "Session conflict for %s — terminating old session fd %d",
                  bare, existing->fd);
        stream_send_error(existing, "conflict");
    }

    snprintf(s->jid_resource, sizeof(s->jid_resource), "%s", resource);
    s->state = STATE_BOUND;

    /* Build response */
    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource, full_jid, sizeof(full_jid));

    xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
    xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
    if (id) {
        xmlNewProp(result, (const xmlChar *)"id", id);
        xmlFree(id);
    }

    xmlNodePtr bind_resp = xmlNewChild(result, NULL, (const xmlChar *)"bind", NULL);
    xmlNsPtr bind_ns = xmlNewNs(bind_resp,
        (const xmlChar *)"urn:ietf:params:xml:ns:xmpp-bind", NULL);
    xmlSetNs(bind_resp, bind_ns);

    xmlNewChild(bind_resp, bind_ns, (const xmlChar *)"jid",
                (const xmlChar *)full_jid);

    stanza_send(s, result);
    xmlFreeNode(result);

    log_write(LOG_INFO, "Resource bound: %s", full_jid);
}

/* --- Session Establishment (RFC 3921, deprecated but Pidgin needs it) --- */

void session_handle_session_iq(session_t *s, xmlNodePtr stanza) {
    xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");

    s->state = STATE_SESSION_ACTIVE;

    xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
    xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
    if (id) {
        xmlNewProp(result, (const xmlChar *)"id", id);
        xmlFree(id);
    }

    stanza_send(s, result);
    xmlFreeNode(result);

    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource, full_jid, sizeof(full_jid));
    log_write(LOG_INFO, "Session established: %s", full_jid);
}

void session_teardown(session_t *s) {
    if (!s || s->state == STATE_DISCONNECTED)
        return;

    log_write(LOG_INFO, "Tearing down session for fd %d (user=%s)",
              s->fd, s->jid_local[0] ? s->jid_local : "(none)");

    /* Broadcast unavailable presence before destroying */
    if (s->available || s->initial_presence_sent)
        presence_broadcast_unavailable(s);

    s->state = STATE_DISCONNECTED;
    server_remove_session(s);
    session_destroy(s);
}
