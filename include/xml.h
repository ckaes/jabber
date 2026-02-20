#ifndef XMPPD_XML_H
#define XMPPD_XML_H

#include <libxml/tree.h>

/* Forward declaration */
typedef struct session session_t;

/* Initialize the global SAX handler (call once at startup) */
void xml_init_sax_handler(void);

/* Create/reset/destroy push parser for a session */
void xml_parser_create(session_t *s);
void xml_parser_reset(session_t *s);
void xml_parser_destroy(session_t *s);

/* Feed TCP data to the push parser */
void xml_feed(session_t *s, const char *data, int len);

/* DOM helper utilities */
const char *xml_get_attr(xmlNodePtr node, const char *name);
xmlNodePtr  xml_find_child(xmlNodePtr node, const char *name);
xmlNodePtr  xml_find_child_ns(xmlNodePtr node, const char *name, const char *ns);

#endif
