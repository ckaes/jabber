#include "stream.h"
#include "session.h"
#include "config.h"
#include "log.h"
#include "util.h"
#include "xml.h"
#include <stdio.h>
#include <string.h>

void stream_handle_open(session_t *s, const char *to, const char *xmlns) {
    (void)xmlns;

    log_write(LOG_DEBUG, "Stream open from fd %d, to='%s'", s->fd, to);

    /* Validate domain */
    if (strcmp(to, g_config.domain) != 0) {
        log_write(LOG_WARN, "Host unknown: '%s' (expected '%s')", to, g_config.domain);
        stream_send_error(s, "host-unknown");
        return;
    }

    /* Generate stream ID */
    char stream_id[17];
    generate_id(stream_id, 16);

    /* Send stream response */
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "<?xml version='1.0'?>"
        "<stream:stream from='%s' id='%s' "
        "xmlns='jabber:client' "
        "xmlns:stream='http://etherx.jabber.org/streams' "
        "version='1.0'>",
        g_config.domain, stream_id);
    session_write_str(s, buf);

    /* Send features based on auth state */
    if (s->authenticated) {
        session_write_str(s,
            "<stream:features>"
            "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/>"
            "<session xmlns='urn:ietf:params:xml:ns:xmpp-session'>"
            "<optional/>"
            "</session>"
            "</stream:features>");
        s->state = STATE_STREAM_OPENED;
    } else {
        session_write_str(s,
            "<stream:features>"
            "<mechanisms xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<mechanism>PLAIN</mechanism>"
            "</mechanisms>"
            "<register xmlns='http://jabber.org/features/iq-register'/>"
            "</stream:features>");
        s->state = STATE_STREAM_OPENED;
    }
}

void stream_handle_close(session_t *s) {
    log_write(LOG_DEBUG, "Stream close from fd %d", s->fd);
    session_write_str(s, "</stream:stream>");
    session_teardown(s);
}

void stream_send_error(session_t *s, const char *condition) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "<stream:error>"
        "<%s xmlns='urn:ietf:params:xml:ns:xmpp-streams'/>"
        "</stream:error>"
        "</stream:stream>",
        condition);
    session_write_str(s, buf);
    session_flush(s);
    session_teardown(s);
}
