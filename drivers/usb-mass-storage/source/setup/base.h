/*
 * base.h - freestanding helpers for Windows 98 tools (no C runtime).
 * Only KERNEL32/USER32/ADVAPI32 are used, so binaries run on a stock Windows 98.
 */
#ifndef NUSB_BASE_H
#define NUSB_BASE_H
#include <windows.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* ---- memory ---- */
void *xalloc(u32 n);               /* zeroed, never NULL (aborts) */
void *xrealloc(void *p, u32 n);
void xfree(void *p);
#ifndef NUSB_HOST                 /* host unit tests use the C library versions */
void *memcpy(void *d, const void *s, unsigned int n);
void *memmove(void *d, const void *s, unsigned int n);
void *memset(void *d, int c, unsigned int n);
int memcmp(const void *a, const void *b, unsigned int n);
#endif

/* ---- strings (ANSI) ---- */
int s_len(const char *s);
char *s_cpy(char *d, const char *s);
char *s_cat(char *d, const char *s);
char *s_dup(const char *s);
int s_icmp(const char *a, const char *b);
int s_nicmp(const char *a, const char *b, int n);
const char *s_istr(const char *hay, const char *needle);  /* case-insensitive strstr */
int s_istarts(const char *s, const char *prefix);
int dec_atoi(const char *s);

/* ---- growable byte buffer ---- */
typedef struct { u8 *p; u32 n, cap; } buf_t;
void buf_put(buf_t *b, const void *d, u32 n);
void buf_puts(buf_t *b, const char *s);
void buf_pad(buf_t *b, u32 align);
void buf_free(buf_t *b);

/* ---- files ---- */
u8 *file_read(const char *path, u32 *size);     /* NULL if missing */
BOOL file_write(const char *path, const void *d, u32 n);
BOOL file_exists(const char *path);
BOOL dir_make(const char *path);                 /* creates missing parents */
void path_join(char *out, const char *a, const char *b);
const char *path_name(const char *p);            /* file name part */

/* ---- misc ---- */
u32 crc32(const void *d, u32 n);
/* fixed file version from VS_FIXEDFILEINFO anywhere in a PE/NE/LE file; 0 if none */
BOOL file_version(const u8 *d, u32 n, u32 *ms, u32 *ls);
void ver_str(char *out, u32 ms, u32 ls);         /* "4.10.0.2222" */

/* ---- log (C:\WINDOWS\NUSB.LOG) ---- */
void log_open(const char *path);
void logmsg(const char *fmt, ...);
void log_close(void);

#endif
