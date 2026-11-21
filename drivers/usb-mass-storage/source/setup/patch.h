/*
 * patch.h - build localized system files on the target PC.
 *
 * Every builder combines
 *   tmpl   - the English NUSB file (new code, English texts)
 *   local  - the PC's own original (its language), may be empty
 *   pack   - language pack for texts that don't exist in the original, may be NULL
 * and falls back to English for anything it can't get.  Pure data processing.
 */
#ifndef NUSB_PATCH_H
#define NUSB_PATCH_H
#include "base.h"
#include "pe.h"
#include "pack.h"

typedef struct { const u8 *d; u32 n; } blob_t;

enum { P_OK = 0, P_KEEP, P_FAIL };   /* P_KEEP: don't install this file, keep the PC's own */

/* UTF-16 -> ANSI (oem=0) or OEM (oem=1) code page of the target system; returns length */
typedef int (*to_ansi_fn)(const wch *w, char *out, int cap, int oem);
void patch_set_converter(to_ansi_fn f);

u32 ne_code_crc(const u8 *d, u32 n);           /* CRC32 over all segments + relocations */
u32 crc32_upd(u32 crc, const void *d, u32 n);

int patch_user_exe(blob_t tmpl, blob_t local, u32 expected_code_crc, buf_t *out, char *why);
int patch_explorer(blob_t tmpl, blob_t local, const pack_t *pk, buf_t *out, char *why);
int patch_systray(blob_t tmpl, blob_t local, const pack_t *pk, buf_t *out, char *why);
int patch_hotplug(blob_t tmpl, const pack_t *pk, buf_t *out, char *why);
int patch_ios(blob_t tmpl, blob_t local, const pack_t *pk, buf_t *out, char *why);
/* name = INF base name in upper case ("USB", "USBSTOR", ...) - selects pack section [INF.name] */
int patch_inf(blob_t tmpl, blob_t local, const pack_t *pk, const char *name, buf_t *out, char *why);
#endif
