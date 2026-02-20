#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* --- JID utilities --- */

int jid_parse(const char *s, char *local, size_t local_sz,
              char *domain, size_t domain_sz,
              char *resource, size_t resource_sz)
{
    if (!s || !*s)
        return -1;

    if (local)    local[0] = '\0';
    if (domain)   domain[0] = '\0';
    if (resource) resource[0] = '\0';

    const char *at = strchr(s, '@');
    const char *slash = strchr(s, '/');

    if (at) {
        /* Has localpart */
        size_t llen = (size_t)(at - s);
        if (llen == 0)
            return -1;
        if (local) {
            if (llen >= local_sz) llen = local_sz - 1;
            memcpy(local, s, llen);
            local[llen] = '\0';
        }
        s = at + 1;
    }

    if (slash) {
        /* Has resource */
        size_t dlen;
        if (at) {
            dlen = (size_t)(slash - (at + 1));
        } else {
            dlen = (size_t)(slash - s);
        }
        if (dlen == 0)
            return -1;
        if (domain) {
            if (dlen >= domain_sz) dlen = domain_sz - 1;
            memcpy(domain, s, dlen);
            domain[dlen] = '\0';
        }
        const char *res = slash + 1;
        size_t rlen = strlen(res);
        if (resource) {
            if (rlen >= resource_sz) rlen = resource_sz - 1;
            memcpy(resource, res, rlen);
            resource[rlen] = '\0';
        }
    } else {
        /* No resource */
        size_t dlen = strlen(s);
        if (dlen == 0)
            return -1;
        if (domain) {
            if (dlen >= domain_sz) dlen = domain_sz - 1;
            memcpy(domain, s, dlen);
            domain[dlen] = '\0';
        }
    }

    return 0;
}

void jid_bare(const char *local, const char *domain, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%s@%s", local, domain);
}

void jid_full(const char *local, const char *domain, const char *resource,
              char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%s@%s/%s", local, domain, resource);
}

/* --- Base64 decoding --- */

static const unsigned char b64_table[256] = {
    ['A']=0,  ['B']=1,  ['C']=2,  ['D']=3,  ['E']=4,  ['F']=5,
    ['G']=6,  ['H']=7,  ['I']=8,  ['J']=9,  ['K']=10, ['L']=11,
    ['M']=12, ['N']=13, ['O']=14, ['P']=15, ['Q']=16, ['R']=17,
    ['S']=18, ['T']=19, ['U']=20, ['V']=21, ['W']=22, ['X']=23,
    ['Y']=24, ['Z']=25,
    ['a']=26, ['b']=27, ['c']=28, ['d']=29, ['e']=30, ['f']=31,
    ['g']=32, ['h']=33, ['i']=34, ['j']=35, ['k']=36, ['l']=37,
    ['m']=38, ['n']=39, ['o']=40, ['p']=41, ['q']=42, ['r']=43,
    ['s']=44, ['t']=45, ['u']=46, ['v']=47, ['w']=48, ['x']=49,
    ['y']=50, ['z']=51,
    ['0']=52, ['1']=53, ['2']=54, ['3']=55, ['4']=56, ['5']=57,
    ['6']=58, ['7']=59, ['8']=60, ['9']=61,
    ['+']=62, ['/']=63,
};

static const unsigned char b64_valid[256] = {
    ['A']=1,['B']=1,['C']=1,['D']=1,['E']=1,['F']=1,['G']=1,['H']=1,
    ['I']=1,['J']=1,['K']=1,['L']=1,['M']=1,['N']=1,['O']=1,['P']=1,
    ['Q']=1,['R']=1,['S']=1,['T']=1,['U']=1,['V']=1,['W']=1,['X']=1,
    ['Y']=1,['Z']=1,
    ['a']=1,['b']=1,['c']=1,['d']=1,['e']=1,['f']=1,['g']=1,['h']=1,
    ['i']=1,['j']=1,['k']=1,['l']=1,['m']=1,['n']=1,['o']=1,['p']=1,
    ['q']=1,['r']=1,['s']=1,['t']=1,['u']=1,['v']=1,['w']=1,['x']=1,
    ['y']=1,['z']=1,
    ['0']=1,['1']=1,['2']=1,['3']=1,['4']=1,['5']=1,['6']=1,['7']=1,
    ['8']=1,['9']=1,['+']=1,['/']=1,['=']=1,
};

int base64_decode(const char *in, size_t in_len,
                  unsigned char *out, size_t *out_len)
{
    /* Strip whitespace and find clean length */
    unsigned char clean[4096];
    size_t clen = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (b64_valid[c]) {
            if (clen >= sizeof(clean))
                return -1;
            clean[clen++] = c;
        }
    }

    if (clen % 4 != 0)
        return -1;

    size_t olen = 0;
    for (size_t i = 0; i < clen; i += 4) {
        unsigned int a = b64_table[clean[i]];
        unsigned int b = b64_table[clean[i+1]];
        unsigned int c = (clean[i+2] != '=') ? b64_table[clean[i+2]] : 0;
        unsigned int d = (clean[i+3] != '=') ? b64_table[clean[i+3]] : 0;

        unsigned int triple = (a << 18) | (b << 12) | (c << 6) | d;

        out[olen++] = (unsigned char)(triple >> 16);
        if (clean[i+2] != '=')
            out[olen++] = (unsigned char)(triple >> 8);
        if (clean[i+3] != '=')
            out[olen++] = (unsigned char)(triple);
    }

    *out_len = olen;
    return 0;
}

/* --- Random ID generation --- */

void generate_id(char *buf, size_t len) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    arc4random_buf(buf, len);
    for (size_t i = 0; i < len; i++) {
        buf[i] = charset[(unsigned char)buf[i] % (sizeof(charset) - 1)];
    }
    buf[len] = '\0';
}
