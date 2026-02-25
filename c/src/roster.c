#include "roster.h"
#include "stanza.h"
#include "config.h"
#include "log.h"
#include "util.h"
#include "xml.h"
#include <string.h>
#include <stdio.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

static int roster_load_from_path(const char *path, roster_t *r) {
    r->count = 0;

    xmlDocPtr doc = xmlReadFile(path, NULL, 0);
    if (!doc) {
        log_write(LOG_DEBUG, "No roster file or parse error: %s", path);
        r->loaded = 1;
        return 0;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, (const xmlChar *)"roster") != 0) {
        xmlFreeDoc(doc);
        r->loaded = 1;
        return 0;
    }

    for (xmlNodePtr item = root->children; item; item = item->next) {
        if (item->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(item->name, (const xmlChar *)"item") != 0)
            continue;
        if (r->count >= MAX_ROSTER_ITEMS)
            break;

        roster_item_t *ri = &r->items[r->count];
        memset(ri, 0, sizeof(*ri));

        xmlChar *jid = xmlGetProp(item, (const xmlChar *)"jid");
        xmlChar *name = xmlGetProp(item, (const xmlChar *)"name");
        xmlChar *sub = xmlGetProp(item, (const xmlChar *)"subscription");
        xmlChar *ask = xmlGetProp(item, (const xmlChar *)"ask");

        if (jid) {
            snprintf(ri->jid, sizeof(ri->jid), "%s", (char *)jid);
            xmlFree(jid);
        }
        if (name) {
            snprintf(ri->name, sizeof(ri->name), "%s", (char *)name);
            xmlFree(name);
        }
        if (sub) {
            snprintf(ri->subscription, sizeof(ri->subscription), "%s", (char *)sub);
            xmlFree(sub);
        } else {
            snprintf(ri->subscription, sizeof(ri->subscription), "none");
        }
        if (ask && xmlStrcmp(ask, (const xmlChar *)"subscribe") == 0) {
            ri->ask_subscribe = 1;
        }
        if (ask) xmlFree(ask);

        r->count++;
    }

    xmlFreeDoc(doc);
    r->loaded = 1;
    return 0;
}

static int roster_save_to_path(const char *path, roster_t *r) {
    xmlDocPtr doc = xmlNewDoc((const xmlChar *)"1.0");
    xmlNodePtr root = xmlNewNode(NULL, (const xmlChar *)"roster");
    xmlDocSetRootElement(doc, root);

    for (int i = 0; i < r->count; i++) {
        roster_item_t *ri = &r->items[i];
        xmlNodePtr item = xmlNewChild(root, NULL, (const xmlChar *)"item", NULL);
        xmlNewProp(item, (const xmlChar *)"jid", (const xmlChar *)ri->jid);
        if (ri->name[0])
            xmlNewProp(item, (const xmlChar *)"name", (const xmlChar *)ri->name);
        xmlNewProp(item, (const xmlChar *)"subscription",
                   (const xmlChar *)ri->subscription);
        if (ri->ask_subscribe)
            xmlNewProp(item, (const xmlChar *)"ask", (const xmlChar *)"subscribe");
    }

    int rc = xmlSaveFormatFile(path, doc, 1);
    xmlFreeDoc(doc);

    if (rc < 0) {
        log_write(LOG_ERROR, "Failed to save roster: %s", path);
        return -1;
    }
    return 0;
}

/* --- Public API --- */

int roster_load(session_t *s) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/roster.xml",
             g_config.datadir, s->jid_local);
    return roster_load_from_path(path, &s->roster);
}

int roster_save(session_t *s) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/roster.xml",
             g_config.datadir, s->jid_local);
    return roster_save_to_path(path, &s->roster);
}

int roster_load_for_user(const char *username, roster_t *r) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/roster.xml",
             g_config.datadir, username);
    return roster_load_from_path(path, r);
}

int roster_save_for_user(const char *username, roster_t *r) {
    char path[1280];
    snprintf(path, sizeof(path), "%s/%s/roster.xml",
             g_config.datadir, username);
    return roster_save_to_path(path, r);
}

roster_item_t *roster_find_item(roster_t *r, const char *jid) {
    for (int i = 0; i < r->count; i++) {
        if (strcmp(r->items[i].jid, jid) == 0)
            return &r->items[i];
    }
    return NULL;
}

int roster_add_item(roster_t *r, const char *jid, const char *name,
                    const char *subscription, int ask_subscribe)
{
    roster_item_t *existing = roster_find_item(r, jid);
    if (existing) {
        if (name)
            snprintf(existing->name, sizeof(existing->name), "%s", name);
        if (subscription)
            snprintf(existing->subscription, sizeof(existing->subscription),
                     "%s", subscription);
        existing->ask_subscribe = ask_subscribe;
        return 0;
    }

    if (r->count >= MAX_ROSTER_ITEMS)
        return -1;

    roster_item_t *ri = &r->items[r->count];
    memset(ri, 0, sizeof(*ri));
    snprintf(ri->jid, sizeof(ri->jid), "%s", jid);
    if (name)
        snprintf(ri->name, sizeof(ri->name), "%s", name);
    snprintf(ri->subscription, sizeof(ri->subscription),
             "%s", subscription ? subscription : "none");
    ri->ask_subscribe = ask_subscribe;
    r->count++;
    return 0;
}

int roster_remove_item(roster_t *r, const char *jid) {
    for (int i = 0; i < r->count; i++) {
        if (strcmp(r->items[i].jid, jid) == 0) {
            /* Shift remaining items down */
            memmove(&r->items[i], &r->items[i + 1],
                    (size_t)(r->count - i - 1) * sizeof(roster_item_t));
            r->count--;
            return 0;
        }
    }
    return -1;
}

void roster_push(session_t *s, roster_item_t *item) {
    char push_id[20];
    generate_id(push_id, 8);

    xmlNodePtr iq = xmlNewNode(NULL, (const xmlChar *)"iq");
    xmlNewProp(iq, (const xmlChar *)"type", (const xmlChar *)"set");
    xmlNewProp(iq, (const xmlChar *)"id", (const xmlChar *)push_id);

    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource,
             full_jid, sizeof(full_jid));
    xmlNewProp(iq, (const xmlChar *)"to", (const xmlChar *)full_jid);

    xmlNodePtr query = xmlNewChild(iq, NULL, (const xmlChar *)"query", NULL);
    xmlNsPtr ns = xmlNewNs(query, (const xmlChar *)"jabber:iq:roster", NULL);
    xmlSetNs(query, ns);

    xmlNodePtr item_el = xmlNewChild(query, ns, (const xmlChar *)"item", NULL);
    xmlNewProp(item_el, (const xmlChar *)"jid", (const xmlChar *)item->jid);
    if (item->name[0])
        xmlNewProp(item_el, (const xmlChar *)"name", (const xmlChar *)item->name);
    xmlNewProp(item_el, (const xmlChar *)"subscription",
               (const xmlChar *)item->subscription);
    if (item->ask_subscribe)
        xmlNewProp(item_el, (const xmlChar *)"ask", (const xmlChar *)"subscribe");

    stanza_send(s, iq);
    xmlFreeNode(iq);
}

void roster_handle_iq(session_t *s, xmlNodePtr stanza) {
    xmlChar *type_attr = xmlGetProp(stanza, (const xmlChar *)"type");
    const char *type = type_attr ? (const char *)type_attr : "";

    /* Ensure roster is loaded */
    if (!s->roster.loaded)
        roster_load(s);

    if (strcmp(type, "get") == 0) {
        /* Return full roster */
        xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");

        xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
        xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
        if (id) {
            xmlNewProp(result, (const xmlChar *)"id", id);
            xmlFree(id);
        }

        char full_jid[768];
        jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                 full_jid, sizeof(full_jid));
        xmlNewProp(result, (const xmlChar *)"to", (const xmlChar *)full_jid);

        xmlNodePtr query = xmlNewChild(result, NULL, (const xmlChar *)"query", NULL);
        xmlNsPtr ns = xmlNewNs(query, (const xmlChar *)"jabber:iq:roster", NULL);
        xmlSetNs(query, ns);

        for (int i = 0; i < s->roster.count; i++) {
            roster_item_t *ri = &s->roster.items[i];
            xmlNodePtr item = xmlNewChild(query, ns, (const xmlChar *)"item", NULL);
            xmlNewProp(item, (const xmlChar *)"jid", (const xmlChar *)ri->jid);
            if (ri->name[0])
                xmlNewProp(item, (const xmlChar *)"name", (const xmlChar *)ri->name);
            xmlNewProp(item, (const xmlChar *)"subscription",
                       (const xmlChar *)ri->subscription);
            if (ri->ask_subscribe)
                xmlNewProp(item, (const xmlChar *)"ask",
                           (const xmlChar *)"subscribe");
        }

        stanza_send(s, result);
        xmlFreeNode(result);
    } else if (strcmp(type, "set") == 0) {
        /* Add/update or remove a contact */
        xmlNodePtr query_el = xml_find_child(stanza, "query");
        xmlNodePtr item_el = query_el ? xml_find_child(query_el, "item") : NULL;
        if (!item_el) {
            stanza_send_error(s, stanza, "modify", "bad-request");
            if (type_attr) xmlFree(type_attr);
            return;
        }

        xmlChar *jid_attr = xmlGetProp(item_el, (const xmlChar *)"jid");
        xmlChar *name_attr = xmlGetProp(item_el, (const xmlChar *)"name");
        xmlChar *sub_attr = xmlGetProp(item_el, (const xmlChar *)"subscription");

        if (!jid_attr) {
            stanza_send_error(s, stanza, "modify", "bad-request");
            if (name_attr) xmlFree(name_attr);
            if (sub_attr) xmlFree(sub_attr);
            if (type_attr) xmlFree(type_attr);
            return;
        }

        const char *jid = (const char *)jid_attr;
        const char *name = name_attr ? (const char *)name_attr : NULL;

        if (sub_attr && xmlStrcmp(sub_attr, (const xmlChar *)"remove") == 0) {
            /* Remove item */
            roster_remove_item(&s->roster, jid);
            roster_save(s);

            /* Send result */
            xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");
            xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
            xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
            if (id) {
                xmlNewProp(result, (const xmlChar *)"id", id);
                xmlFree(id);
            }
            stanza_send(s, result);
            xmlFreeNode(result);

            /* Roster push with subscription=remove */
            roster_item_t removed;
            memset(&removed, 0, sizeof(removed));
            snprintf(removed.jid, sizeof(removed.jid), "%s", jid);
            snprintf(removed.subscription, sizeof(removed.subscription), "remove");
            roster_push(s, &removed);

            /* TODO: Cancel subscriptions in both directions (Phase 7) */
        } else {
            /* Add or update */
            roster_item_t *existing = roster_find_item(&s->roster, jid);
            const char *sub = existing ? existing->subscription : "none";
            int ask = existing ? existing->ask_subscribe : 0;

            roster_add_item(&s->roster, jid, name, sub, ask);
            if (name && existing) {
                snprintf(existing->name, sizeof(existing->name), "%s", name);
            }
            roster_save(s);

            /* Send result */
            xmlChar *id = xmlGetProp(stanza, (const xmlChar *)"id");
            xmlNodePtr result = xmlNewNode(NULL, (const xmlChar *)"iq");
            xmlNewProp(result, (const xmlChar *)"type", (const xmlChar *)"result");
            if (id) {
                xmlNewProp(result, (const xmlChar *)"id", id);
                xmlFree(id);
            }
            stanza_send(s, result);
            xmlFreeNode(result);

            /* Roster push */
            roster_item_t *item = roster_find_item(&s->roster, jid);
            if (item)
                roster_push(s, item);
        }

        xmlFree(jid_attr);
        if (name_attr) xmlFree(name_attr);
        if (sub_attr) xmlFree(sub_attr);
    } else {
        stanza_send_error(s, stanza, "cancel", "feature-not-implemented");
    }

    if (type_attr) xmlFree(type_attr);
}
