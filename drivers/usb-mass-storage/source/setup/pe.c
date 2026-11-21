/* pe.c - PE resource engine (port of tools/rsrc.py and tools/verres.py, same output layout) */
#include "pe.h"

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put16(buf_t *b, u32 v) { u8 t[2]; wr16(t, v); buf_put(b, t, 2); }
static void put32(buf_t *b, u32 v) { u8 t[4]; wr32(t, v); buf_put(b, t, 4); }
static u32 align(u32 x, u32 a) { return (x + a - 1) / a * a; }

/* ------------------------------------------------------------ wide strings */
int w_len(const wch *s) { int n = 0; while (s[n]) n++; return n; }
wch *w_dup(const wch *s) { int n = w_len(s); wch *r = xalloc((n + 1) * 2); memcpy(r, s, n * 2); return r; }
wch *w_from_ascii(const char *s) { int n = s_len(s), i; wch *r = xalloc((n + 1) * 2); for (i = 0; i < n; i++) r[i] = (u8)s[i]; return r; }
int w_eq_ascii(const wch *w, const char *a) {
    for (; *w && *a; w++, a++) {
        wch x = *w, y = (u8)*a;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
    }
    return !*w && !*a;
}
wch *w_from_utf8(const char *s) {
    const u8 *p = (const u8 *)s; int n = s_len(s); wch *r = xalloc((n + 1) * 2); int o = 0;
    while (*p) {
        u32 c = *p++;
        if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
        else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
        if (c >= 0x10000) { c -= 0x10000; r[o++] = (wch)(0xD800 | (c >> 10)); r[o++] = (wch)(0xDC00 | (c & 0x3FF)); }
        else r[o++] = (wch)c;
    }
    return r;
}
static wch *w_from_buf(const u8 *p, int n) { wch *r = xalloc((n + 1) * 2); int i; for (i = 0; i < n; i++) r[i] = rd16(p + 2 * i); return r; }

/* ------------------------------------------------------------ reading */
static u32 rva2off(const u8 *d, u32 n, u32 rva) {
    u32 peo = rd32(d + 0x3C), opt = peo + 24, nsec = rd16(d + peo + 6), sh = opt + rd16(d + peo + 20), k;
    for (k = 0; k < nsec; k++) {
        const u8 *s = d + sh + 40 * k;
        u32 vs = rd32(s + 8), va = rd32(s + 12), rsz = rd32(s + 16), rp = rd32(s + 20), span = vs > rsz ? vs : rsz;
        if (rva >= va && rva < va + span) return rp + rva - va;
    }
    return 0xFFFFFFFF;
}
static void rname_read(const u8 *d, u32 base, u32 v, rname_t *r) {
    if (v & 0x80000000) { const u8 *p = d + base + (v & 0x7FFFFFFF); r->str = w_from_buf(p + 2, rd16(p)); r->id = 0; }
    else { r->id = (u16)v; r->str = NULL; }
}
static void reslist_add(reslist_t *l, res_t *r) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->v = xrealloc(l->v, l->cap * sizeof(res_t)); }
    l->v[l->n++] = *r;
}
BOOL pe_read_resources(const u8 *d, u32 n, reslist_t *out) {
    u32 peo, opt, rva, base, i, j, k;
    memset(out, 0, sizeof *out);
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return FALSE;
    peo = rd32(d + 0x3C);
    if (peo + 0x100 > n || memcmp(d + peo, "PE\0\0", 4)) return FALSE;
    opt = peo + 24;
    rva = rd32(d + opt + 96 + 16);
    if (!rva || (base = rva2off(d, n, rva)) == 0xFFFFFFFF) return FALSE;
    {
        const u8 *rt = d + base; u32 nt = rd16(rt + 12) + rd16(rt + 14);
        for (i = 0; i < nt; i++) {
            u32 tn = rd32(rt + 16 + 8 * i), to = rd32(rt + 20 + 8 * i);
            const u8 *nd = d + base + (to & 0x7FFFFFFF); u32 nn = rd16(nd + 12) + rd16(nd + 14);
            for (j = 0; j < nn; j++) {
                u32 nmv = rd32(nd + 16 + 8 * j), no = rd32(nd + 20 + 8 * j);
                const u8 *ld = d + base + (no & 0x7FFFFFFF); u32 nl = rd16(ld + 12) + rd16(ld + 14);
                for (k = 0; k < nl; k++) {
                    u32 lid = rd32(ld + 16 + 8 * k), de = rd32(ld + 20 + 8 * k);
                    const u8 *e = d + base + de;
                    u32 drva = rd32(e), size = rd32(e + 4), off = rva2off(d, n, drva);
                    res_t r;
                    memset(&r, 0, sizeof r);
                    if (off == 0xFFFFFFFF || off + size > n) return FALSE;
                    rname_read(d, base, tn, &r.type);
                    rname_read(d, base, nmv, &r.name);
                    r.lang = (u16)lid; r.codepage = rd32(e + 8);
                    r.data = xalloc(size); memcpy(r.data, d + off, size); r.size = size;
                    reslist_add(out, &r);
                }
            }
        }
    }
    return TRUE;
}
void reslist_free(reslist_t *l) {
    int i;
    for (i = 0; i < l->n; i++) { xfree(l->v[i].type.str); xfree(l->v[i].name.str); xfree(l->v[i].data); }
    xfree(l->v); memset(l, 0, sizeof *l);
}
res_t *res_find(reslist_t *l, u16 type, u16 id) {
    int i;
    for (i = 0; i < l->n; i++) if (!l->v[i].type.str && l->v[i].type.id == type && !l->v[i].name.str && l->v[i].name.id == id) return &l->v[i];
    return NULL;
}
res_t *res_find_named(reslist_t *l, u16 type, const char *name) {
    int i;
    for (i = 0; i < l->n; i++) if (!l->v[i].type.str && l->v[i].type.id == type && l->v[i].name.str && w_eq_ascii(l->v[i].name.str, name)) return &l->v[i];
    return NULL;
}
void res_set_data(res_t *r, const u8 *d, u32 n) { xfree(r->data); r->data = xalloc(n); memcpy(r->data, d, n); r->size = n; }

/* ------------------------------------------------------------ building (same layout as rsrc.build_rsrc) */
static int wcmp_upper(const wch *a, const wch *b) {
    for (;; a++, b++) {
        wch x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y || !x) return (int)x - (int)y;
    }
}
static int rname_cmp(const rname_t *a, const rname_t *b) {
    if (a->str && !b->str) return -1;
    if (!a->str && b->str) return 1;
    if (a->str) return wcmp_upper(a->str, b->str);
    return (int)a->id - (int)b->id;
}
static int rname_eq(const rname_t *a, const rname_t *b) {
    if (!!a->str != !!b->str) return 0;
    if (a->str) { int i; for (i = 0; a->str[i] || b->str[i]; i++) if (a->str[i] != b->str[i]) return 0; return 1; }
    return a->id == b->id;
}

typedef struct { int ntypes; int *types; int *nnames; int **names; } tree_t;  /* indexes into reslist */

static void sort_idx(reslist_t *l, int *idx, int n, int by_name) {
    int i, j;
    for (i = 1; i < n; i++) for (j = i; j > 0; j--) {
        res_t *a = &l->v[idx[j - 1]], *b = &l->v[idx[j]];
        int c = by_name == 0 ? rname_cmp(&a->type, &b->type) : by_name == 1 ? rname_cmp(&a->name, &b->name) : (int)a->lang - (int)b->lang;
        if (c > 0) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; } else break;
    }
}

static void build_rsrc(reslist_t *l, u32 rva, buf_t *out) {
    int *types = xalloc(sizeof(int) * (l->n + 1)), nt = 0, i, j, k, t;
    int *names = xalloc(sizeof(int) * (l->n + 1));
    u32 off = 0, *diroff_t, *diroff_n, *deoff, *dataoff, total, nstr = 0;
    rname_t **strs = xalloc(sizeof(rname_t *) * (2 * l->n + 2));
    u32 *stroff = xalloc(sizeof(u32) * (2 * l->n + 2));
    int *nameidx_start = xalloc(sizeof(int) * (l->n + 1)), *nameidx_cnt = xalloc(sizeof(int) * (l->n + 1));
    int nnames = 0;
    u8 *b;
    /* unique sorted types: representative resource index */
    for (i = 0; i < l->n; i++) {
        for (j = 0; j < nt; j++) if (rname_eq(&l->v[types[j]].type, &l->v[i].type)) break;
        if (j == nt) types[nt++] = i;
    }
    sort_idx(l, types, nt, 0);
    /* per type: unique sorted names */
    for (t = 0; t < nt; t++) {
        nameidx_start[t] = nnames;
        for (i = 0; i < l->n; i++) {
            if (!rname_eq(&l->v[i].type, &l->v[types[t]].type)) continue;
            for (j = nameidx_start[t]; j < nnames; j++) if (rname_eq(&l->v[names[j]].name, &l->v[i].name)) break;
            if (j == nnames) names[nnames++] = i;
        }
        nameidx_cnt[t] = nnames - nameidx_start[t];
        sort_idx(l, names + nameidx_start[t], nameidx_cnt[t], 1);
    }
    diroff_t = xalloc(4 * (nt + 1)); diroff_n = xalloc(4 * (nnames + 1));
    deoff = xalloc(4 * (l->n + 1)); dataoff = xalloc(4 * (l->n + 1));
    /* directories: root, type dirs, name dirs */
    off = 16 + 8 * nt;
    for (t = 0; t < nt; t++) { diroff_t[t] = off; off += 16 + 8 * nameidx_cnt[t]; }
    for (t = 0; t < nt; t++) for (j = 0; j < nameidx_cnt[t]; j++) {
        int ni = nameidx_start[t] + j, cnt = 0;
        for (i = 0; i < l->n; i++) if (rname_eq(&l->v[i].type, &l->v[types[t]].type) && rname_eq(&l->v[i].name, &l->v[names[ni]].name)) cnt++;
        diroff_n[ni] = off; off += 16 + 8 * cnt;
    }
    /* strings */
    for (t = 0; t < nt; t++) {
        rname_t *cand[64]; int nc = 0;
        if (l->v[types[t]].type.str) cand[nc++] = &l->v[types[t]].type;
        for (j = 0; j < nameidx_cnt[t] && nc < 64; j++) if (l->v[names[nameidx_start[t] + j]].name.str) cand[nc++] = &l->v[names[nameidx_start[t] + j]].name;
        for (k = 0; k < nc; k++) {
            u32 s;
            for (s = 0; s < nstr; s++) if (rname_eq(strs[s], cand[k])) break;
            if (s == nstr) { strs[nstr] = cand[k]; stroff[nstr] = off; off += 2 + 2 * w_len(cand[k]->str); nstr++; }
        }
    }
    off = align(off, 4);
    /* data entries, then data */
    {
        int *order = xalloc(sizeof(int) * (l->n + 1)), no = 0;
        for (t = 0; t < nt; t++) for (j = 0; j < nameidx_cnt[t]; j++) {
            int ni = nameidx_start[t] + j, first = no;
            for (i = 0; i < l->n; i++) if (rname_eq(&l->v[i].type, &l->v[types[t]].type) && rname_eq(&l->v[i].name, &l->v[names[ni]].name)) order[no++] = i;
            sort_idx(l, order + first, no - first, 2);
        }
        for (i = 0; i < no; i++) { deoff[order[i]] = off; off += 16; }
        for (i = 0; i < no; i++) { off = align(off, 8); dataoff[order[i]] = off; off += l->v[order[i]].size; }
        total = align(off, 8);
        out->n = 0;
        buf_put(out, NULL, total);
        b = out->p;
        /* root */
        {
            int named = 0;
            for (t = 0; t < nt; t++) if (l->v[types[t]].type.str) named++;
            wr32(b, 0); wr32(b + 4, 0); wr16(b + 8, 4); wr16(b + 10, 0); wr16(b + 12, named); wr16(b + 14, nt - named);
            for (t = 0; t < nt; t++) {
                rname_t *rn = &l->v[types[t]].type; u32 s, nm = rn->id;
                if (rn->str) { for (s = 0; s < nstr; s++) if (rname_eq(strs[s], rn)) break; nm = 0x80000000 | stroff[s]; }
                wr32(b + 16 + 8 * t, nm); wr32(b + 20 + 8 * t, 0x80000000 | diroff_t[t]);
            }
        }
        for (t = 0; t < nt; t++) {
            u8 *p = b + diroff_t[t]; int named = 0;
            for (j = 0; j < nameidx_cnt[t]; j++) if (l->v[names[nameidx_start[t] + j]].name.str) named++;
            wr32(p, 0); wr32(p + 4, 0); wr16(p + 8, 4); wr16(p + 10, 0); wr16(p + 12, named); wr16(p + 14, nameidx_cnt[t] - named);
            for (j = 0; j < nameidx_cnt[t]; j++) {
                int ni = nameidx_start[t] + j; rname_t *rn = &l->v[names[ni]].name; u32 s, nm = rn->id;
                u8 *q; int cnt = 0, e;
                if (rn->str) { for (s = 0; s < nstr; s++) if (rname_eq(strs[s], rn)) break; nm = 0x80000000 | stroff[s]; }
                wr32(p + 16 + 8 * j, nm); wr32(p + 20 + 8 * j, 0x80000000 | diroff_n[ni]);
                q = b + diroff_n[ni];
                for (e = 0; e < no; e++) if (rname_eq(&l->v[order[e]].type, &l->v[types[t]].type) && rname_eq(&l->v[order[e]].name, rn)) {
                    wr32(q + 16 + 8 * cnt, l->v[order[e]].lang); wr32(q + 20 + 8 * cnt, deoff[order[e]]); cnt++;
                }
                wr32(q, 0); wr32(q + 4, 0); wr16(q + 8, 4); wr16(q + 10, 0); wr16(q + 12, 0); wr16(q + 14, cnt);
            }
        }
        for (k = 0; k < (int)nstr; k++) {
            int L = w_len(strs[k]->str), c;
            wr16(b + stroff[k], L);
            for (c = 0; c < L; c++) wr16(b + stroff[k] + 2 + 2 * c, strs[k]->str[c]);
        }
        for (i = 0; i < no; i++) {
            res_t *r = &l->v[order[i]];
            wr32(b + deoff[order[i]], rva + dataoff[order[i]]); wr32(b + deoff[order[i]] + 4, r->size);
            wr32(b + deoff[order[i]] + 8, r->codepage); wr32(b + deoff[order[i]] + 12, 0);
            memcpy(b + dataoff[order[i]], r->data, r->size);
        }
        xfree(order);
    }
    xfree(types); xfree(names); xfree(diroff_t); xfree(diroff_n); xfree(deoff); xfree(dataoff);
    xfree(strs); xfree(stroff); xfree(nameidx_start); xfree(nameidx_cnt);
}

static u32 pe_checksum(const u8 *d, u32 n, u32 csum_off) {
    unsigned long long total = 0; u32 k;
    for (k = 0; k < n; k += 4) {
        u32 w = 0, j;
        if (k == csum_off) continue;
        for (j = 0; j < 4; j++) if (k + j < n) w |= (u32)d[k + j] << (8 * j);
        total += w;
        total = (total & 0xFFFFFFFFull) + (total >> 32);
    }
    total = (total & 0xFFFF) + (total >> 16);
    total = (total & 0xFFFF) + (total >> 16);
    return (u32)((total + n) & 0xFFFFFFFF);
}

BOOL pe_replace_resources(const u8 *raw, u32 n, reslist_t *l, buf_t *out) {
    u32 peo = rd32(raw + 0x3C), opt = peo + 24, nsec = rd16(raw + peo + 6), sh = opt + rd16(raw + peo + 20);
    u32 fa = rd32(raw + opt + 36), sa = rd32(raw + opt + 32), dd = opt + 96;
    u32 vs[16], va[16], rsz[16], rp[16], nva[16], hdr[16], ri = 99, k, lastend = 0, rsrc_rva, rawsize, vnext, ov_delta;
    buf_t nr = { 0 };
    if (nsec > 16 || rd16(raw + opt) != 0x10B) return FALSE;
    rsrc_rva = rd32(raw + dd + 16);
    for (k = 0; k < nsec; k++) {
        hdr[k] = sh + 40 * k;
        vs[k] = rd32(raw + hdr[k] + 8); va[k] = rd32(raw + hdr[k] + 12); rsz[k] = rd32(raw + hdr[k] + 16); rp[k] = rd32(raw + hdr[k] + 20);
        nva[k] = va[k];
        if (va[k] == rsrc_rva) ri = k;
        if (rp[k] + rsz[k] > lastend) lastend = rp[k] + rsz[k];
    }
    if (ri == 99) return FALSE;
    build_rsrc(l, va[ri], &nr);
    out->n = 0;
    buf_put(out, raw, rp[ri]);
    rawsize = align(nr.n, fa);
    buf_put(out, nr.p, nr.n);
    buf_put(out, NULL, rawsize - nr.n);
    wr32(out->p + hdr[ri] + 8, nr.n); wr32(out->p + hdr[ri] + 16, rawsize);
    vnext = align(va[ri] + nr.n, sa);
    for (k = ri + 1; k < nsec; k++) {
        u32 newraw = out->n, span = vs[k] > rsz[k] ? vs[k] : rsz[k], dv = vnext - va[k], e;
        buf_put(out, raw + rp[k], rsz[k]);
        wr32(out->p + hdr[k] + 12, vnext); wr32(out->p + hdr[k] + 20, newraw);
        for (e = 0; e < 16; e++) {
            u32 v = rd32(out->p + dd + 8 * e);
            if (e != 4 && v && v >= va[k] && v < va[k] + span) wr32(out->p + dd + 8 * e, v + dv);
        }
        nva[k] = vnext;
        vnext = align(vnext + span, sa);
    }
    ov_delta = out->n - lastend;
    if (n > lastend) buf_put(out, raw + lastend, n - lastend);
    wr32(out->p + dd + 16, va[ri]); wr32(out->p + dd + 20, nr.n);
    {
        u32 last = nsec - 1, lva, lspan, tot = 0;
        if (last == ri) { lva = va[ri]; lspan = nr.n; }
        else { lva = nva[last]; lspan = vs[last] > rsz[last] ? vs[last] : rsz[last]; }
        wr32(out->p + opt + 56, align(lva + lspan, sa));
        for (k = 0; k < nsec; k++) if (rd32(out->p + hdr[k] + 36) & 0x40) tot += rd32(out->p + hdr[k] + 16);
        wr32(out->p + opt + 8, tot);
    }
    {
        u32 drva = rd32(out->p + dd + 48), dsz = rd32(out->p + dd + 52);
        if (drva) for (k = 0; k < nsec; k++) {
            u32 kvs = rd32(out->p + hdr[k] + 8), kva = rd32(out->p + hdr[k] + 12), krs = rd32(out->p + hdr[k] + 16), krp = rd32(out->p + hdr[k] + 20);
            if (drva >= kva && drva < kva + (kvs > krs ? kvs : krs)) {
                u32 fo = krp + drva - kva, e;
                for (e = 0; e < dsz / 28; e++) {
                    u32 prd = rd32(out->p + fo + 28 * e + 24);
                    if (prd >= lastend) wr32(out->p + fo + 28 * e + 24, prd + ov_delta);
                }
                break;
            }
        }
    }
    wr32(out->p + opt + 64, 0);
    wr32(out->p + opt + 64, pe_checksum(out->p, out->n, opt + 64));
    buf_free(&nr);
    return TRUE;
}

/* ------------------------------------------------------------ string tables */
void st_decode(const u8 *d, u32 n, strblock_t *b) {
    u32 p = 0; int i;
    for (i = 0; i < 16; i++) {
        u32 L = p + 2 <= n ? rd16(d + p) : 0;
        p += 2;
        if (p + 2 * L > n) L = 0;
        b->s[i] = w_from_buf(d + p, L);
        p += 2 * L;
    }
}
void st_encode(const strblock_t *b, buf_t *out) {
    int i, j;
    out->n = 0;
    for (i = 0; i < 16; i++) {
        int L = b->s[i] ? w_len(b->s[i]) : 0;
        put16(out, L);
        for (j = 0; j < L; j++) put16(out, b->s[i][j]);
    }
}
void st_free(strblock_t *b) { int i; for (i = 0; i < 16; i++) { xfree(b->s[i]); b->s[i] = NULL; } }

/* ------------------------------------------------------------ dialogs */
static u32 szord_read(const u8 *d, u32 p, szord_t *s) {
    u16 w = rd16(d + p); u32 e;
    memset(s, 0, sizeof *s);
    if (w == 0xFFFF) { s->is_ord = TRUE; s->ord = rd16(d + p + 2); return p + 4; }
    if (w == 0) return p + 2;
    for (e = p; rd16(d + e); e += 2) ;
    s->s = w_from_buf(d + p, (e - p) / 2);
    return e + 2;
}
static void szord_write(buf_t *b, const szord_t *s) {
    if (s->is_ord) { put16(b, 0xFFFF); put16(b, s->ord); return; }
    if (s->s) { int i, L = w_len(s->s); for (i = 0; i < L; i++) put16(b, s->s[i]); }
    put16(b, 0);
}
BOOL dlg_parse(const u8 *d, u32 n, dlg_t *g) {
    u32 p, cnt, i;
    memset(g, 0, sizeof *g);
    if (n < 18) return FALSE;
    if (rd16(d) == 1 && rd16(d + 2) == 0xFFFF) {
        g->ex = TRUE; g->helpid = rd32(d + 4); g->exstyle = rd32(d + 8); g->style = rd32(d + 12); cnt = rd16(d + 16);
        g->x = rd16(d + 18); g->y = rd16(d + 20); g->cx = rd16(d + 22); g->cy = rd16(d + 24); p = 26;
    } else {
        g->style = rd32(d); g->exstyle = rd32(d + 4); cnt = rd16(d + 8);
        g->x = rd16(d + 10); g->y = rd16(d + 12); g->cx = rd16(d + 14); g->cy = rd16(d + 16); p = 18;
    }
    p = szord_read(d, p, &g->menu); p = szord_read(d, p, &g->cls); p = szord_read(d, p, &g->title);
    if (g->style & 0x40) {
        if (g->ex) { g->pt = rd16(d + p); g->weight = rd16(d + p + 2); g->italic = d[p + 4]; g->charset = d[p + 5]; p += 6; }
        else { g->pt = rd16(d + p); p += 2; }
        p = szord_read(d, p, &g->font);
    }
    g->nitems = cnt;
    g->items = xalloc(sizeof(dlgitem_t) * (cnt + 1));
    for (i = 0; i < cnt; i++) {
        dlgitem_t *it = &g->items[i];
        p = (p + 3) & ~3u;
        if (p + 18 > n) return FALSE;
        if (g->ex) {
            it->helpid = rd32(d + p); it->exstyle = rd32(d + p + 4); it->style = rd32(d + p + 8);
            it->x = rd16(d + p + 12); it->y = rd16(d + p + 14); it->cx = rd16(d + p + 16); it->cy = rd16(d + p + 18); it->id = rd32(d + p + 20); p += 24;
        } else {
            it->style = rd32(d + p); it->exstyle = rd32(d + p + 4);
            it->x = rd16(d + p + 8); it->y = rd16(d + p + 10); it->cx = rd16(d + p + 12); it->cy = rd16(d + p + 14); it->id = rd16(d + p + 16); p += 18;
        }
        p = szord_read(d, p, &it->cls); p = szord_read(d, p, &it->text);
        it->extra_n = rd16(d + p); p += 2;
        if (it->extra_n) { it->extra = xalloc(it->extra_n); memcpy(it->extra, d + p, it->extra_n); p += it->extra_n; }
    }
    return p <= n;
}
void dlg_build(const dlg_t *g, buf_t *b) {
    int i;
    b->n = 0;
    if (g->ex) { put16(b, 1); put16(b, 0xFFFF); put32(b, g->helpid); put32(b, g->exstyle); put32(b, g->style); put16(b, g->nitems); }
    else { put32(b, g->style); put32(b, g->exstyle); put16(b, g->nitems); }
    put16(b, (u16)g->x); put16(b, (u16)g->y); put16(b, (u16)g->cx); put16(b, (u16)g->cy);
    szord_write(b, &g->menu); szord_write(b, &g->cls); szord_write(b, &g->title);
    if (g->style & 0x40) {
        if (g->ex) { u8 t[2]; put16(b, g->pt); put16(b, g->weight); t[0] = g->italic; t[1] = g->charset; buf_put(b, t, 2); }
        else put16(b, g->pt);
        szord_write(b, &g->font);
    }
    for (i = 0; i < g->nitems; i++) {
        const dlgitem_t *it = &g->items[i];
        buf_pad(b, 4);
        if (g->ex) { put32(b, it->helpid); put32(b, it->exstyle); put32(b, it->style); }
        else { put32(b, it->style); put32(b, it->exstyle); }
        put16(b, (u16)it->x); put16(b, (u16)it->y); put16(b, (u16)it->cx); put16(b, (u16)it->cy);
        if (g->ex) put32(b, it->id); else put16(b, it->id);
        szord_write(b, &it->cls); szord_write(b, &it->text);
        put16(b, it->extra_n); if (it->extra_n) buf_put(b, it->extra, it->extra_n);
    }
}
static void szord_free(szord_t *s) { xfree(s->s); s->s = NULL; }
void dlg_free(dlg_t *g) {
    int i;
    for (i = 0; i < g->nitems; i++) { szord_free(&g->items[i].cls); szord_free(&g->items[i].text); xfree(g->items[i].extra); }
    xfree(g->items); szord_free(&g->menu); szord_free(&g->cls); szord_free(&g->title); szord_free(&g->font);
    memset(g, 0, sizeof *g);
}

/* ------------------------------------------------------------ VS_VERSIONINFO */
static u32 vparse(const u8 *d, u32 n, u32 p, vnode_t *v) {
    u32 L = rd16(d + p), VL = rd16(d + p + 2), T = rd16(d + p + 4), end = p + L, q = p + 6, e;
    memset(v, 0, sizeof *v);
    v->type = (u16)T;
    for (e = q; e + 1 < n && rd16(d + e); e += 2) ;
    v->key = w_from_buf(d + q, (e - q) / 2);
    q = (e + 2 + 3) & ~3u;
    if (T == 1) {
        int L2 = VL; wch *t = w_from_buf(d + q, L2);
        while (L2 > 0 && !t[L2 - 1]) t[--L2] = 0;
        v->text = t; q += VL * 2;
    } else { v->bin = xalloc(VL + 1); memcpy(v->bin, d + q, VL); v->bin_n = (u16)VL; q += VL; }
    q = (q + 3) & ~3u;
    while (q < end && q < n) {
        vnode_t k;
        q = vparse(d, n, q, &k);
        v->kids = xrealloc(v->kids, sizeof(vnode_t) * (v->nkids + 1));
        v->kids[v->nkids++] = k;
        q = (q + 3) & ~3u;
    }
    return end;
}
BOOL ver_parse(const u8 *d, u32 n, vnode_t *root) { if (n < 6) return FALSE; vparse(d, n, 0, root); return TRUE; }
static void vbuild(const vnode_t *v, buf_t *b) {
    u32 start = b->n, vl, i; int L = w_len(v->key);
    put16(b, 0); put16(b, 0); put16(b, v->type);
    for (i = 0; i < (u32)L; i++) put16(b, v->key[i]);
    put16(b, 0);
    while ((b->n - start) % 4) buf_put(b, NULL, 1);
    if (v->type == 1) {
        int T = v->text ? w_len(v->text) : 0;
        for (i = 0; i < (u32)T; i++) put16(b, v->text[i]);
        put16(b, 0);
        vl = T ? T + 1 : 1;           /* same as verres.build: empty text -> one NUL, VL=1 */
    } else { buf_put(b, v->bin, v->bin_n); vl = v->bin_n; }
    for (i = 0; i < (u32)v->nkids; i++) { while ((b->n - start) % 4) buf_put(b, NULL, 1); vbuild(&v->kids[i], b); }
    wr16(b->p + start, b->n - start); wr16(b->p + start + 2, vl);
}
void ver_build(const vnode_t *v, buf_t *out) { out->n = 0; vbuild(v, out); }
void ver_free(vnode_t *v) {
    int i;
    for (i = 0; i < v->nkids; i++) ver_free(&v->kids[i]);
    xfree(v->kids); xfree(v->key); xfree(v->text); xfree(v->bin);
    memset(v, 0, sizeof *v);
}
vnode_t *ver_strtable(vnode_t *root) {
    int i;
    for (i = 0; i < root->nkids; i++) if (w_eq_ascii(root->kids[i].key, "StringFileInfo") && root->kids[i].nkids) return &root->kids[i].kids[0];
    return NULL;
}
void ver_set_string(vnode_t *t, const char *key, const wch *value) {
    int i;
    if (!t) return;
    for (i = 0; i < t->nkids; i++) if (w_eq_ascii(t->kids[i].key, key)) { xfree(t->kids[i].text); t->kids[i].text = w_dup(value); return; }
    t->kids = xrealloc(t->kids, sizeof(vnode_t) * (t->nkids + 1));
    memset(&t->kids[t->nkids], 0, sizeof(vnode_t));
    t->kids[t->nkids].key = w_from_ascii(key); t->kids[t->nkids].type = 1; t->kids[t->nkids].text = w_dup(value);
    t->nkids++;
}
const wch *ver_get_string(vnode_t *t, const char *key) {
    int i;
    if (t) for (i = 0; i < t->nkids; i++) if (w_eq_ascii(t->kids[i].key, key)) return t->kids[i].text;
    return NULL;
}
void ver_set_fixed(vnode_t *r, u32 fms, u32 fls, u32 pms, u32 pls) {
    if (r->bin_n < 24 || rd32(r->bin) != 0xFEEF04BD) return;
    wr32(r->bin + 8, fms); wr32(r->bin + 12, fls); wr32(r->bin + 16, pms); wr32(r->bin + 20, pls);
}
BOOL ver_get_fixed(const vnode_t *r, u32 *fms, u32 *fls, u32 *pms, u32 *pls) {
    if (r->bin_n < 24 || rd32(r->bin) != 0xFEEF04BD) return FALSE;
    *fms = rd32(r->bin + 8); *fls = rd32(r->bin + 12); *pms = rd32(r->bin + 16); *pls = rd32(r->bin + 20);
    return TRUE;
}
void ver_set_translation(vnode_t *r, u16 lang, u16 cp) {
    int i;
    for (i = 0; i < r->nkids; i++)
        if (w_eq_ascii(r->kids[i].key, "VarFileInfo") && r->kids[i].nkids && r->kids[i].kids[0].bin_n >= 4) {
            wr16(r->kids[i].kids[0].bin, lang); wr16(r->kids[i].kids[0].bin + 2, cp);
        }
}
