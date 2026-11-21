/*
 * pe.h - PE (Win32) resource engine: read all resources, rebuild the .rsrc section,
 * string tables, dialog templates, VS_VERSIONINFO.  Pure data processing (no Win32 calls),
 * so it is also unit-tested on the build host.  Port of tools/rsrc.py + tools/verres.py.
 */
#ifndef NUSB_PE_H
#define NUSB_PE_H
#include "base.h"

typedef u16 wch;                 /* UTF-16 code unit */

typedef struct {                 /* resource name or type: id, or string when str != NULL */
    u16 id;
    wch *str;                    /* zero-terminated, owned */
} rname_t;

typedef struct {
    rname_t type, name;
    u16 lang;
    u32 codepage;
    u8 *data;                    /* owned */
    u32 size;
} res_t;

typedef struct { res_t *v; int n, cap; } reslist_t;

BOOL pe_read_resources(const u8 *d, u32 n, reslist_t *out);
void reslist_free(reslist_t *l);
res_t *res_find(reslist_t *l, u16 type, u16 id);                 /* numeric type + id */
res_t *res_find_named(reslist_t *l, u16 type, const char *name);  /* numeric type + ASCII name */
void res_set_data(res_t *r, const u8 *d, u32 n);
/* rebuild .rsrc with the given resources; writes a new file image into out */
BOOL pe_replace_resources(const u8 *d, u32 n, reslist_t *l, buf_t *out);

/* ---- wide string helpers ---- */
int w_len(const wch *s);
wch *w_dup(const wch *s);
wch *w_from_ascii(const char *s);
int w_eq_ascii(const wch *w, const char *a);                     /* case-insensitive */
wch *w_from_utf8(const char *s);                                 /* owned */

/* ---- string tables (RT_STRING block = 16 strings) ---- */
typedef struct { wch *s[16]; } strblock_t;
void st_decode(const u8 *d, u32 n, strblock_t *b);
void st_encode(const strblock_t *b, buf_t *out);
void st_free(strblock_t *b);

/* ---- dialogs (DLGTEMPLATE / DLGTEMPLATEEX) ---- */
typedef struct {                  /* sz_Or_Ord */
    BOOL is_ord; u16 ord; wch *s;
} szord_t;
typedef struct {
    u32 helpid, exstyle, style; short x, y, cx, cy; u32 id;
    szord_t cls, text;
    u8 *extra; u16 extra_n;
} dlgitem_t;
typedef struct {
    BOOL ex;
    u32 helpid, exstyle, style; short x, y, cx, cy;
    szord_t menu, cls, title;
    u16 pt, weight; u8 italic, charset; szord_t font;
    int nitems; dlgitem_t *items;
} dlg_t;
BOOL dlg_parse(const u8 *d, u32 n, dlg_t *g);
void dlg_build(const dlg_t *g, buf_t *out);
void dlg_free(dlg_t *g);

/* ---- VS_VERSIONINFO ---- */
typedef struct vnode {
    wch *key; u16 type;            /* 1 = text */
    wch *text;                     /* type 1 */
    u8 *bin; u16 bin_n;            /* type 0 */
    struct vnode *kids; int nkids;
} vnode_t;
BOOL ver_parse(const u8 *d, u32 n, vnode_t *root);
void ver_build(const vnode_t *v, buf_t *out);
void ver_free(vnode_t *v);
vnode_t *ver_strtable(vnode_t *root);                            /* StringFileInfo\<first block> */
void ver_set_string(vnode_t *table, const char *key, const wch *value);  /* add or replace */
const wch *ver_get_string(vnode_t *table, const char *key);
void ver_set_fixed(vnode_t *root, u32 fms, u32 fls, u32 pms, u32 pls);
BOOL ver_get_fixed(const vnode_t *root, u32 *fms, u32 *fls, u32 *pms, u32 *pls);
void ver_set_translation(vnode_t *root, u16 lang, u16 cp);
#endif
