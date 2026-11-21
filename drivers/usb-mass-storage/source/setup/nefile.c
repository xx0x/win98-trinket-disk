/* nefile.c - minimal NE (16-bit Windows executable) helpers */
#include "nefile.h"

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

BOOL ne_find_res(const u8 *d, u32 n, u16 type, u16 id, u32 *off, u32 *len) {
    u32 ne, rt, p, shift;
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return FALSE;
    ne = rd32(d + 0x3C);
    if (ne + 0x40 > n || d[ne] != 'N' || d[ne + 1] != 'E') return FALSE;
    rt = ne + rd16(d + ne + 0x24);
    if (rt + 2 > n) return FALSE;
    shift = rd16(d + rt);
    p = rt + 2;
    while (p + 8 <= n) {
        u16 t = rd16(d + p), cnt = rd16(d + p + 2), i;
        if (!t) break;
        p += 8;
        for (i = 0; i < cnt && p + 12 <= n; i++, p += 12) {
            if (t == (0x8000 | type) && rd16(d + p + 6) == (0x8000 | id)) {
                *off = (u32)rd16(d + p) << shift;
                *len = (u32)rd16(d + p + 2) << shift;
                return *off + *len <= n;
            }
        }
    }
    return FALSE;
}

u32 user_exe_masked_crc(const u8 *d, u32 n) {
    u32 off, len, c;
    u8 *tmp;
    if (!ne_find_res(d, n, 6, 33, &off, &len)) return crc32(d, n);
    tmp = xalloc(n);
    memcpy(tmp, d, n);
    memset(tmp + off, 0, len);
    c = crc32(tmp, n);
    xfree(tmp);
    return c;
}

BOOL user_exe_copy_stamp(u8 *dst, u32 dn, const u8 *src, u32 sn) {
    u32 doff, dlen, soff, slen;
    if (!ne_find_res(dst, dn, 6, 33, &doff, &dlen) || !ne_find_res(src, sn, 6, 33, &soff, &slen)) return FALSE;
    if (dlen != slen) return FALSE;
    memcpy(dst + doff, src + soff, dlen);
    return TRUE;
}

static void stamp_field(u8 *s, const char *v) {     /* s = Pascal string; keep its length, pad with spaces */
    u32 i, l = s[0];
    for (i = 0; i < l; i++) s[1 + i] = (v && *v) ? (u8)*v++ : ' ';
}

BOOL user_exe_set_stamp(u8 *dst, u32 dn, const char *owner, const char *org) {
    u32 off, len, p, i; u8 *f[16];
    if (!ne_find_res(dst, dn, 6, 33, &off, &len)) return FALSE;
    for (i = 0, p = off; i < 16; i++) {                 /* block 33 = strings 512..527 */
        if (p >= off + len) return FALSE;
        f[i] = dst + p; p += 1 + dst[p];
    }
    if (f[2][0] != 30 || f[3][0] != 30 || f[6][0] != 23) return FALSE;   /* unexpected layout */
    stamp_field(f[2], owner);
    stamp_field(f[3], org);
    stamp_field(f[6], "");
    return TRUE;
}
