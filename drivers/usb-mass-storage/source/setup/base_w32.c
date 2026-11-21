/* base_w32.c - Win32 parts of base.h: heap, files, log, mem* (no C runtime) */
#include "base.h"
#include <stdarg.h>

static HANDLE g_heap;

void *xalloc(u32 n) {
    void *p;
    if (!g_heap) g_heap = GetProcessHeap();
    p = HeapAlloc(g_heap, HEAP_ZERO_MEMORY, n ? n : 1);
    if (!p) { MessageBoxA(NULL, "Out of memory", "NUSB", MB_ICONSTOP); ExitProcess(3); }
    return p;
}
void *xrealloc(void *p, u32 n) {
    void *q;
    if (!p) return xalloc(n);
    q = HeapReAlloc(g_heap, HEAP_ZERO_MEMORY, p, n ? n : 1);
    if (!q) { MessageBoxA(NULL, "Out of memory", "NUSB", MB_ICONSTOP); ExitProcess(3); }
    return q;
}
void xfree(void *p) { if (p) HeapFree(g_heap, 0, p); }

void *memcpy(void *d, const void *s, unsigned int n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, unsigned int n) {
    u8 *a = d; const u8 *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, unsigned int n) { u8 *a = d; while (n--) *a++ = (u8)c; return d; }
int memcmp(const void *x, const void *y, unsigned int n) {
    const u8 *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}

u8 *file_read(const char *path, u32 *size) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD n, got;
    u8 *p;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    n = GetFileSize(h, NULL);
    p = xalloc(n + 2);
    if (!ReadFile(h, p, n, &got, NULL) || got != n) { CloseHandle(h); xfree(p); return NULL; }
    CloseHandle(h);
    if (size) *size = n;
    return p;
}
BOOL file_write(const char *path, const void *d, u32 n) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w;
    BOOL ok;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(h, d, n, &w, NULL) && w == n;
    CloseHandle(h);
    return ok;
}
BOOL file_exists(const char *path) { return GetFileAttributesA(path) != 0xFFFFFFFF; }
BOOL dir_make(const char *path) {
    char tmp[MAX_PATH]; int i;
    s_cpy(tmp, path);
    for (i = 3; tmp[i]; i++) if (tmp[i] == '\\') { tmp[i] = 0; CreateDirectoryA(tmp, NULL); tmp[i] = '\\'; }
    CreateDirectoryA(tmp, NULL);
    return (GetFileAttributesA(tmp) & FILE_ATTRIBUTE_DIRECTORY) && GetFileAttributesA(tmp) != 0xFFFFFFFF;
}
static HANDLE g_log = INVALID_HANDLE_VALUE;
void log_open(const char *path) {
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log != INVALID_HANDLE_VALUE) SetFilePointer(g_log, 0, NULL, FILE_END);
}
void logmsg(const char *fmt, ...) {
    char line[1200]; DWORD w; va_list ap;
    if (g_log == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    wvsprintfA(line, fmt, ap);
    va_end(ap);
    s_cat(line, "\r\n");
    WriteFile(g_log, line, s_len(line), &w, NULL);
}
void log_close(void) { if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
