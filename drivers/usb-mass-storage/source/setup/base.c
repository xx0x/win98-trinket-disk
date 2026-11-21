/* base.c - pure helpers (strings, buffers, CRC, version info); also used by host tests */
#include "base.h"

int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
char *s_cpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }
char *s_cat(char *d, const char *s) { s_cpy(d + s_len(d), s); return d; }
char *s_dup(const char *s) { char *r = xalloc(s_len(s) + 1); return s_cpy(r, s); }
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int s_icmp(const char *a, const char *b) {
    while (*a && lc((u8)*a) == lc((u8)*b)) { a++; b++; }
    return lc((u8)*a) - lc((u8)*b);
}
int s_nicmp(const char *a, const char *b, int n) {
    while (n-- > 0) {
        int d = lc((u8)*a) - lc((u8)*b);
        if (d || !*a) return d;
        a++; b++;
    }
    return 0;
}
const char *s_istr(const char *h, const char *n) {
    int ln = s_len(n);
    for (; *h; h++) if (!s_nicmp(h, n, ln)) return h;
    return NULL;
}
int s_istarts(const char *s, const char *p) { return !s_nicmp(s, p, s_len(p)); }
int dec_atoi(const char *s) { int v = 0, neg = 0; while (*s == ' ') s++; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

void buf_put(buf_t *b, const void *d, u32 n) {
    if (b->n + n > b->cap) {
        u32 c = b->cap ? b->cap : 4096;
        while (c < b->n + n) c *= 2;
        b->p = xrealloc(b->p, c); b->cap = c;
    }
    if (d) memcpy(b->p + b->n, d, n); else memset(b->p + b->n, 0, n);
    b->n += n;
}
void buf_puts(buf_t *b, const char *s) { buf_put(b, s, s_len(s)); }
void buf_pad(buf_t *b, u32 a) { while (b->n % a) buf_put(b, NULL, 1); }
void buf_free(buf_t *b) { xfree(b->p); b->p = NULL; b->n = b->cap = 0; }

void path_join(char *out, const char *a, const char *b) {
    int n;
    s_cpy(out, a); n = s_len(out);
    if (n && out[n - 1] != '\\') s_cat(out, "\\");
    s_cat(out, b);
}
const char *path_name(const char *p) {
    const char *r = p;
    for (; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') r = p + 1;
    return r;
}

u32 crc32(const void *d, u32 n) {
    static u32 t[256]; static int init;
    const u8 *p = d; u32 c = 0xFFFFFFFF, i, k;
    if (!init) {
        for (i = 0; i < 256; i++) { c = i; for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1; t[i] = c; }
        init = 1; c = 0xFFFFFFFF;
    }
    while (n--) c = t[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}
BOOL file_version(const u8 *d, u32 n, u32 *ms, u32 *ls) {
    u32 i;
    for (i = 0; i + 16 <= n; i++)
        if (d[i] == 0xBD && d[i + 1] == 0x04 && d[i + 2] == 0xEF && d[i + 3] == 0xFE) {
            memcpy(ms, d + i + 8, 4); memcpy(ls, d + i + 12, 4);
            return TRUE;
        }
    *ms = *ls = 0;
    return FALSE;
}
void ver_str(char *out, u32 ms, u32 ls) { wsprintfA(out, "%u.%u.%u.%u", ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF); }

