#include "presence.h"
#include "roster.h"
#include "stanza.h"
#include "server.h"
#include "config.h"
#include "log.h"
#include "xml.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>

/* Forward declaration — message module delivers offline messages */
void message_deliver_offline(session_t *s);

static int sub_has_to(const char *sub) {
    return strcmp(sub, "to") == 0 || strcmp(sub, "both") == 0;
}

static int sub_has_from(const char *sub) {
    return strcmp(sub, "from") == 0 || strcmp(sub, "both") == 0;
}

/* --- Available Presence (initial or update) --- */

static void presence_handle_available(session_t *s, xmlNodePtr stanza) {
    int is_initial = !s->available;

    s->available = 1;

    /* Store a copy of the presence stanza */
    if (s->presence_stanza)
        xmlFreeNode(s->presence_stanza);
    s->presence_stanza = xmlCopyNode(stanza, 1);

    /* Set from attribute to our full JID */
    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource,
             full_jid, sizeof(full_jid));
    xmlSetProp(s->presence_stanza, (const xmlChar *)"from",
               (const xmlChar *)full_jid);

    /* Load roster if not yet loaded */
    if (!s->roster.loaded)
        roster_load(s);

    /* Broadcast our presence to contacts with from/both subscription */
    for (int i = 0; i < s->roster.count; i++) {
        roster_item_t *ri = &s->roster.items[i];
        if (!sub_has_from(ri->subscription))
            continue;

        /* Parse contact's bare JID to find their session */
        char local[256], domain[256], resource[256];
        jid_parse(ri->jid, local, sizeof(local), domain, sizeof(domain),
                  resource, sizeof(resource));
        char bare[512];
        jid_bare(local, domain, bare, sizeof(bare));
        session_t *contact = session_find_by_jid(bare);
        if (contact) {
            stanza_send(contact, s->presence_stanza);
        }
    }

    /* Receive contacts' presence (contacts with to/both subscription) */
    for (int i = 0; i < s->roster.count; i++) {
        roster_item_t *ri = &s->roster.items[i];
        if (!sub_has_to(ri->subscription))
            continue;

        char local[256], domain[256], resource[256];
        jid_parse(ri->jid, local, sizeof(local), domain, sizeof(domain),
                  resource, sizeof(resource));
        char bare[512];
        jid_bare(local, domain, bare, sizeof(bare));
        session_t *contact = session_find_by_jid(bare);
        if (contact && contact->available && contact->presence_stanza) {
            stanza_send(s, contact->presence_stanza);
        }
    }

    if (is_initial) {
        s->initial_presence_sent = 1;
        /* Deliver offline messages */
        message_deliver_offline(s);
        /* Re-deliver pending subscribe requests */
        presence_redeliver_pending_subscribes(s);
    }
}

/* --- Unavailable Presence --- */

static void presence_handle_unavailable(session_t *s, xmlNodePtr stanza) {
    (void)stanza;
    presence_broadcast_unavailable(s);
    s->available = 0;
}

void presence_broadcast_unavailable(session_t *s) {
    if (!s->available && !s->initial_presence_sent)
        return;

    char full_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource,
             full_jid, sizeof(full_jid));

    xmlNodePtr pres = xmlNewNode(NULL, (const xmlChar *)"presence");
    xmlNewProp(pres, (const xmlChar *)"type", (const xmlChar *)"unavailable");
    xmlNewProp(pres, (const xmlChar *)"from", (const xmlChar *)full_jid);

    if (!s->roster.loaded)
        roster_load(s);

    for (int i = 0; i < s->roster.count; i++) {
        roster_item_t *ri = &s->roster.items[i];
        if (!sub_has_from(ri->subscription))
            continue;

        char local[256], domain[256], resource[256];
        jid_parse(ri->jid, local, sizeof(local), domain, sizeof(domain),
                  resource, sizeof(resource));
        char bare[512];
        jid_bare(local, domain, bare, sizeof(bare));
        session_t *contact = session_find_by_jid(bare);
        if (contact && contact != s) {
            stanza_send(contact, pres);
        }
    }

    xmlFreeNode(pres);
    s->available = 0;
}

/* --- Subscription: subscribe --- */

static void presence_handle_subscribe(session_t *s, xmlNodePtr stanza, const char *to) {
    (void)stanza;
    char local[256], domain[256], resource[256];
    jid_parse(to, local, sizeof(local), domain, sizeof(domain),
              resource, sizeof(resource));
    char bare[512];
    jid_bare(local, domain, bare, sizeof(bare));

    if (!s->roster.loaded)
        roster_load(s);

    /* Ensure sender's roster has an entry for target */
    roster_item_t *item = roster_find_item(&s->roster, bare);
    if (!item) {
        roster_add_item(&s->roster, bare, NULL, "none", 1);
        item = roster_find_item(&s->roster, bare);
    } else {
        item->ask_subscribe = 1;
    }
    roster_save(s);
    roster_push(s, item);

    /* Deliver to target if online */
    session_t *target = session_find_by_jid(bare);
    if (target) {
        char from_bare[512];
        jid_bare(s->jid_local, s->jid_domain, from_bare, sizeof(from_bare));

        xmlNodePtr sub = xmlNewNode(NULL, (const xmlChar *)"presence");
        xmlNewProp(sub, (const xmlChar *)"type", (const xmlChar *)"subscribe");
        xmlNewProp(sub, (const xmlChar *)"from", (const xmlChar *)from_bare);
        xmlNewProp(sub, (const xmlChar *)"to", (const xmlChar *)bare);
        stanza_send(target, sub);
        xmlFreeNode(sub);
    }
}

/* --- Subscription: subscribed (approve) --- */

static void presence_handle_subscribed(session_t *s, xmlNodePtr stanza, const char *to) {
    (void)stanza;

    char local[256], domain[256], resource[256];
    jid_parse(to, local, sizeof(local), domain, sizeof(domain),
              resource, sizeof(resource));
    char target_bare[512];
    jid_bare(local, domain, target_bare, sizeof(target_bare));

    char sender_bare[512];
    jid_bare(s->jid_local, s->jid_domain, sender_bare, sizeof(sender_bare));

    /* Update sender's (bob's) roster: none->from, to->both */
    if (!s->roster.loaded)
        roster_load(s);

    roster_item_t *sender_item = roster_find_item(&s->roster, target_bare);
    if (!sender_item) {
        roster_add_item(&s->roster, target_bare, NULL, "from", 0);
        sender_item = roster_find_item(&s->roster, target_bare);
    } else {
        if (strcmp(sender_item->subscription, "none") == 0)
            snprintf(sender_item->subscription, sizeof(sender_item->subscription), "from");
        else if (strcmp(sender_item->subscription, "to") == 0)
            snprintf(sender_item->subscription, sizeof(sender_item->subscription), "both");
    }
    roster_save(s);
    roster_push(s, sender_item);

    /* Update target's (alice's) roster: none->to, from->both, clear ask */
    session_t *target_session = session_find_by_jid(target_bare);
    roster_t target_roster;
    memset(&target_roster, 0, sizeof(target_roster));

    if (target_session && target_session->roster.loaded) {
        /* Use the online session's roster */
        roster_item_t *target_item = roster_find_item(&target_session->roster, sender_bare);
        if (target_item) {
            if (strcmp(target_item->subscription, "none") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "to");
            else if (strcmp(target_item->subscription, "from") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "both");
            target_item->ask_subscribe = 0;
        }
        roster_save(target_session);
        if (target_item)
            roster_push(target_session, target_item);
    } else {
        /* Modify on disk */
        roster_load_for_user(local, &target_roster);
        roster_item_t *target_item = roster_find_item(&target_roster, sender_bare);
        if (target_item) {
            if (strcmp(target_item->subscription, "none") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "to");
            else if (strcmp(target_item->subscription, "from") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "both");
            target_item->ask_subscribe = 0;
        }
        roster_save_for_user(local, &target_roster);
    }

    /* If target is online, send presence and subscribed notification */
    if (target_session) {
        /* Send sender's current presence to target */
        if (s->available && s->presence_stanza)
            stanza_send(target_session, s->presence_stanza);

        /* Send subscribed notification */
        xmlNodePtr notif = xmlNewNode(NULL, (const xmlChar *)"presence");
        xmlNewProp(notif, (const xmlChar *)"type", (const xmlChar *)"subscribed");
        xmlNewProp(notif, (const xmlChar *)"from", (const xmlChar *)sender_bare);
        xmlNewProp(notif, (const xmlChar *)"to", (const xmlChar *)target_bare);
        stanza_send(target_session, notif);
        xmlFreeNode(notif);
    }
}

/* --- Subscription: unsubscribe --- */

static void presence_handle_unsubscribe(session_t *s, xmlNodePtr stanza, const char *to) {
    (void)stanza;

    char local[256], domain[256], resource[256];
    jid_parse(to, local, sizeof(local), domain, sizeof(domain),
              resource, sizeof(resource));
    char target_bare[512];
    jid_bare(local, domain, target_bare, sizeof(target_bare));
    char sender_bare[512];
    jid_bare(s->jid_local, s->jid_domain, sender_bare, sizeof(sender_bare));

    /* Update sender's roster: to->none, both->from, clear ask */
    if (!s->roster.loaded)
        roster_load(s);

    roster_item_t *sender_item = roster_find_item(&s->roster, target_bare);
    if (sender_item) {
        if (strcmp(sender_item->subscription, "to") == 0)
            snprintf(sender_item->subscription, sizeof(sender_item->subscription), "none");
        else if (strcmp(sender_item->subscription, "both") == 0)
            snprintf(sender_item->subscription, sizeof(sender_item->subscription), "from");
        sender_item->ask_subscribe = 0;
        roster_save(s);
        roster_push(s, sender_item);
    }

    /* Update target's roster: from->none, both->to */
    session_t *target_session = session_find_by_jid(target_bare);
    if (target_session && target_session->roster.loaded) {
        roster_item_t *target_item = roster_find_item(&target_session->roster, sender_bare);
        if (target_item) {
            if (strcmp(target_item->subscription, "from") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "none");
            else if (strcmp(target_item->subscription, "both") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "to");
            roster_save(target_session);
            roster_push(target_session, target_item);
        }

        /* Deliver unsubscribe notification */
        xmlNodePtr notif = xmlNewNode(NULL, (const xmlChar *)"presence");
        xmlNewProp(notif, (const xmlChar *)"type", (const xmlChar *)"unsubscribe");
        xmlNewProp(notif, (const xmlChar *)"from", (const xmlChar *)sender_bare);
        xmlNewProp(notif, (const xmlChar *)"to", (const xmlChar *)target_bare);
        stanza_send(target_session, notif);
        xmlFreeNode(notif);

        /* Send unavailable from sender */
        if (s->available) {
            char full_jid[768];
            jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                     full_jid, sizeof(full_jid));
            xmlNodePtr unavail = xmlNewNode(NULL, (const xmlChar *)"presence");
            xmlNewProp(unavail, (const xmlChar *)"type", (const xmlChar *)"unavailable");
            xmlNewProp(unavail, (const xmlChar *)"from", (const xmlChar *)full_jid);
            stanza_send(target_session, unavail);
            xmlFreeNode(unavail);
        }
    } else {
        /* Offline: modify on disk */
        roster_t target_roster;
        memset(&target_roster, 0, sizeof(target_roster));
        roster_load_for_user(local, &target_roster);
        roster_item_t *target_item = roster_find_item(&target_roster, sender_bare);
        if (target_item) {
            if (strcmp(target_item->subscription, "from") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "none");
            else if (strcmp(target_item->subscription, "both") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "to");
            roster_save_for_user(local, &target_roster);
        }
    }
}

/* --- Subscription: unsubscribed (deny/revoke) --- */

static void presence_handle_unsubscribed(session_t *s, xmlNodePtr stanza, const char *to) {
    (void)stanza;

    char local[256], domain[256], resource[256];
    jid_parse(to, local, sizeof(local), domain, sizeof(domain),
              resource, sizeof(resource));
    char target_bare[512];
    jid_bare(local, domain, target_bare, sizeof(target_bare));
    char sender_bare[512];
    jid_bare(s->jid_local, s->jid_domain, sender_bare, sizeof(sender_bare));

    /* Update sender's roster: from->none, both->to */
    if (!s->roster.loaded)
        roster_load(s);

    roster_item_t *sender_item = roster_find_item(&s->roster, target_bare);
    if (sender_item) {
        if (strcmp(sender_item->subscription, "from") == 0)
            snprintf(sender_item->subscription, sizeof(sender_item->subscription), "none");
        else if (strcmp(sender_item->subscription, "both") == 0)
            snprintf(sender_item->subscription, sizeof(sender_item->subscription), "to");
        roster_save(s);
        roster_push(s, sender_item);
    }

    /* Update target's roster: to->none, both->from, clear ask */
    session_t *target_session = session_find_by_jid(target_bare);
    if (target_session && target_session->roster.loaded) {
        roster_item_t *target_item = roster_find_item(&target_session->roster, sender_bare);
        if (target_item) {
            if (strcmp(target_item->subscription, "to") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "none");
            else if (strcmp(target_item->subscription, "both") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "from");
            target_item->ask_subscribe = 0;
            roster_save(target_session);
            roster_push(target_session, target_item);
        }

        /* Deliver unsubscribed notification */
        xmlNodePtr notif = xmlNewNode(NULL, (const xmlChar *)"presence");
        xmlNewProp(notif, (const xmlChar *)"type", (const xmlChar *)"unsubscribed");
        xmlNewProp(notif, (const xmlChar *)"from", (const xmlChar *)sender_bare);
        xmlNewProp(notif, (const xmlChar *)"to", (const xmlChar *)target_bare);
        stanza_send(target_session, notif);
        xmlFreeNode(notif);

        /* Send unavailable from sender */
        if (s->available) {
            char full_jid[768];
            jid_full(s->jid_local, s->jid_domain, s->jid_resource,
                     full_jid, sizeof(full_jid));
            xmlNodePtr unavail = xmlNewNode(NULL, (const xmlChar *)"presence");
            xmlNewProp(unavail, (const xmlChar *)"type", (const xmlChar *)"unavailable");
            xmlNewProp(unavail, (const xmlChar *)"from", (const xmlChar *)full_jid);
            stanza_send(target_session, unavail);
            xmlFreeNode(unavail);
        }
    } else {
        roster_t target_roster;
        memset(&target_roster, 0, sizeof(target_roster));
        roster_load_for_user(local, &target_roster);
        roster_item_t *target_item = roster_find_item(&target_roster, sender_bare);
        if (target_item) {
            if (strcmp(target_item->subscription, "to") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "none");
            else if (strcmp(target_item->subscription, "both") == 0)
                snprintf(target_item->subscription, sizeof(target_item->subscription), "from");
            target_item->ask_subscribe = 0;
            roster_save_for_user(local, &target_roster);
        }
    }
}

/* --- Pending subscribe re-delivery on login --- */

void presence_redeliver_pending_subscribes(session_t *s) {
    char our_bare[512];
    jid_bare(s->jid_local, s->jid_domain, our_bare, sizeof(our_bare));

    /* Scan all online sessions for roster entries pointing at us with ask=subscribe */
    session_t **sessions = server_get_sessions();
    int nfds = server_get_nfds();

    for (int i = 1; i < nfds; i++) {
        session_t *other = sessions[i];
        if (!other || other == s || other->jid_local[0] == '\0')
            continue;
        if (!other->roster.loaded)
            continue;

        for (int j = 0; j < other->roster.count; j++) {
            roster_item_t *ri = &other->roster.items[j];
            if (!ri->ask_subscribe)
                continue;
            if (strcmp(ri->jid, our_bare) != 0)
                continue;

            /* This user has a pending subscribe to us — re-deliver */
            char from_bare[512];
            jid_bare(other->jid_local, other->jid_domain,
                     from_bare, sizeof(from_bare));

            xmlNodePtr sub = xmlNewNode(NULL, (const xmlChar *)"presence");
            xmlNewProp(sub, (const xmlChar *)"type", (const xmlChar *)"subscribe");
            xmlNewProp(sub, (const xmlChar *)"from", (const xmlChar *)from_bare);
            xmlNewProp(sub, (const xmlChar *)"to", (const xmlChar *)our_bare);
            stanza_send(s, sub);
            xmlFreeNode(sub);
        }
    }
}

/* --- Main dispatcher --- */

void handle_presence(session_t *s, xmlNodePtr stanza) {
    xmlChar *type_attr = xmlGetProp(stanza, (const xmlChar *)"type");
    xmlChar *to_attr   = xmlGetProp(stanza, (const xmlChar *)"to");
    const char *type = type_attr ? (const char *)type_attr : "";
    const char *to   = to_attr ? (const char *)to_attr : "";

    if (type[0] == '\0') {
        /* Available presence */
        presence_handle_available(s, stanza);
    } else if (strcmp(type, "unavailable") == 0) {
        presence_handle_unavailable(s, stanza);
    } else if (strcmp(type, "subscribe") == 0) {
        presence_handle_subscribe(s, stanza, to);
    } else if (strcmp(type, "subscribed") == 0) {
        presence_handle_subscribed(s, stanza, to);
    } else if (strcmp(type, "unsubscribe") == 0) {
        presence_handle_unsubscribe(s, stanza, to);
    } else if (strcmp(type, "unsubscribed") == 0) {
        presence_handle_unsubscribed(s, stanza, to);
    } else {
        log_write(LOG_WARN, "Unknown presence type '%s' from fd %d", type, s->fd);
    }

    if (type_attr) xmlFree(type_attr);
    if (to_attr) xmlFree(to_attr);
}
