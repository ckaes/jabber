#include "register.h"
#include "session.h"
#include "stanza.h"
#include "user.h"
#include "config.h"
#include "log.h"
#include "util.h"
#include <string.h>
#include <libxml/tree.h>

static void send_result_iq(session_t *s, const char *id, int include_to) {
    xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
    xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
    if (id && id[0])
        xmlNewProp(result, (const xmlChar *)"id", (const xmlChar *)id);
    xmlNewProp(result, (const xmlChar *)"from", (const xmlChar *)g_config.domain);
    if (include_to) {
        char full_jid[768];
        jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                 full_jid, sizeof(full_jid));
        xmlNewProp(result, (const xmlChar *)"to", (const xmlChar *)full_jid);
    }
    stanza_send(s, result);
    xmlFreeNode(result);
}

void register_handle_iq(session_t *s, xmlNodePtr stanza) {
    xmlChar *type_attr = xmlGetProp(stanza, (const xmlChar *)"type");
    xmlChar *id_attr   = xmlGetProp(stanza, (const xmlChar *)"id");
    const char *type = type_attr ? (const char *)type_attr : "";
    const char *id   = id_attr   ? (const char *)id_attr   : "";

    if (strcmp(type, "get") == 0) {
        /* Return the registration form */
        xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
        xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
        if (id[0])
            xmlNewProp(result, (const xmlChar *)"id", (const xmlChar *)id);
        xmlNewProp(result, (const xmlChar *)"from", (const xmlChar *)g_config.domain);
        if (s->authenticated) {
            char full_jid[768];
            jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                     full_jid, sizeof(full_jid));
            xmlNewProp(result, (const xmlChar *)"to", (const xmlChar *)full_jid);
        }

        xmlNodePtr query = xmlNewChild(result, NULL, (const xmlChar *)"query", NULL);
        xmlNsPtr ns = xmlNewNs(query, (const xmlChar *)"jabber:iq:register", NULL);
        xmlSetNs(query, ns);
        xmlNewChild(query, ns, (const xmlChar *)"instructions",
                    (const xmlChar *)"Choose a username and password.");
        xmlNewChild(query, ns, (const xmlChar *)"username", NULL);
        xmlNewChild(query, ns, (const xmlChar *)"password", NULL);

        stanza_send(s, result);
        xmlFreeNode(result);

    } else if (strcmp(type, "set") == 0) {
        /* Find the query child element */
        xmlNodePtr query = stanza->children;
        while (query && query->type != XML_ELEMENT_NODE)
            query = query->next;

        /* Check for <remove/> inside the query */
        int has_remove = 0;
        if (query) {
            for (xmlNodePtr n = query->children; n; n = n->next) {
                if (n->type == XML_ELEMENT_NODE &&
                    strcmp((const char *)n->name, "remove") == 0) {
                    has_remove = 1;
                    break;
                }
            }
        }

        if (has_remove) {
            /* Account removal — must be authenticated */
            if (!s->authenticated) {
                stanza_send_error(s, stanza, "cancel", "not-allowed");
            } else {
                send_result_iq(s, id, 1);
                char username[256];
                snprintf(username, sizeof(username), "%s", s->jid_local);
                user_delete(username);
                session_teardown(s);
            }
        } else {
            /* Extract username and password from the query children */
            xmlChar *uname = NULL, *pw = NULL;
            if (query) {
                for (xmlNodePtr n = query->children; n; n = n->next) {
                    if (n->type != XML_ELEMENT_NODE)
                        continue;
                    if (strcmp((const char *)n->name, "username") == 0)
                        uname = xmlNodeGetContent(n);
                    else if (strcmp((const char *)n->name, "password") == 0)
                        pw = xmlNodeGetContent(n);
                }
            }

            if (!uname || xmlStrlen(uname) == 0 ||
                !pw   || xmlStrlen(pw)   == 0) {
                stanza_send_error(s, stanza, "modify", "bad-request");
            } else if (!s->authenticated) {
                /* Pre-auth: create new account */
                int rc = user_create((const char *)uname, (const char *)pw);
                if (rc == 0) {
                    log_write(LOG_INFO, "New account registered: '%s'",
                              (const char *)uname);
                    send_result_iq(s, id, 0);
                } else if (rc == -1) {
                    stanza_send_error(s, stanza, "cancel", "conflict");
                } else if (rc == -2) {
                    stanza_send_error(s, stanza, "modify", "not-acceptable");
                } else {
                    stanza_send_error(s, stanza, "wait", "internal-server-error");
                }
            } else {
                /* Post-auth: password change — username must match */
                if (strcmp((const char *)uname, s->jid_local) != 0) {
                    stanza_send_error(s, stanza, "cancel", "not-allowed");
                } else {
                    int rc = user_change_password((const char *)uname,
                                                  (const char *)pw);
                    if (rc == 0) {
                        log_write(LOG_INFO, "Password changed for user '%s'",
                                  (const char *)uname);
                        send_result_iq(s, id, 1);
                    } else {
                        stanza_send_error(s, stanza, "wait",
                                         "internal-server-error");
                    }
                }
            }

            if (uname) xmlFree(uname);
            if (pw)    xmlFree(pw);
        }
    } else {
        stanza_send_error(s, stanza, "cancel", "bad-request");
    }

    if (type_attr) xmlFree(type_attr);
    if (id_attr)   xmlFree(id_attr);
}
