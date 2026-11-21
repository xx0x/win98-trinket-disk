/*
 * pack.h - language packs for the universal installer.
 *
 * A pack is a UTF-8 text file LXXXX.TXT (XXXX = hex LANGID, e.g. L0405.TXT = Czech):
 *
 *   [PACK]            LangID=0405  CodePage=1250  OEMCodePage=852  Name=...
 *   [SETUP]           installer messages (key=text)
 *   [VERSION]         Comments=...   (written into version info of patched files)
 *   [SYSTRAY.STRINGS] id=text        (string-table entries)
 *   [SYSTRAY.DIALOG.UNSAFEDLG]
 *                     @title=..  @cx=..  @cy=..   n=text  n.x= n.y= n.cx= n.cy=   (n = control index)
 *   [HOTPLUG.STRINGS] / [HOTPLUG.DIALOG.300] / [HOTPLUG.VERSION] ...
 *   [IOS]             1= 2= 3=   (boot-time I/O subsystem errors, converted to the OEM code page)
 *   [INF]             Header=...  (first line written into translated INF files)
 *   [INF.USBSTOR]     key=value  ([Strings] of USBSTOR.INF; same for USB, USB2, NTMAP, ...)
 *   [INF._NUSB]       [Strings] of the install INF _NUSB.INF (Lang, UpdName, SrcDiskName, ...)
 *
 * Values may be wrapped in "..." to keep leading/trailing spaces; escapes: \n \t \\ \"
 * Lines starting with ; or # are comments.  Anything missing falls back to English.
 */
#ifndef NUSB_PACK_H
#define NUSB_PACK_H
#include "base.h"
#include "pe.h"

typedef struct { char *sec, *key, *val; } pent_t;
typedef struct { pent_t *e; int n, cap; u16 lang, cp, oemcp; } pack_t;

BOOL pack_parse(pack_t *p, const char *utf8, u32 n);   /* text may start with a UTF-8 BOM */
void pack_free(pack_t *p);
const char *pack_get(const pack_t *p, const char *sec, const char *key);   /* UTF-8 or NULL */
wch *pack_getw(const pack_t *p, const char *sec, const char *key);         /* owned UTF-16 or NULL */
int pack_has_section(const pack_t *p, const char *sec);
#endif
