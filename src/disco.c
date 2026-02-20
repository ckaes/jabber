#include "disco.h"
#include "stanza.h"
#include "config.h"
#include "util.h"
#include <libxml/tree.h>

void disco_handle_info(session_t *s, xmlNodePtr stanza) {
    xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");

    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource,
             full_jid, sizeof(full_jid));

    xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
    xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
    xmlNewProp(result, (const xmlChar *)"from", (const xmlChar *)g_config.domain);
    xmlNewProp(result, (const xmlChar *)"to", (const xmlChar *)full_jid);
    if (id) {
        xmlNewProp(result, (const xmlChar *)"id", id);
        xmlFree(id);
    }

    xmlNodePtr query = xmlNewChild(result, NULL, (const xmlChar *)"query", NULL);
    xmlNsPtr ns = xmlNewNs(query,
        (const xmlChar *)"http://jabber.org/protocol/disco#info", NULL);
    xmlSetNs(query, ns);

    /* Identity */
    xmlNodePtr identity = xmlNewChild(query, ns, (const xmlChar *)"identity", NULL);
    xmlNewProp(identity, (const xmlChar *)"category", (const xmlChar *)"server");
    xmlNewProp(identity, (const xmlChar *)"type", (const xmlChar *)"im");
    xmlNewProp(identity, (const xmlChar *)"name", (const xmlChar *)"xmppd");

    /* Features */
    const char *features[] = {
        "http://jabber.org/protocol/disco#info",
        "http://jabber.org/protocol/disco#items",
        "jabber:iq:roster",
        "urn:xmpp:delay",
        NULL
    };

    for (int i = 0; features[i]; i++) {
        xmlNodePtr feat = xmlNewChild(query, ns, (const xmlChar *)"feature", NULL);
        xmlNewProp(feat, (const xmlChar *)"var", (const xmlChar *)features[i]);
    }

    stanza_send(s, result);
    xmlFreeNode(result);
}

void disco_handle_items(session_t *s, xmlNodePtr stanza) {
    xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");

    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource,
             full_jid, sizeof(full_jid));

    xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
    xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
    xmlNewProp(result, (const xmlChar *)"from", (const xmlChar *)g_config.domain);
    xmlNewProp(result, (const xmlChar *)"to", (const xmlChar *)full_jid);
    if (id) {
        xmlNewProp(result, (const xmlChar *)"id", id);
        xmlFree(id);
    }

    xmlNodePtr query = xmlNewChild(result, NULL, (const xmlChar *)"query", NULL);
    xmlNsPtr ns = xmlNewNs(query,
        (const xmlChar *)"http://jabber.org/protocol/disco#items", NULL);
    xmlSetNs(query, ns);

    /* Empty items list */

    stanza_send(s, result);
    xmlFreeNode(result);
}
