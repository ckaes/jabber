#include "auth.h"
#include "session.h"
#include "config.h"
#include "user.h"
#include "xml.h"
#include "log.h"
#include "util.h"
#include <string.h>
#include <libxml/tree.h>

void auth_handle_sasl(session_t *s, xmlNodePtr stanza) {
    /* Check mechanism attribute */
    xmlChar *mechanism = xmlGetProp(stanza, (const xmlChar *)"mechanism");
    if (!mechanism || xmlStrcmp(mechanism, (const xmlChar *)"PLAIN") != 0) {
        log_write(LOG_WARN, "Unsupported SASL mechanism from fd %d: %s",
                  s->fd, mechanism ? (char *)mechanism : "(none)");
        if (mechanism) xmlFree(mechanism);
        session_write_str(s,
            "<failure xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<invalid-mechanism/>"
            "</failure>");
        return;
    }
    xmlFree(mechanism);

    /* Get base64-encoded content */
    xmlChar *b64_content = xmlNodeGetContent(stanza);
    if (!b64_content || xmlStrlen(b64_content) == 0) {
        log_write(LOG_WARN, "Empty SASL PLAIN payload from fd %d", s->fd);
        if (b64_content) xmlFree(b64_content);
        session_write_str(s,
            "<failure xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<not-authorized/>"
            "</failure>");
        return;
    }

    /* Base64 decode */
    unsigned char decoded[4096];
    size_t decoded_len = 0;
    int rc = base64_decode((const char *)b64_content, xmlStrlen(b64_content),
                           decoded, &decoded_len);
    xmlFree(b64_content);

    if (rc < 0 || decoded_len < 3) {
        log_write(LOG_WARN, "Invalid base64 in SASL PLAIN from fd %d", s->fd);
        session_write_str(s,
            "<failure xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<not-authorized/>"
            "</failure>");
        return;
    }
    decoded[decoded_len] = '\0'; /* ensure null-terminated for strcmp */

    /*
     * SASL PLAIN format: \0authcid\0passwd
     * The first byte should be \0 (empty authzid).
     * Then authcid runs until the next \0.
     * Then passwd runs to the end.
     */
    const char *authcid = NULL;
    const char *passwd = NULL;

    /* Find authcid: starts after first \0 */
    size_t i = 0;
    if (decoded[i] != '\0') {
        /* authzid is present — skip to next \0 */
        while (i < decoded_len && decoded[i] != '\0') i++;
    }
    i++; /* skip the \0 */
    if (i >= decoded_len) {
        log_write(LOG_WARN, "Malformed SASL PLAIN payload from fd %d", s->fd);
        session_write_str(s,
            "<failure xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<not-authorized/>"
            "</failure>");
        return;
    }
    authcid = (const char *)&decoded[i];

    /* Find passwd: starts after second \0 */
    while (i < decoded_len && decoded[i] != '\0') i++;
    i++; /* skip the \0 */
    if (i >= decoded_len) {
        log_write(LOG_WARN, "Malformed SASL PLAIN payload from fd %d (no password)", s->fd);
        session_write_str(s,
            "<failure xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<not-authorized/>"
            "</failure>");
        return;
    }
    passwd = (const char *)&decoded[i];

    log_write(LOG_DEBUG, "SASL PLAIN auth attempt: user='%s' fd=%d", authcid, s->fd);

    /* Validate credentials */
    if (!user_check_password(authcid, passwd)) {
        log_write(LOG_INFO, "Authentication failed for user '%s' from fd %d",
                  authcid, s->fd);
        session_write_str(s,
            "<failure xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>"
            "<not-authorized/>"
            "</failure>");
        return;
    }

    /* Success */
    log_write(LOG_INFO, "User '%s' authenticated on fd %d", authcid, s->fd);

    snprintf(s->jid_local, sizeof(s->jid_local), "%s", authcid);
    snprintf(s->jid_domain, sizeof(s->jid_domain), "%s", g_config.domain);
    s->authenticated = 1;
    s->state = STATE_AUTHENTICATED;

    session_write_str(s,
        "<success xmlns='urn:ietf:params:xml:ns:xmpp-sasl'/>");

    /* Defer parser reset — we're inside an xmlParseChunk callback,
     * so we can't destroy the parser context from under it.
     * session_on_readable will handle the reset after xmlParseChunk returns. */
    s->parser_reset_pending = 1;
}
