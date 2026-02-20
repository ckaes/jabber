#include "message.h"
#include "stanza.h"
#include "config.h"
#include "user.h"
#include "log.h"
#include "util.h"
#include "xml.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

void handle_message(session_t *s, xmlNodePtr stanza) {
    xmlChar *to_attr = xmlGetProp(stanza, (const xmlChar *)"to");
    xmlChar *type_attr = xmlGetProp(stanza, (const xmlChar *)"type");
    const char *to = to_attr ? (const char *)to_attr : "";
    const char *type = type_attr ? (const char *)type_attr : "normal";

    /* Parse target JID */
    char local[256], domain[256], resource[256];
    if (jid_parse(to, local, sizeof(local), domain, sizeof(domain),
                  resource, sizeof(resource)) < 0 || local[0] == '\0') {
        stanza_send_error(s, stanza, "modify", "jid-malformed");
        if (to_attr) xmlFree(to_attr);
        if (type_attr) xmlFree(type_attr);
        return;
    }

    /* Verify domain is ours */
    if (strcmp(domain, g_config.domain) != 0) {
        stanza_send_error(s, stanza, "cancel", "item-not-found");
        if (to_attr) xmlFree(to_attr);
        if (type_attr) xmlFree(type_attr);
        return;
    }

    /* Check user exists */
    if (!user_exists(local)) {
        stanza_send_error(s, stanza, "cancel", "item-not-found");
        if (to_attr) xmlFree(to_attr);
        if (type_attr) xmlFree(type_attr);
        return;
    }

    /* Set from to sender's full JID */
    char from_jid[768];
    jid_full(s->jid_local, s->jid_domain, s->jid_resource,
             from_jid, sizeof(from_jid));
    xmlSetProp(stanza, (const xmlChar *)"from", (const xmlChar *)from_jid);

    /* Look up recipient */
    char bare[512];
    jid_bare(local, domain, bare, sizeof(bare));
    session_t *target = session_find_by_jid(bare);

    if (target && target->available) {
        /* Deliver immediately */
        stanza_send(target, stanza);
    } else if (strcmp(type, "error") != 0) {
        /* Store offline (for chat/normal, never for error) */
        message_store_offline(local, stanza);
    }

    if (to_attr) xmlFree(to_attr);
    if (type_attr) xmlFree(type_attr);
}

void message_store_offline(const char *username, xmlNodePtr stanza) {
    char dir[1280];
    snprintf(dir, sizeof(dir), "%s/%s/offline", g_config.datadir, username);

    /* Ensure offline directory exists */
    mkdir(dir, 0755);

    /* Find next sequence number */
    int max_seq = 0;
    DIR *dp = opendir(dir);
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            int seq = atoi(de->d_name);
            if (seq > max_seq)
                max_seq = seq;
        }
        closedir(dp);
    }

    /* Add delay element (XEP-0203) */
    xmlNodePtr delay = xmlNewChild(stanza, NULL, (const xmlChar *)"delay", NULL);
    xmlNsPtr delay_ns = xmlNewNs(delay, (const xmlChar *)"urn:xmpp:delay", NULL);
    xmlSetNs(delay, delay_ns);
    xmlNewProp(delay, (const xmlChar *)"from", (const xmlChar *)g_config.domain);

    char stamp[32];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    xmlNewProp(delay, (const xmlChar *)"stamp", (const xmlChar *)stamp);

    /* Serialize and write to file */
    size_t xml_len;
    char *xml = stanza_serialize(stanza, &xml_len);
    if (!xml) {
        log_write(LOG_ERROR, "Failed to serialize offline message for %s", username);
        return;
    }

    char path[1536];
    snprintf(path, sizeof(path), "%s/%04d.xml", dir, max_seq + 1);

    FILE *fp = fopen(path, "w");
    if (fp) {
        fwrite(xml, 1, xml_len, fp);
        fclose(fp);
        log_write(LOG_INFO, "Stored offline message for %s: %s", username, path);
    } else {
        log_write(LOG_ERROR, "Failed to write offline message: %s", path);
    }

    free(xml);
}

void message_deliver_offline(session_t *s) {
    char dir[1280];
    snprintf(dir, sizeof(dir), "%s/%s/offline", g_config.datadir, s->jid_local);

    DIR *dp = opendir(dir);
    if (!dp)
        return;

    /* Collect filenames and sort */
    char filenames[256][64];
    int count = 0;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL && count < 256) {
        if (de->d_name[0] == '.')
            continue;
        size_t len = strlen(de->d_name);
        if (len < 5 || strcmp(de->d_name + len - 4, ".xml") != 0)
            continue;
        snprintf(filenames[count], sizeof(filenames[count]), "%s", de->d_name);
        count++;
    }
    closedir(dp);

    if (count == 0)
        return;

    /* Sort by filename (numeric order) */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(filenames[i], filenames[j]) > 0) {
                char tmp[64];
                memcpy(tmp, filenames[i], sizeof(tmp));
                memcpy(filenames[i], filenames[j], sizeof(tmp));
                memcpy(filenames[j], tmp, sizeof(tmp));
            }
        }
    }

    /* Deliver each message */
    for (int i = 0; i < count; i++) {
        char path[1536];
        snprintf(path, sizeof(path), "%s/%s", dir, filenames[i]);

        xmlDocPtr doc = xmlReadFile(path, NULL, 0);
        if (!doc) {
            log_write(LOG_WARN, "Failed to parse offline message: %s", path);
            unlink(path);
            continue;
        }

        xmlNodePtr root = xmlDocGetRootElement(doc);
        if (root) {
            stanza_send(s, root);
            log_write(LOG_INFO, "Delivered offline message to %s: %s",
                      s->jid_local, filenames[i]);
        }

        xmlFreeDoc(doc);
        unlink(path);
    }
}
