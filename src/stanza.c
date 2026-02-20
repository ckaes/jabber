#include "stanza.h"
#include "session.h"
#include "stream.h"
#include "auth.h"
#include "roster.h"
#include "presence.h"
#include "message.h"
#include "disco.h"
#include "register.h"
#include "config.h"
#include "log.h"
#include "xml.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <libxml/tree.h>

/* Forward declarations for handlers implemented in other modules */
void session_handle_bind(session_t *s, xmlNodePtr stanza);
void session_handle_session_iq(session_t *s, xmlNodePtr stanza);

/* Forward declarations */
static void handle_iq(session_t *s, xmlNodePtr stanza);

static int is_server_jid(const char *to) {
    /* Check if the JID is addressed to the server (no localpart) */
    if (!to || !*to)
        return 1;
    if (strcmp(to, g_config.domain) == 0)
        return 1;
    return 0;
}

void stanza_route(session_t *s, xmlNodePtr stanza) {
    const char *name = (const char *)stanza->name;
    const char *ns = stanza->ns ? (const char *)stanza->ns->href : "";

    log_write(LOG_DEBUG, "Stanza received on fd %d: <%s> ns='%s' state=%d presence_stanza=%p",
              s->fd, name, ns, s->state, (void *)s->presence_stanza);

    /* Pre-auth: only SASL and in-band registration allowed */
    if (s->state == STATE_STREAM_OPENED && !s->authenticated) {
        if (strcmp(name, "auth") == 0 &&
            strcmp(ns, "urn:ietf:params:xml:ns:xmpp-sasl") == 0) {
            auth_handle_sasl(s, stanza);
        } else if (strcmp(name, "iq") == 0) {
            /* Allow registration IQs only */
            xmlNodePtr child = stanza->children;
            while (child && child->type != XML_ELEMENT_NODE)
                child = child->next;
            const char *cns = (child && child->ns && child->ns->href)
                              ? (const char *)child->ns->href : "";
            if (strcmp(cns, "jabber:iq:register") == 0)
                register_handle_iq(s, stanza);
            else
                stanza_send_error(s, stanza, "cancel", "not-allowed");
        } else {
            stream_send_error(s, "not-authorized");
        }
        return;
    }

    /* Post-auth: IQ for bind/session, then full routing once active */
    if (strcmp(name, "iq") == 0) {
        handle_iq(s, stanza);
    } else if (strcmp(name, "message") == 0) {
        if (s->state != STATE_SESSION_ACTIVE && s->state != STATE_BOUND) {
            stream_send_error(s, "not-authorized");
            return;
        }
        handle_message(s, stanza);
    } else if (strcmp(name, "presence") == 0) {
        if (s->state != STATE_SESSION_ACTIVE && s->state != STATE_BOUND) {
            stream_send_error(s, "not-authorized");
            return;
        }
        handle_presence(s, stanza);
    } else {
        stream_send_error(s, "unsupported-stanza-type");
    }
}

static void handle_iq(session_t *s, xmlNodePtr stanza) {
    xmlChar *type_attr = xmlGetProp(stanza, (const xmlChar *)"type");
    xmlChar *to_attr   = xmlGetProp(stanza, (const xmlChar *)"to");
    const char *type = type_attr ? (const char *)type_attr : "";
    const char *to   = to_attr ? (const char *)to_attr : "";

    /* result/error: route to target user if online, else drop */
    if (strcmp(type, "result") == 0 || strcmp(type, "error") == 0) {
        if (to[0] && !is_server_jid(to)) {
            /* Route to target user */
            char local[256], domain[256], resource[256];
            jid_parse(to, local, sizeof(local), domain, sizeof(domain),
                      resource, sizeof(resource));
            char bare[512];
            jid_bare(local, domain, bare, sizeof(bare));
            session_t *target = session_find_by_jid(bare);
            if (target) {
                /* Set from to sender's full JID */
                char from_jid[768];
                jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                         from_jid, sizeof(from_jid));
                xmlSetProp(stanza, (const xmlChar *)"from",
                           (const xmlChar *)from_jid);
                stanza_send(target, stanza);
            }
        }
        if (type_attr) xmlFree(type_attr);
        if (to_attr) xmlFree(to_attr);
        return;
    }

    /* get/set: identify child element namespace for dispatch */
    xmlNodePtr child = stanza->children;
    while (child && child->type != XML_ELEMENT_NODE)
        child = child->next;

    const char *child_ns = "";
    if (child && child->ns && child->ns->href)
        child_ns = (const char *)child->ns->href;

    if (strcmp(child_ns, "urn:ietf:params:xml:ns:xmpp-bind") == 0) {
        session_handle_bind(s, stanza);
    } else if (strcmp(child_ns, "urn:ietf:params:xml:ns:xmpp-session") == 0) {
        session_handle_session_iq(s, stanza);
    } else if (strcmp(child_ns, "jabber:iq:roster") == 0) {
        if (s->state != STATE_SESSION_ACTIVE && s->state != STATE_BOUND) {
            stanza_send_error(s, stanza, "cancel", "not-allowed");
        } else {
            roster_handle_iq(s, stanza);
        }
    } else if (strcmp(child_ns, "http://jabber.org/protocol/disco#info") == 0) {
        if (s->state != STATE_SESSION_ACTIVE && s->state != STATE_BOUND) {
            stanza_send_error(s, stanza, "cancel", "not-allowed");
        } else {
            disco_handle_info(s, stanza);
        }
    } else if (strcmp(child_ns, "http://jabber.org/protocol/disco#items") == 0) {
        if (s->state != STATE_SESSION_ACTIVE && s->state != STATE_BOUND) {
            stanza_send_error(s, stanza, "cancel", "not-allowed");
        } else {
            disco_handle_items(s, stanza);
        }
    } else if (strcmp(child_ns, "jabber:iq:register") == 0) {
        register_handle_iq(s, stanza);
    } else {
        /* Unknown namespace: if addressed to another user, route; else error */
        if (to[0] && !is_server_jid(to) &&
            (s->state == STATE_SESSION_ACTIVE || s->state == STATE_BOUND)) {
            char local[256], domain[256], resource[256];
            jid_parse(to, local, sizeof(local), domain, sizeof(domain),
                      resource, sizeof(resource));
            char bare[512];
            jid_bare(local, domain, bare, sizeof(bare));
            session_t *target = session_find_by_jid(bare);
            if (target) {
                char from_jid[768];
                jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                         from_jid, sizeof(from_jid));
                xmlSetProp(stanza, (const xmlChar *)"from",
                           (const xmlChar *)from_jid);
                stanza_send(target, stanza);
            } else {
                stanza_send_error(s, stanza, "cancel", "service-unavailable");
            }
        } else {
            stanza_send_error(s, stanza, "cancel", "service-unavailable");
        }
    }

    if (type_attr) xmlFree(type_attr);
    if (to_attr) xmlFree(to_attr);
}

/* --- Serialization utilities --- */

char *stanza_serialize(xmlNodePtr node, size_t *out_len) {
    xmlBufferPtr buf = xmlBufferCreate();
    if (!buf)
        return NULL;

    int len = xmlNodeDump(buf, NULL, node, 0, 0);
    if (len < 0) {
        xmlBufferFree(buf);
        return NULL;
    }

    char *result = malloc((size_t)len + 1);
    if (!result) {
        xmlBufferFree(buf);
        return NULL;
    }

    memcpy(result, xmlBufferContent(buf), (size_t)len);
    result[len] = '\0';
    if (out_len)
        *out_len = (size_t)len;

    xmlBufferFree(buf);
    return result;
}

void stanza_send(session_t *s, xmlNodePtr node) {
    size_t len;
    char *xml = stanza_serialize(node, &len);
    if (xml) {
        session_write(s, xml, len);
        free(xml);
    }
}

void stanza_send_error(session_t *s, xmlNodePtr original,
                       const char *error_type, const char *condition)
{
    const char *tag = (const char *)original->name;
    xmlChar *id = xmlGetProp(original, (const xmlChar *)"id");

    xmlNodePtr err = xmlNewNode(NULL, (const xmlChar *)tag);
    xmlNewProp(err, (const xmlChar *)"type", (const xmlChar *)"error");
    if (id) {
        xmlNewProp(err, (const xmlChar *)"id", id);
        xmlFree(id);
    }
    xmlNewProp(err, (const xmlChar *)"from", (const xmlChar *)g_config.domain);

    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource, full_jid, sizeof(full_jid));
    xmlNewProp(err, (const xmlChar *)"to", (const xmlChar *)full_jid);

    xmlNodePtr error_el = xmlNewChild(err, NULL, (const xmlChar *)"error", NULL);
    xmlNewProp(error_el, (const xmlChar *)"type", (const xmlChar *)error_type);

    xmlNodePtr cond = xmlNewChild(error_el, NULL, (const xmlChar *)condition, NULL);
    xmlNsPtr ns = xmlNewNs(cond,
        (const xmlChar *)"urn:ietf:params:xml:ns:xmpp-stanzas", NULL);
    xmlSetNs(cond, ns);

    stanza_send(s, err);
    xmlFreeNode(err);
}
