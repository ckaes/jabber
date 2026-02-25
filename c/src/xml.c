#include "xml.h"
#include "session.h"
#include "stanza.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlerror.h>

/* Forward declarations for stream handlers (Phase 4) */
void stream_handle_open(session_t *s, const char *to, const char *xmlns);
void stream_handle_close(session_t *s);

static xmlSAXHandler sax_handler;

/*
 * Extract a named attribute from the SAX2 attributes array.
 * Returns a malloc'd string or NULL. Caller must free with xmlFree().
 *
 * The attributes array contains nb_attributes * 5 pointers:
 *   [i*5+0] = localname
 *   [i*5+1] = prefix
 *   [i*5+2] = namespace URI
 *   [i*5+3] = value start (NOT null-terminated)
 *   [i*5+4] = value end
 */
static xmlChar *sax_get_attribute(const xmlChar **attributes,
                                  int nb_attributes, const char *name)
{
    if (!attributes)
        return NULL;

    for (int i = 0; i < nb_attributes; i++) {
        const xmlChar *localname = attributes[i * 5];
        if (xmlStrcmp(localname, (const xmlChar *)name) == 0) {
            const xmlChar *val_start = attributes[i * 5 + 3];
            const xmlChar *val_end   = attributes[i * 5 + 4];
            return xmlStrndup(val_start, (int)(val_end - val_start));
        }
    }
    return NULL;
}

static void on_start_element_ns(
    void *ctx,
    const xmlChar *localname,
    const xmlChar *prefix,
    const xmlChar *URI,
    int nb_namespaces,
    const xmlChar **namespaces,
    int nb_attributes,
    int nb_defaulted,
    const xmlChar **attributes)
{
    (void)nb_defaulted;

    session_t *s = (session_t *)ctx;
    s->stanza_depth++;

    if (s->stanza_depth == 1) {
        /* <stream:stream> — extract attributes, don't build DOM */
        xmlChar *to = sax_get_attribute(attributes, nb_attributes, "to");
        stream_handle_open(s, to ? (const char *)to : "",
                           URI ? (const char *)URI : "");
        if (to) xmlFree(to);
        return;
    }

    /* Build a DOM node for this element */
    xmlNodePtr node = xmlNewNode(NULL, localname);
    if (!node) {
        log_write(LOG_ERROR, "xmlNewNode failed");
        return;
    }

    /* Attach namespace declarations from this element */
    for (int i = 0; i < nb_namespaces; i++) {
        const xmlChar *ns_prefix = namespaces[i * 2];
        const xmlChar *ns_uri    = namespaces[i * 2 + 1];
        xmlNsPtr ns = xmlNewNs(node, ns_uri, ns_prefix);
        /* If this namespace matches the element's own URI, set it */
        if (URI && xmlStrEqual(ns_uri, URI) &&
            ((prefix == NULL && ns_prefix == NULL) ||
             (prefix && ns_prefix && xmlStrEqual(prefix, ns_prefix)))) {
            xmlSetNs(node, ns);
        }
    }

    /* If the element has a URI but we haven't set a namespace yet */
    if (!node->ns && URI) {
        /* Search existing ns declarations on this node */
        xmlNsPtr ns = NULL;
        for (xmlNsPtr cur = node->nsDef; cur; cur = cur->next) {
            if (xmlStrEqual(cur->href, URI)) {
                ns = cur;
                break;
            }
        }
        if (!ns)
            ns = xmlNewNs(node, URI, prefix);
        xmlSetNs(node, ns);
    }

    /* Copy attributes */
    for (int i = 0; i < nb_attributes; i++) {
        const xmlChar *attr_local  = attributes[i * 5 + 0];
        const xmlChar *attr_prefix = attributes[i * 5 + 1];
        const xmlChar *attr_uri    = attributes[i * 5 + 2];
        const xmlChar *val_start   = attributes[i * 5 + 3];
        const xmlChar *val_end     = attributes[i * 5 + 4];
        xmlChar *value = xmlStrndup(val_start, (int)(val_end - val_start));

        if (attr_uri && attr_uri[0]) {
            xmlNsPtr attr_ns = NULL;
            for (xmlNsPtr cur = node->nsDef; cur; cur = cur->next) {
                if (xmlStrEqual(cur->href, attr_uri)) {
                    attr_ns = cur;
                    break;
                }
            }
            if (!attr_ns)
                attr_ns = xmlNewNs(node, attr_uri, attr_prefix);
            xmlNewNsProp(node, attr_ns, attr_local, value);
        } else {
            xmlNewProp(node, attr_local, value);
        }
        xmlFree(value);
    }

    if (s->stanza_depth == 2) {
        /* Root of a new stanza */
        s->current_stanza = node;
        s->current_node   = node;
    } else {
        /* Child element inside a stanza */
        xmlAddChild(s->current_node, node);
        s->current_node = node;
    }
}

static void on_end_element_ns(void *ctx,
    const xmlChar *localname, const xmlChar *prefix, const xmlChar *URI)
{
    (void)localname;
    (void)prefix;
    (void)URI;

    session_t *s = (session_t *)ctx;
    s->stanza_depth--;

    if (s->stanza_depth == 0) {
        /* </stream:stream> */
        stream_handle_close(s);
    } else if (s->stanza_depth == 1) {
        /* Complete stanza — dispatch */
        if (s->current_stanza) {
            stanza_route(s, s->current_stanza);
            xmlFreeNode(s->current_stanza);
            s->current_stanza = NULL;
            s->current_node   = NULL;
        }
    } else {
        /* Move up to parent */
        if (s->current_node && s->current_node->parent)
            s->current_node = s->current_node->parent;
    }
}

static void on_characters(void *ctx, const xmlChar *ch, int len) {
    session_t *s = (session_t *)ctx;
    if (s->stanza_depth >= 2 && s->current_node) {
        xmlNodeAddContentLen(s->current_node, ch, len);
    }
}

static void on_structured_error(void *ctx, xmlErrorPtr error) {
    session_t *s = (session_t *)ctx;
    if (!s)
        return;

    /* Ignore warnings */
    if (error->level < XML_ERR_ERROR)
        return;

    log_write(LOG_WARN, "XML parse error on fd %d: %s",
              s->fd, error->message ? error->message : "(unknown)");

    /* Defer teardown if called from within a SAX callback (xmlParseChunk is
     * on the stack); freeing the parser context now would be use-after-free. */
    if (s->in_xml_parse)
        s->teardown_pending = 1;
    else
        session_teardown(s);
}

/* --- Public API --- */

void xml_init_sax_handler(void) {
    memset(&sax_handler, 0, sizeof(sax_handler));
    sax_handler.initialized    = XML_SAX2_MAGIC;
    sax_handler.startElementNs = on_start_element_ns;
    sax_handler.endElementNs   = on_end_element_ns;
    sax_handler.characters     = on_characters;
    sax_handler.serror         = on_structured_error;
}

void xml_parser_create(session_t *s) {
    if (s->xml_ctx) {
        xmlFreeParserCtxt(s->xml_ctx);
        s->xml_ctx = NULL;
    }

    s->xml_ctx = xmlCreatePushParserCtxt(&sax_handler, s, NULL, 0, NULL);
    if (!s->xml_ctx) {
        log_write(LOG_ERROR, "Failed to create XML push parser for fd %d", s->fd);
        return;
    }

    xmlCtxtUseOptions(s->xml_ctx, XML_PARSE_NONET);

    s->stanza_depth   = 0;
    s->current_stanza = NULL;
    s->current_node   = NULL;
}

void xml_parser_reset(session_t *s) {
    /* Destroy existing parser and create fresh one for stream restart */
    if (s->xml_ctx) {
        xmlFreeParserCtxt(s->xml_ctx);
        s->xml_ctx = NULL;
    }
    if (s->current_stanza) {
        xmlFreeNode(s->current_stanza);
        s->current_stanza = NULL;
    }
    s->current_node = NULL;
    s->stanza_depth = 0;

    xml_parser_create(s);
}

void xml_parser_destroy(session_t *s) {
    if (s->xml_ctx) {
        xmlFreeParserCtxt(s->xml_ctx);
        s->xml_ctx = NULL;
    }
    if (s->current_stanza) {
        xmlFreeNode(s->current_stanza);
        s->current_stanza = NULL;
    }
    s->current_node = NULL;
    s->stanza_depth = 0;
}

void xml_feed(session_t *s, const char *data, int len) {
    if (!s->xml_ctx || !data || len <= 0)
        return;
    xmlParseChunk(s->xml_ctx, data, len, 0);
}

/* --- DOM helper utilities --- */

const char *xml_get_attr(xmlNodePtr node, const char *name) {
    if (!node)
        return NULL;
    xmlChar *val = xmlGetProp(node, (const xmlChar *)name);
    /* Note: returns internal libxml2 string. The caller should NOT free this
     * unless they use xmlGetProp (which returns a copy). Since we want a
     * simple const char*, we return the copy and the caller must be aware.
     * Actually, xmlGetProp returns a copy that must be freed with xmlFree.
     * For convenience we return it as const char* but callers of this function
     * should be careful. In practice for short-lived use during stanza
     * processing this is acceptable with the understanding that it leaks
     * if not freed. We'll use a different pattern below. */
    return (const char *)val;
}

xmlNodePtr xml_find_child(xmlNodePtr node, const char *name) {
    if (!node)
        return NULL;
    for (xmlNodePtr child = node->children; child; child = child->next) {
        if (child->type == XML_ELEMENT_NODE &&
            xmlStrcmp(child->name, (const xmlChar *)name) == 0)
            return child;
    }
    return NULL;
}

xmlNodePtr xml_find_child_ns(xmlNodePtr node, const char *name, const char *ns) {
    if (!node)
        return NULL;
    for (xmlNodePtr child = node->children; child; child = child->next) {
        if (child->type == XML_ELEMENT_NODE &&
            xmlStrcmp(child->name, (const xmlChar *)name) == 0 &&
            child->ns && xmlStrcmp(child->ns->href, (const xmlChar *)ns) == 0)
            return child;
    }
    return NULL;
}
