#ifndef XMPPD_UTIL_H
#define XMPPD_UTIL_H

#include <stddef.h>

/* JID utilities */
int  jid_parse(const char *s, char *local, size_t local_sz,
               char *domain, size_t domain_sz,
               char *resource, size_t resource_sz);
void jid_bare(const char *local, const char *domain, char *out, size_t out_sz);
void jid_full(const char *local, const char *domain, const char *resource,
              char *out, size_t out_sz);

/* Base64 decoding */
int base64_decode(const char *in, size_t in_len,
                  unsigned char *out, size_t *out_len);

/* Random ID generation */
void generate_id(char *buf, size_t len);

#endif
