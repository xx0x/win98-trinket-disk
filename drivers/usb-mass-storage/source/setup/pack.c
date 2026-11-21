/* pack.c - language pack parser (see pack.h) */
#include "pack.h"

static char *dup_range(const char *a, const char *b) {
    char *r = xalloc((u32)(b - a) + 1);
    memcpy(r, a, (u32)(b - a));
    return r;
}
static u16 hex16(const char *s) {
    u16 v = 0;
    for (; *s; s++) {
        char c = *s;
        if (c >= '0' && c <= '9') v = (u16)(v * 16 + c - '0');
        else if (c >= 'a' && c <= 'f') v = (u16)(v * 16 + c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (u16)(v * 16 + c - 'A' + 10);
        else break;
    }
    return v;
}
static u16 dec16(const char *s) { u16 v = 0; while (*s >= '0' && *s <= '9') v = (u16)(v * 10 + *s++ - '0'); return v; }

static char *unescape(const char *a, const char *b) {
    char *r = xalloc((u32)(b - a) + 1), *o = r;
    if (b - a >= 2 && *a == '"' && b[-1] == '"') { a++; b--; }
    while (a < b) {
        if (*a == '\\' && a + 1 < b) {
            a++;
            *o++ = *a == 'n' ? '\n' : *a == 't' ? '\t' : *a;
            a++;
        } else *o++ = *a++;
    }
    return r;
}

BOOL pack_parse(pack_t *p, const char *t, u32 n) {
    const char *end = t + n, *line;
    char *sec = s_dup("");
    memset(p, 0, sizeof *p);
    if (n >= 3 && (u8)t[0] == 0xEF && (u8)t[1] == 0xBB && (u8)t[2] == 0xBF) t += 3;
    while (t < end) {
        const char *le, *a, *b, *eq;
        line = t;
        while (t < end && *t != '\n') t++;
        le = t;
        if (t < end) t++;
        while (le > line && (le[-1] == '\r' || le[-1] == ' ' || le[-1] == '\t')) le--;
        a = line;
        while (a < le && (*a == ' ' || *a == '\t')) a++;
        if (a == le || *a == ';' || *a == '#') continue;
        if (*a == '[') {
            b = a + 1;
            while (b < le && *b != ']') b++;
            xfree(sec); sec = dup_range(a + 1, b);
            continue;
        }
        for (eq = a; eq < le && *eq != '='; eq++) ;
        if (eq == le) continue;
        b = eq;
        while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
        if (p->n == p->cap) { p->cap = p->cap ? p->cap * 2 : 256; p->e = xrealloc(p->e, p->cap * sizeof(pent_t)); }
        p->e[p->n].sec = s_dup(sec);
        p->e[p->n].key = dup_range(a, b);
        eq++;
        while (eq < le && (*eq == ' ' || *eq == '\t')) eq++;
        p->e[p->n].val = unescape(eq, le);
        p->n++;
    }
    xfree(sec);
    {
        const char *v;
        if ((v = pack_get(p, "PACK", "LangID"))) p->lang = hex16(v);
        if ((v = pack_get(p, "PACK", "CodePage"))) p->cp = dec16(v);
        if ((v = pack_get(p, "PACK", "OEMCodePage"))) p->oemcp = dec16(v);
    }
    return p->n > 0;
}
void pack_free(pack_t *p) {
    int i;
    for (i = 0; i < p->n; i++) { xfree(p->e[i].sec); xfree(p->e[i].key); xfree(p->e[i].val); }
    xfree(p->e);
    memset(p, 0, sizeof *p);
}
const char *pack_get(const pack_t *p, const char *sec, const char *key) {
    int i;
    if (!p) return NULL;
    for (i = 0; i < p->n; i++) if (!s_icmp(p->e[i].sec, sec) && !s_icmp(p->e[i].key, key)) return p->e[i].val;
    return NULL;
}
wch *pack_getw(const pack_t *p, const char *sec, const char *key) {
    const char *v = pack_get(p, sec, key);
    return v ? w_from_utf8(v) : NULL;
}
int pack_has_section(const pack_t *p, const char *sec) {
    int i;
    if (!p) return 0;
    for (i = 0; i < p->n; i++) if (!s_icmp(p->e[i].sec, sec)) return 1;
    return 0;
}
