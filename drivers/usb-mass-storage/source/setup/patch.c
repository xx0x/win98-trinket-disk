/* patch.c - builders for the universal installer (port of tools/build_*.py, generalized) */
#include "patch.h"

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static to_ansi_fn g_conv;
void patch_set_converter(to_ansi_fn f) { g_conv = f; }

u32 crc32_upd(u32 crc, const void *d, u32 n) {
    static u32 t[256]; static int init;
    const u8 *p = d; u32 c, i, k;
    if (!init) { for (i = 0; i < 256; i++) { c = i; for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1; t[i] = c; } init = 1; }
    c = ~crc;
    while (n--) c = t[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

/* ================================================================ NE (USER.EXE) */
typedef struct { u32 tblpos; u16 type, id; u32 off, len; } neres_t;
typedef struct {
    const u8 *d; u32 n, ne, restab, rshift, nseg, segtab, align;
    neres_t *res; int nres;
} ne_t;

static BOOL ne_open(ne_t *x, const u8 *d, u32 n) {
    u32 p;
    memset(x, 0, sizeof *x);
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return FALSE;
    x->d = d; x->n = n; x->ne = rd32(d + 0x3C);
    if (x->ne + 0x40 > n || d[x->ne] != 'N' || d[x->ne + 1] != 'E') return FALSE;
    x->nseg = rd16(d + x->ne + 0x1C);
    x->segtab = x->ne + rd16(d + x->ne + 0x22);
    x->restab = x->ne + rd16(d + x->ne + 0x24);
    x->align = rd16(d + x->ne + 0x32);
    x->rshift = rd16(d + x->restab);
    x->res = xalloc(sizeof(neres_t) * 512);
    p = x->restab + 2;
    while (p + 8 <= n) {
        u16 t = rd16(d + p), cnt = rd16(d + p + 2), j;
        if (!t) break;
        p += 8;
        for (j = 0; j < cnt && p + 12 <= n && x->nres < 512; j++, p += 12) {
            neres_t *r = &x->res[x->nres++];
            r->tblpos = p; r->type = t; r->id = rd16(d + p + 6);
            r->off = (u32)rd16(d + p) << x->rshift; r->len = (u32)rd16(d + p + 2) << x->rshift;
        }
    }
    return TRUE;
}
static void ne_close(ne_t *x) { xfree(x->res); }

/* resource type/id: numeric (bit 15) or a Pascal string in the resource table */
static int ne_name_eq(const ne_t *a, u16 va, const ne_t *b, u16 vb) {
    if ((va & 0x8000) || (vb & 0x8000)) return va == vb;
    {
        const u8 *pa = a->d + a->restab + va, *pb = b->d + b->restab + vb;
        return pa[0] == pb[0] && !memcmp(pa + 1, pb + 1, pa[0]);
    }
}
static neres_t *ne_find(const ne_t *x, const ne_t *ref, const neres_t *r) {
    int i;
    for (i = 0; i < x->nres; i++)
        if (ne_name_eq(x, x->res[i].type, ref, r->type) && ne_name_eq(x, x->res[i].id, ref, r->id)) return &x->res[i];
    return NULL;
}

u32 ne_code_crc(const u8 *d, u32 n) {
    ne_t x; u32 i, c = 0;
    if (!ne_open(&x, d, n)) return 0;
    for (i = 0; i < x.nseg; i++) {
        const u8 *s = d + x.segtab + 8 * i;
        u32 off = rd16(s), ln = rd16(s + 2), fl = rd16(s + 4), o = off << x.align, l = ln ? ln : 0x10000;
        if (!off) continue;
        if (o + l > n) break;
        c = crc32_upd(c, d + o, l);
        if (fl & 0x100 && o + l + 2 <= n) { u32 rn = rd16(d + o + l); if (o + l + 2 + rn * 8 <= n) c = crc32_upd(c, d + o + l, 2 + rn * 8); }
    }
    ne_close(&x);
    return c;
}

static BOOL fixed_ver(const u8 *d, u32 n, u32 *at) {
    u32 i;
    for (i = 0; i + 24 <= n; i++) if (rd32(d + i) == 0xFEEF04BD) { *at = i; return TRUE; }
    return FALSE;
}

int patch_user_exe(blob_t tmpl, blob_t local, u32 expected_code_crc, buf_t *out, char *why) {
    ne_t e, c; u32 tv, lv, base = 0xFFFFFFFF, rs, i, crc;
    char vold[24], vnew[24];
    if (!local.n) { s_cpy(why, "no original USER.EXE"); return P_KEEP; }
    if (!ne_open(&e, tmpl.d, tmpl.n)) { s_cpy(why, "bad template"); return P_FAIL; }
    if (!ne_open(&c, local.d, local.n)) { ne_close(&e); s_cpy(why, "original is not an NE file"); return P_KEEP; }
    if (!fixed_ver(tmpl.d, tmpl.n, &tv) || !fixed_ver(local.d, local.n, &lv)) { s_cpy(why, "no version info"); goto keep; }
    if (rd32(local.d + lv + 12) != 2222 || rd32(local.d + lv + 8) != 0x0004000A) {
        wsprintfA(why, "original is %u.%u.%u, not 4.10.2222", rd32(local.d + lv + 8) >> 16, rd32(local.d + lv + 8) & 0xFFFF, rd32(local.d + lv + 12) & 0xFFFF);
        goto keep;
    }
    crc = ne_code_crc(local.d, local.n);
    if (expected_code_crc && crc != expected_code_crc) { wsprintfA(why, "unknown USER.EXE build (code CRC %08X)", crc); goto keep; }
    if (e.nres != c.nres) { s_cpy(why, "different resource set"); goto keep; }
    for (i = 0; i < (u32)e.nres; i++) {
        if (!ne_find(&c, &e, &e.res[i])) { s_cpy(why, "different resource set"); goto keep; }
        if (e.res[i].off < base) base = e.res[i].off;
    }
    rs = 1u << e.rshift;
    if (base % rs) { s_cpy(why, "unexpected template layout"); goto keep; }
    wsprintfA(vold, "%u.%u.%u", rd32(local.d + lv + 8) >> 16, rd32(local.d + lv + 8) & 0xFFFF, rd32(local.d + lv + 12) & 0xFFFF);
    wsprintfA(vnew, "%u.%u.%u", rd32(tmpl.d + tv + 8) >> 16, rd32(tmpl.d + tv + 8) & 0xFFFF, rd32(tmpl.d + tv + 12) & 0xFFFF);
    out->n = 0;
    buf_put(out, tmpl.d, base);
    for (i = 0; i < (u32)e.nres; i++) {
        neres_t *src = ne_find(&c, &e, &e.res[i]);
        u32 len = src->len, off;
        u8 *data = xalloc(len + rs);
        memcpy(data, local.d + src->off, len);
        if ((e.res[i].type & 0x7FFF) == 16 && (e.res[i].id & 0x7FFF) == 1) {     /* version: bump to the template's */
            u32 at, k, lo = s_len(vold);
            if (fixed_ver(data, len, &at)) { wr32(data + at + 12, rd32(tmpl.d + tv + 12)); wr32(data + at + 20, rd32(tmpl.d + tv + 20)); }
            for (k = 0; k + lo <= len; k++) if (!memcmp(data + k, vold, lo)) memcpy(data + k, vnew, lo);
        }
        len = (len + rs - 1) / rs * rs;
        off = out->n;
        buf_put(out, data, len);
        wr16(out->p + e.res[i].tblpos, off >> e.rshift);
        wr16(out->p + e.res[i].tblpos + 2, len >> e.rshift);
        xfree(data);
    }
    ne_close(&e); ne_close(&c);
    s_cpy(why, "new code + this PC's texts");
    return P_OK;
keep:
    ne_close(&e); ne_close(&c);
    return P_KEEP;
}

/* ================================================================ PE helpers */
static u16 res_lang(reslist_t *l) { return l->n ? l->v[0].lang : 0x409; }
static void all_lang(reslist_t *l, u16 lang) { int i; for (i = 0; i < l->n; i++) { l->v[i].lang = lang; l->v[i].codepage = 0; } }

static int dlg_struct_eq(const res_t *a, const res_t *b) {
    dlg_t x, y; int ok = 1, i;
    if (!dlg_parse(a->data, a->size, &x)) return 0;
    if (!dlg_parse(b->data, b->size, &y)) { dlg_free(&x); return 0; }
    if (x.nitems != y.nitems) ok = 0;
    for (i = 0; ok && i < x.nitems; i++) {
        dlgitem_t *p = &x.items[i], *q = &y.items[i];
        if (p->id != q->id || (p->style & 0xFFFF0000) != (q->style & 0xFFFF0000) || p->cls.is_ord != q->cls.is_ord) ok = 0;
        else if (p->cls.is_ord ? p->cls.ord != q->cls.ord : (!!p->cls.s != !!q->cls.s)) ok = 0;
    }
    dlg_free(&x); dlg_free(&y);
    return ok;
}

/* string id -> text from a resource list; returns NULL when absent/empty (owned copy) */
static wch *get_string(reslist_t *l, u16 id) {
    res_t *r = res_find(l, 6, (u16)(id / 16 + 1));
    strblock_t b; wch *s = NULL;
    if (!r) return NULL;
    st_decode(r->data, r->size, &b);
    if (b.s[id % 16][0]) s = w_dup(b.s[id % 16]);
    st_free(&b);
    return s;
}
/* apply a callback value to every string of every block */
typedef wch *(*strmap_fn)(void *ctx, u16 id, const wch *cur);
static void map_strings(reslist_t *l, strmap_fn f, void *ctx) {
    int i, k;
    for (i = 0; i < l->n; i++) {
        res_t *r = &l->v[i];
        strblock_t b; buf_t enc = { 0 };
        if (r->type.str || r->type.id != 6 || r->name.str) continue;
        st_decode(r->data, r->size, &b);
        for (k = 0; k < 16; k++) {
            wch *nv = f(ctx, (u16)((r->name.id - 1) * 16 + k), b.s[k]);
            if (nv) { xfree(b.s[k]); b.s[k] = nv; }
        }
        st_encode(&b, &enc);
        res_set_data(r, enc.p, enc.n);
        st_free(&b); buf_free(&enc);
    }
}

/* translate a dialog from pack section e.g. "HOTPLUG.DIALOG.300"; returns 1 if the section exists */
static int dialog_from_pack(res_t *r, const pack_t *pk, const char *sec) {
    dlg_t g; buf_t b = { 0 }; int i; const char *v; char key[16];
    if (!pk || !pack_has_section(pk, sec) || !dlg_parse(r->data, r->size, &g)) return 0;
    if ((v = pack_get(pk, sec, "@title"))) { xfree(g.title.s); g.title.is_ord = FALSE; g.title.s = w_from_utf8(v); }
    if ((v = pack_get(pk, sec, "@cx"))) g.cx = (short)dec_atoi(v);
    if ((v = pack_get(pk, sec, "@cy"))) g.cy = (short)dec_atoi(v);
    for (i = 0; i < g.nitems; i++) {
        dlgitem_t *it = &g.items[i];
        wsprintfA(key, "%d", i);
        if ((v = pack_get(pk, sec, key)) && !it->text.is_ord) { xfree(it->text.s); it->text.s = w_from_utf8(v); }
        wsprintfA(key, "%d.x", i); if ((v = pack_get(pk, sec, key))) it->x = (short)dec_atoi(v);
        wsprintfA(key, "%d.y", i); if ((v = pack_get(pk, sec, key))) it->y = (short)dec_atoi(v);
        wsprintfA(key, "%d.cx", i); if ((v = pack_get(pk, sec, key))) it->cx = (short)dec_atoi(v);
        wsprintfA(key, "%d.cy", i); if ((v = pack_get(pk, sec, key))) it->cy = (short)dec_atoi(v);
    }
    dlg_build(&g, &b);
    res_set_data(r, b.p, b.n);
    buf_free(&b); dlg_free(&g);
    return 1;
}

static void name_of(const res_t *r, char *out) {
    if (r->name.str) { int i; for (i = 0; r->name.str[i] && i < 30; i++) out[i] = (char)r->name.str[i]; out[i] = 0; }
    else wsprintfA(out, "%u", r->name.id);
}

/* localize a version resource: start from 'base' data, set fixed info + strings */
static void version_from(res_t *dst, const res_t *base, const res_t *tmpl_ver, const char *const *copy_keys, const pack_t *pk,
                         const char *extra_sec, BOOL set_key, u16 lang, u16 cp) {
    vnode_t v, t; buf_t b = { 0 }; vnode_t *st, *tst; u32 a, bb, c, d; int i;
    if (!ver_parse(base->data, base->size, &v)) return;
    ver_parse(tmpl_ver->data, tmpl_ver->size, &t);
    if (ver_get_fixed(&t, &a, &bb, &c, &d)) ver_set_fixed(&v, a, bb, c, d);
    st = ver_strtable(&v); tst = ver_strtable(&t);
    for (i = 0; copy_keys && copy_keys[i]; i++) { const wch *s = ver_get_string(tst, copy_keys[i]); if (s) ver_set_string(st, copy_keys[i], s); }
    if (extra_sec && pk) {
        static const char *const keys[] = { "FileDescription", "ProductName", 0 };
        for (i = 0; keys[i]; i++) { wch *s = pack_getw(pk, extra_sec, keys[i]); if (s) { ver_set_string(st, keys[i], s); xfree(s); } }
    }
    if (pk) { wch *s = pack_getw(pk, "VERSION", "Comments"); if (s) { ver_set_string(st, "Comments", s); xfree(s); } }
    if (set_key && st) {
        char k[12]; xfree(st->key); wsprintfA(k, "%04X%04X", lang, cp); st->key = w_from_ascii(k);
        ver_set_translation(&v, lang, cp);
    }
    ver_build(&v, &b);
    res_set_data(dst, b.p, b.n);
    buf_free(&b); ver_free(&v); ver_free(&t);
}

/* ================================================================ EXPLORER.EXE */
int patch_explorer(blob_t tmpl, blob_t local, const pack_t *pk, buf_t *out, char *why) {
    reslist_t e, c; int i, ok = P_OK; res_t *v, *cv;
    static const char *const copy[] = { "FileVersion", "ProductVersion", 0 };
    if (!local.n) { s_cpy(why, "no original EXPLORER.EXE"); return P_KEEP; }
    if (!pe_read_resources(tmpl.d, tmpl.n, &e)) { s_cpy(why, "bad template"); return P_FAIL; }
    if (!pe_read_resources(local.d, local.n, &c)) { reslist_free(&e); s_cpy(why, "original has no resources"); return P_KEEP; }
    for (i = 0; i < e.n && ok == P_OK; i++) {
        res_t *r = &e.v[i], *s; char nm[32];
        if (r->type.str || (r->type.id != 4 && r->type.id != 5 && r->type.id != 6)) continue;
        s = r->name.str ? NULL : res_find(&c, r->type.id, r->name.id);
        name_of(r, nm);
        if (!s) { wsprintfA(why, "original lacks resource %u/%s", r->type.id, nm); ok = P_KEEP; break; }
        if (r->type.id == 5 && !dlg_struct_eq(r, s)) { wsprintfA(why, "dialog %s differs", nm); ok = P_KEEP; break; }
        if (r->type.id == 6) {
            strblock_t a, b; int k;
            st_decode(r->data, r->size, &a); st_decode(s->data, s->size, &b);
            for (k = 0; k < 16; k++) if (!a.s[k][0] != !b.s[k][0]) { wsprintfA(why, "string table %s differs", nm); ok = P_KEEP; }
            st_free(&a); st_free(&b);
            if (ok != P_OK) break;
        }
        res_set_data(r, s->data, s->size);
    }
    if (ok == P_OK && (v = res_find(&e, 16, 1)) && (cv = res_find(&c, 16, 1))) {
        res_t tv = *v; u8 *keep = xalloc(v->size); memcpy(keep, v->data, v->size); tv.data = keep;
        version_from(v, cv, &tv, copy, pk, NULL, FALSE, 0, 0);
        xfree(keep);
    }
    if (ok == P_OK) {
        all_lang(&e, res_lang(&c));
        if (!pe_replace_resources(tmpl.d, tmpl.n, &e, out)) { s_cpy(why, "rebuild failed"); ok = P_FAIL; }
        else s_cpy(why, "new code + this PC's texts");
    }
    reslist_free(&e); reslist_free(&c);
    return ok;
}

/* ================================================================ SYSTRAY.EXE */
typedef struct { reslist_t *loc; reslist_t *en; const pack_t *pk; } trayctx_t;
static int in_list(u16 id, const u16 *l) { for (; *l; l++) if (*l == id) return 1; return 0; }
static wch *tray_map(void *vctx, u16 id, const wch *cur) {
    static const u16 nonloc[] = { 151, 152, 155, 213, 214, 227, 219, 0 };   /* commands / class names: keep English */
    static const u16 changed[] = { 211, 0 };                                /* meaning changed in 2224: tray tooltip */
    trayctx_t *x = vctx; wch *l; char key[8];
    if (!cur[0] || in_list(id, nonloc)) return NULL;
    wsprintfA(key, "%u", id);
    l = (x->loc && !in_list(id, changed)) ? get_string(x->loc, id) : NULL;
    if (l) return l;
    return pack_getw(x->pk, "SYSTRAY.STRINGS", key);
}
int patch_systray(blob_t tmpl, blob_t local, const pack_t *pk, buf_t *out, char *why) {
    reslist_t e, c; trayctx_t x; res_t *r, *s, *v; u16 lang; int have_local;
    static const char *const copy[] = { "FileVersion", "LegalCopyright", 0 };
    if (!pe_read_resources(tmpl.d, tmpl.n, &e)) { s_cpy(why, "bad template"); return P_FAIL; }
    have_local = local.n && pe_read_resources(local.d, local.n, &c);
    if (!have_local) memset(&c, 0, sizeof c);
    x.loc = have_local ? &c : NULL; x.en = &e; x.pk = pk;
    map_strings(&e, tray_map, &x);
    if ((r = res_find(&e, 5, 100))) {                                   /* power meter dialog */
        if (have_local && (s = res_find(&c, 5, 100)) && dlg_struct_eq(r, s)) res_set_data(r, s->data, s->size);
        else dialog_from_pack(r, pk, "SYSTRAY.DIALOG.100");
    }
    if ((r = res_find_named(&e, 5, "UNSAFEDLG"))) dialog_from_pack(r, pk, "SYSTRAY.DIALOG.UNSAFEDLG");
    lang = have_local ? res_lang(&c) : (pk ? pk->lang : 0x409);
    if ((v = res_find(&e, 16, 1))) {
        res_t tv = *v; u8 *keep = xalloc(v->size); memcpy(keep, v->data, v->size); tv.data = keep;
        if (have_local && (s = res_find(&c, 16, 1))) version_from(v, s, &tv, copy, pk, NULL, FALSE, 0, 0);
        else if (pk) version_from(v, &tv, &tv, NULL, pk, NULL, TRUE, pk->lang, pk->cp);
        xfree(keep);
    }
    all_lang(&e, lang);
    if (!pe_replace_resources(tmpl.d, tmpl.n, &e, out)) { reslist_free(&e); if (have_local) reslist_free(&c); s_cpy(why, "rebuild failed"); return P_FAIL; }
    s_cpy(why, have_local ? "new code + this PC's texts + language pack" : "language pack");
    reslist_free(&e); if (have_local) reslist_free(&c);
    return P_OK;
}

/* ================================================================ HOTPLUG.DLL */
static wch *pack_map(void *vpk, u16 id, const wch *cur) {
    char key[8];
    if (!cur[0]) return NULL;
    wsprintfA(key, "%u", id);
    return pack_getw((const pack_t *)vpk, "HOTPLUG.STRINGS", key);
}
int patch_hotplug(blob_t tmpl, const pack_t *pk, buf_t *out, char *why) {
    reslist_t e; int i; res_t *v;
    if (!pe_read_resources(tmpl.d, tmpl.n, &e)) { s_cpy(why, "bad template"); return P_FAIL; }
    if (pk) {
        map_strings(&e, pack_map, (void *)pk);
        for (i = 0; i < e.n; i++) if (!e.v[i].type.str && e.v[i].type.id == 5) {
            char sec[48], nm[32]; name_of(&e.v[i], nm);
            wsprintfA(sec, "HOTPLUG.DIALOG.%s", nm);
            dialog_from_pack(&e.v[i], pk, sec);
        }
        if ((v = res_find(&e, 16, 1))) {
            res_t tv = *v; u8 *keep = xalloc(v->size); memcpy(keep, v->data, v->size); tv.data = keep;
            version_from(v, &tv, &tv, NULL, pk, "HOTPLUG.VERSION", TRUE, pk->lang, pk->cp);
            xfree(keep);
        }
        all_lang(&e, pk->lang);
    }
    if (!pe_replace_resources(tmpl.d, tmpl.n, &e, out)) { reslist_free(&e); s_cpy(why, "rebuild failed"); return P_FAIL; }
    reslist_free(&e);
    s_cpy(why, pk ? "language pack" : "English");
    return P_OK;
}

/* ================================================================ IOS.VXD message table */
static const u8 ios_sig[14] = { 3, 0, 0, 0, 0x14, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
static int ios_block(const u8 *d, u32 n, u32 *at, u32 *len) {
    u32 i; int found = 0;
    for (i = 0; i + 20 <= n; i++) if (!memcmp(d + i, ios_sig, 14)) { *at = i; found++; }
    if (found != 1) return 0;
    *len = 0x14 + rd16(d + *at + 18);
    return *at + *len <= n;
}
static void replace_all(u8 *d, u32 n, const void *a, const void *b, u32 l) {
    u32 i; for (i = 0; i + l <= n; i++) if (!memcmp(d + i, a, l)) memcpy(d + i, b, l);
}
static int ver_lang_code(const u8 *d, u32 n, char *code) {   /* 8 hex digits after "StringFileInfo" (ANSI) */
    u32 i, j;
    for (i = 0; i + 14 + 40 <= n; i++) if (!memcmp(d + i, "StringFileInfo", 14)) {
        for (j = i + 14; j < i + 14 + 40; j++) {
            int k, ok = 1;
            for (k = 0; k < 8; k++) { char ch = d[j + k]; if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))) { ok = 0; break; } }
            if (ok) { memcpy(code, d + j, 8); code[8] = 0; return 1; }
        }
    }
    return 0;
}
int patch_ios(blob_t tmpl, blob_t local, const pack_t *pk, buf_t *out, char *why) {
    u32 eat, elen, lat, llen; u8 *o; char code[12];
    out->n = 0;
    buf_put(out, tmpl.d, tmpl.n);
    o = out->p;
    if (!ios_block(o, out->n, &eat, &elen)) { s_cpy(why, "English messages kept (template layout)"); return P_OK; }
    if (local.n && ios_block(local.d, local.n, &lat, &llen) && llen <= elen) {
        memset(o + eat, 0, elen);
        memcpy(o + eat, local.d + lat, llen);
        if (ver_lang_code(local.d, local.n, code) && code[0]) {
            u8 bin[4]; u16 lg, cpv;
            u32 val = 0; int k;                                       /* code = LLLLCCCC hex */
            for (k = 0; k < 8; k++) { char ch = code[k]; val = val * 16 + (ch <= '9' ? ch - '0' : (ch | 32) - 'a' + 10); }
            lg = (u16)(val >> 16); cpv = (u16)val;
            replace_all(o, out->n, "040904E4", code, 8);
            wr16(bin, lg); wr16(bin + 2, cpv);
            replace_all(o, out->n, "\x09\x04\xE4\x04", bin, 4);
        }
        s_cpy(why, "messages from this PC's IOS.VXD");
        return P_OK;
    }
    if (pk && g_conv && pack_has_section(pk, "IOS")) {
        buf_t b = { 0 }; u32 offs[3], k; char key[4], tmp[600];
        buf_put(&b, ios_sig, 14); buf_put(&b, NULL, 6);
        for (k = 0; k < 3; k++) {
            wch *w; int l;
            wsprintfA(key, "%u", k + 1);
            if (!(w = pack_getw(pk, "IOS", key))) { buf_free(&b); s_cpy(why, "English messages kept"); return P_OK; }
            {   /* \n -> \r\n */
                wch *w2 = xalloc((w_len(w) * 2 + 2) * 2); int i2, o2 = 0;
                for (i2 = 0; w[i2]; i2++) { if (w[i2] == '\n') w2[o2++] = '\r'; w2[o2++] = w[i2]; }
                l = g_conv(w2, tmp, sizeof tmp - 2, 1);
                xfree(w2);
            }
            xfree(w);
            buf_put(&b, tmp, l); buf_put(&b, NULL, 2);
            offs[k] = b.n - 0x14;
        }
        for (k = 0; k < 3; k++) wr16(b.p + 14 + 2 * k, offs[k]);
        if (b.n <= elen) {
            char code[12]; u8 bin[4];
            memset(o + eat, 0, elen); memcpy(o + eat, b.p, b.n);
            wsprintfA(code, "%04X%04X", pk->lang, pk->cp);
            replace_all(o, out->n, "040904E4", code, 8);
            wr16(bin, pk->lang); wr16(bin + 2, pk->cp);
            replace_all(o, out->n, "\x09\x04\xE4\x04", bin, 4);
            s_cpy(why, "messages from language pack");
        }
        else s_cpy(why, "English messages kept (pack text too long)");
        buf_free(&b);
        return P_OK;
    }
    s_cpy(why, "English messages");
    return P_OK;
}

/* ================================================================ INF [Strings] */
typedef struct { char *key, *val; } kv_t;
typedef struct { kv_t *v; int n; } kvlist_t;

static void kv_parse(const u8 *d, u32 n, kvlist_t *l) {   /* [Strings] of an INF, raw ANSI bytes */
    const char *t = (const char *)d, *end = t + n; int in = 0;
    memset(l, 0, sizeof *l);
    l->v = xalloc(sizeof(kv_t) * 2048);
    while (t < end) {
        const char *a = t, *le, *eq, *b, *vs, *ve;
        while (t < end && *t != '\n') t++;
        le = t; if (t < end) t++;
        while (le > a && (le[-1] == '\r')) le--;
        while (a < le && (*a == ' ' || *a == '\t')) a++;
        if (a == le || *a == ';') continue;
        if (*a == '[') { in = !s_nicmp(a, "[strings]", 9); continue; }
        if (!in) continue;
        for (eq = a; eq < le && *eq != '='; eq++) ;
        if (eq == le || l->n >= 2048) continue;
        b = eq; while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
        vs = eq + 1; while (vs < le && (*vs == ' ' || *vs == '\t')) vs++;
        if (vs < le && *vs == '"') { vs++; for (ve = vs; ve < le && *ve != '"'; ve++) ; }
        else { for (ve = vs; ve < le && *ve != ';'; ve++) ; while (ve > vs && (ve[-1] == ' ' || ve[-1] == '\t')) ve--; }
        l->v[l->n].key = xalloc((u32)(b - a) + 1); memcpy(l->v[l->n].key, a, (u32)(b - a));
        l->v[l->n].val = xalloc((u32)(ve - vs) + 1); memcpy(l->v[l->n].val, vs, (u32)(ve - vs));
        l->n++;
    }
}
static void kv_free(kvlist_t *l) { int i; for (i = 0; i < l->n; i++) { xfree(l->v[i].key); xfree(l->v[i].val); } xfree(l->v); }
static const char *kv_get(kvlist_t *l, const char *k) { int i; for (i = 0; i < l->n; i++) if (!s_icmp(l->v[i].key, k)) return l->v[i].val; return NULL; }

/* keys whose meaning NUSB changed - never take them from the PC's original INF */
static int inf_override(const char *name, const char *key) {
    if (!s_icmp(name, "NTMAP")) return s_istarts(key, "Gen") && s_istr(key, "DeviceDesc") != NULL;
    return 0;
}

int patch_inf(blob_t tmpl, blob_t local, const pack_t *pk, const char *name, buf_t *out, char *why) {
    kvlist_t T, L; char sec[40]; const char *t = (const char *)tmpl.d, *end = t + tmpl.n, *hdr;
    int in = 0, from_local = 0, from_pack = 0;
    buf_t lines = { 0 };
    wsprintfA(sec, "INF.%s", name);
    kv_parse(tmpl.d, tmpl.n, &T);
    if (local.n) kv_parse(local.d, local.n, &L); else memset(&L, 0, sizeof L);
    out->n = 0;
    if (pk && g_conv && (hdr = pack_get(pk, "INF", "Header"))) {
        wch *w = w_from_utf8(hdr); char tmp[512]; int l = g_conv(w, tmp, sizeof tmp, 0);
        buf_put(out, tmp, l); buf_puts(out, "\r\n"); xfree(w);
    }
    while (t < end) {
        const char *a = t, *le, *s;
        while (t < end && *t != '\n') t++;
        le = t; if (t < end) t++;
        if (le > a && le[-1] == '\r') le--;
        s = a; while (s < le && (*s == ' ' || *s == '\t')) s++;
        if (s < le && *s == '[') in = !s_nicmp(s, "[strings]", 9);
        else if (in && s < le && *s != ';') {
            const char *eq = s; while (eq < le && *eq != '=') eq++;
            if (eq < le) {
                char key[260], *val = NULL, tmp[1024]; const char *b = eq, *lv, *ev; int kl;
                while (b > s && (b[-1] == ' ' || b[-1] == '\t')) b--;
                kl = (int)(b - s); if (kl > 259) kl = 259;
                memcpy(key, s, kl); key[kl] = 0;
                ev = kv_get(&T, key);
                if (!inf_override(name, key) && (lv = kv_get(&L, key))) { val = s_dup(lv); from_local++; }
                if (!val && ev && L.n && !inf_override(name, key)) {      /* same English value under another key */
                    int k;
                    for (k = 0; k < T.n && !val; k++)
                        if (!s_icmp(T.v[k].val, ev) && (lv = kv_get(&L, T.v[k].key))) { val = s_dup(lv); from_local++; }
                }
                if (!val && pk && g_conv) {
                    wch *w = pack_getw(pk, sec, key);
                    if (w) { int l = g_conv(w, tmp, sizeof tmp - 1, 0); tmp[l] = 0; val = s_dup(tmp); xfree(w); from_pack++; }
                }
                if (!val && ev) val = s_dup(ev);          /* English; line normalized */
                if (val) {
                    buf_put(&lines, s, kl); buf_puts(&lines, "=\""); buf_puts(&lines, val); buf_puts(&lines, "\"\r\n");
                    xfree(val);
                    continue;
                }
            }
        }
        buf_put(&lines, a, (u32)(le - a)); buf_puts(&lines, "\r\n");
    }
    while (lines.n >= 2 && lines.p[lines.n - 1] == '\n' && lines.n >= 4 && lines.p[lines.n - 3] == '\n') lines.n -= 2;   /* single final CRLF */
    buf_put(out, lines.p, lines.n);
    buf_free(&lines); kv_free(&T); if (L.n || L.v) kv_free(&L);
    wsprintfA(why, "%d strings from this PC, %d from language pack", from_local, from_pack);
    return P_OK;
}
