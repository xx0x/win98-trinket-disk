/*
 * setup.c - NUSB 3.6 Universal setup (NUSBSETU.EXE).
 *
 * Instead of shipping translated system files, it builds them on this PC:
 *   new code (English NUSB file) + texts of this PC's own original (any language)
 *   + language pack LXXXX.TXT for texts that don't exist in Windows 98 yet, English fallback.
 * Originals are taken from the backup (reinstall), the installed file, or the Windows 98 CD
 * (Setup SourcePath / OPTIONS\CABS, via CABINET.DLL). Every original is verified (version,
 * code checksum, resource layout); a file that can't be built safely is left unchanged.
 * Backup, USB clean-up, advpack install and uninstall live in common.c / usbclean.c.
 */
#include "common.h"
#include "usbclean.h"
#include "nefile.h"
#include "pe.h"
#include "pack.h"
#include "patch.h"
#include "manifest.h"

#define VERSION_STR "3.6 Universal"

/* ------------------------------------------------------------------ texts */
enum { X_LANG = M_DISPLAYNAME + 1, X_NOPACK, X_BADCP, X_HDR, X_G_LOCAL, X_G_CD, X_G_PACK, X_G_EN, X_G_KEEP,
       X_KEEPWARN, X_PLAN, X_STAGEFAIL, X_INFFAIL, X_DONE, X_COUNT };
static const char *const KEYS[X_COUNT] = {
    "TITLE", "WRONG_OS", "INSTALLED", "NO_BACKUP", "UNINST_Q", "UNINST_DONE", "DISPLAYNAME",
    "LANG", "NOPACK", "BADCP", "HDR", "G_LOCAL", "G_CD", "G_PACK", "G_EN", "G_KEEP", "KEEPWARN", "PLAN",
    "STAGEFAIL", "INFFAIL", "DONE" };
static const char *const EN[X_COUNT] = {
    "NUSB 3.6 Universal - USB mass storage setup",
    "This package is for Windows 98 Second Edition (4.10.2222 A) only.\n\nDetected Windows version: %s",
    "NUSB 3.6 is already installed.\n\nYes = reinstall (repair)\nNo = uninstall\nCancel = exit",
    "Backup C:\\WINDOWS\\NUSB_BAK not found - cannot uninstall.",
    "Uninstall NUSB 3.6 and restore the original Windows files from the backup?",
    "The original files will be restored when Windows starts next time.\n\nRestart now?",
    "NUSB 3.6 - USB mass storage drivers",
    "Windows language: %s (%04X), language pack %s.",
    "Windows language %04X: no language pack, new texts will be English.",
    "Language pack %s uses code page %u but Windows uses %u - new texts will be English.",
    "Prepared system files:",
    "From this PC's originals", "From the Windows 98 CD", "Language pack texts", "English texts", "!! LEFT UNCHANGED",
    "Some files can't be built safely from this PC's originals and stay unchanged (details in C:\\WINDOWS\\NUSB.LOG). "
    "USB disks work anyway; only the unplug icon and dialogs may be English or missing.",
    "\nThe setup will back up files to %s, remove %d USB devices (controllers, hubs, disks; keyboards and mice are kept) "
    "and install the drivers.\n\nContinue?",
    "Cannot prepare the setup files.",
    "Driver installation failed. Details: C:\\WINDOWS\\NUSB.LOG",
    "Setup finished.\n\nAfter the restart Windows re-detects the USB controllers and installs the new drivers "
    "(it may ask for the Windows 98 CD). Then restart once more.\n\nRestart now?" };
static char *g_txt[X_COUNT];
static pack_t g_pack;
static const pack_t *g_pk;                 /* NULL = English */
static char g_packfile[MAX_PATH];

const char *M(int id) { return (id >= 0 && id < X_COUNT && g_txt[id]) ? g_txt[id] : (id >= 0 && id < X_COUNT ? EN[id] : ""); }

static int conv(const wch *w, char *out, int cap, int oem) {
    int n = WideCharToMultiByte(oem ? CP_OEMCP : CP_ACP, 0, (LPCWSTR)w, -1, out, cap, NULL, NULL);
    if (n <= 0) { out[0] = 0; return 0; }
    return n - 1;
}

static char g_langline[300];
static void load_pack(char *langline) {
    char pat[MAX_PATH], f[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h;
    LANGID lang = GetSystemDefaultLangID(); u8 *d; u32 n; int i;
    static int loaded;
    if (loaded) { s_cpy(langline, g_langline); return; }
    loaded = 1;
    langline = g_langline;
    g_packfile[0] = 0;
    wsprintfA(f, "L%04X.TXT", lang);
    path_join(pat, g_src, f);
    if (file_exists(pat)) s_cpy(g_packfile, f);
    else {                                     /* same primary language, other sublanguage */
        path_join(pat, g_src, "L*.TXT");
        if ((h = FindFirstFileA(pat, &fd)) != INVALID_HANDLE_VALUE) {
            do {
                u16 l = 0; const char *p = fd.cFileName + 1;
                for (i = 0; i < 4; i++) { char c = p[i]; l = (u16)(l * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10)); }
                if (PRIMARYLANGID(l) == PRIMARYLANGID(lang)) { s_cpy(g_packfile, fd.cFileName); break; }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    langline[0] = 0;
    if (!g_packfile[0]) { wsprintfA(langline, EN[X_NOPACK], lang); logmsg("no language pack for %04x", lang); return; }
    path_join(f, g_src, g_packfile);
    if (!(d = file_read(f, &n)) || !pack_parse(&g_pack, (const char *)d, n)) { xfree(d); return; }
    xfree(d);
    if (g_pack.cp && g_pack.cp != GetACP()) {
        wsprintfA(langline, EN[X_BADCP], g_packfile, g_pack.cp, GetACP());
        logmsg("pack %s code page %u != ACP %u - ignored", g_packfile, g_pack.cp, GetACP());
        return;
    }
    g_pk = &g_pack;
    for (i = 0; i < X_COUNT; i++) {
        wch *w = pack_getw(g_pk, "SETUP", KEYS[i]);
        if (w) { char *a = xalloc(w_len(w) * 2 + 2); conv(w, a, w_len(w) * 2 + 2, 0); g_txt[i] = a; xfree(w); }
    }
    {
        const char *name = pack_get(g_pk, "PACK", "Name"); char nm[80] = "?";
        if (name) { wch *w = w_from_utf8(name); conv(w, nm, sizeof nm, 0); xfree(w); }
        wsprintfA(langline, M(X_LANG), nm, lang, g_packfile);
    }
    logmsg("language pack %s (%04x, cp %u)", g_packfile, g_pack.lang, g_pack.cp);
}

/* ------------------------------------------------------------------ originals */
/* Windows 98 CD originals are read with FDI from CABINET.DLL (part of every Windows 98 install).
 * SETUPAPI's SetupIterateCabinetA on Windows 98 stops with ERROR_INVALID_DATA right after the first
 * extracted file of the WIN98_xx set, so it can't be used. FDI callbacks are __cdecl. */
typedef struct { int erfOper, erfType; BOOL fError; } fdi_erf_t;
typedef struct {
    long cb; char *psz1, *psz2, *psz3; void *pv; INT_PTR hf;
    USHORT date, time, attribs, setID, iCabinet, iFolder; int fdie;
} fdi_note_t;
typedef INT_PTR (__cdecl *fdi_notify_fn)(int, fdi_note_t *);
typedef void *(__cdecl *FDICREATE)(void *, void *, void *, void *, void *, void *, void *, int, fdi_erf_t *);
typedef BOOL (__cdecl *FDICOPY)(void *, char *, char *, int, fdi_notify_fn, void *, void *);
typedef BOOL (__cdecl *FDIDESTROY)(void *);
enum { fdintCABINET_INFO, fdintPARTIAL_FILE, fdintCOPY_FILE, fdintCLOSE_FILE_INFO, fdintNEXT_CABINET, fdintENUMERATE };

/* originals wanted from the Windows 98 CD (spread over the PRECOPY / WIN98_xx cab set) */
static const char *const g_cdwant[] = { "USER.EXE", "EXPLORER.EXE", "SYSTRAY.EXE", "IOS.VXD", "SYSDM.CPL",
                                        "USB.INF", "1394.INF", "NTMAP.INF", "NODRIVER.INF", 0 };
static int g_cdgot[16], g_cdcur = -1;
static long g_cdsize;

static void *__cdecl fdi_alloc(ULONG n) { return HeapAlloc(GetProcessHeap(), 0, n); }
static void __cdecl fdi_free(void *p) { HeapFree(GetProcessHeap(), 0, p); }
static INT_PTR __cdecl fdi_open(char *name, int oflag, int pmode) {
    (void)oflag; (void)pmode;                    /* FDI only opens cabinets, read-only */
    return (INT_PTR)CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
}
static UINT __cdecl fdi_read(INT_PTR h, void *p, UINT n) { DWORD r; return ReadFile((HANDLE)h, p, n, &r, NULL) ? r : (UINT)-1; }
static UINT __cdecl fdi_write(INT_PTR h, void *p, UINT n) { DWORD r; return WriteFile((HANDLE)h, p, n, &r, NULL) ? r : (UINT)-1; }
static int __cdecl fdi_close(INT_PTR h) { return CloseHandle((HANDLE)h) ? 0 : -1; }
static long __cdecl fdi_seek(INT_PTR h, long d, int how) { return (long)SetFilePointer((HANDLE)h, d, NULL, how); }

static void cd_target(char *t, int k) { path_join(t, g_stage, "ORIG_"); s_cat(t, g_cdwant[k]); }
static int cd_left(void) { int k, n = 0; for (k = 0; g_cdwant[k]; k++) n += !g_cdgot[k]; return n; }

static INT_PTR __cdecl fdi_notify(int what, fdi_note_t *f) {
    char t[MAX_PATH]; int k; HANDLE h;
    switch (what) {
    case fdintCOPY_FILE:
        if (!cd_left()) return -1;                              /* all found: stop this cabinet */
        for (k = 0; g_cdwant[k]; k++)
            if (!g_cdgot[k] && !s_icmp(path_name(f->psz1), g_cdwant[k])) {
                cd_target(t, k);
                h = CreateFileA(t, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (h == INVALID_HANDLE_VALUE) return 0;
                g_cdcur = k; g_cdsize = f->cb;
                return (INT_PTR)h;
            }
        return 0;                                                /* skip */
    case fdintCLOSE_FILE_INFO: {
        DWORD sz = GetFileSize((HANDLE)f->hf, NULL);
        CloseHandle((HANDLE)f->hf);
        if (g_cdcur >= 0) {
            cd_target(t, g_cdcur);
            if (sz == (DWORD)g_cdsize) g_cdgot[g_cdcur] = 1;
            else { logmsg("CD: %s incomplete (%lu of %ld bytes)", g_cdwant[g_cdcur], sz, g_cdsize); DeleteFileA(t); }
        }
        g_cdcur = -1;
        return TRUE;
    }
    case fdintNEXT_CABINET:                                     /* continue in the same folder */
        return f->fdie ? -1 : 0;
    default:                                                     /* CABINET_INFO, PARTIAL_FILE, ENUMERATE */
        return 0;
    }
}

/* one pass over every cab in the Windows source folder(s), extracting all wanted originals */
static void cd_prefetch(void) {
    static int done;
    FDICREATE fcreate; FDICOPY fcopy; FDIDESTROY fdestroy; HMODULE h; void *hfdi; fdi_erf_t erf;
    char dirs[2][MAX_PATH], pat[MAX_PATH], dir[MAX_PATH], name[MAX_PATH]; int d, ncab = 0, k, got = 0;
    if (done) return;
    done = 1;
    if (!(h = LoadLibraryA("CABINET.DLL")) || !(fcreate = (FDICREATE)GetProcAddress(h, "FDICreate")) ||
        !(fcopy = (FDICOPY)GetProcAddress(h, "FDICopy")) || !(fdestroy = (FDIDESTROY)GetProcAddress(h, "FDIDestroy"))) {
        logmsg("CD: CABINET.DLL not available"); return;
    }
    memset(&erf, 0, sizeof erf);
    if (!(hfdi = fcreate(fdi_alloc, fdi_free, fdi_open, fdi_read, fdi_write, fdi_close, fdi_seek, -1, &erf))) {
        logmsg("CD: FDICreate failed (%d)", erf.erfOper); return;
    }
    dirs[0][0] = 0;
    {
        HKEY k2; DWORD t, l = MAX_PATH;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Setup", 0, KEY_READ, &k2) == ERROR_SUCCESS) {
            if (RegQueryValueExA(k2, "SourcePath", NULL, &t, (BYTE *)dirs[0], &l) != ERROR_SUCCESS) dirs[0][0] = 0;
            RegCloseKey(k2);
        }
    }
    winpath(dirs[1], "OPTIONS\\CABS");
    for (d = 0; d < 2 && cd_left(); d++) {
        WIN32_FIND_DATAA fd; HANDLE fh; int l;
        if (!dirs[d][0]) continue;
        path_join(pat, dirs[d], "*.CAB");
        if ((fh = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) { logmsg("CD: no cabinets in %s", dirs[d]); continue; }
        s_cpy(dir, dirs[d]);
        if ((l = s_len(dir)) && dir[l - 1] != '\\') s_cat(dir, "\\");
        do {
            if (!s_icmp(fd.cFileName, "MINI.CAB")) continue;   /* setup-time stubs, not the real files */
            s_cpy(name, fd.cFileName);
            fcopy(hfdi, name, dir, 0, fdi_notify, NULL, NULL);  /* FALSE also on our early stop */
            ncab++;
        } while (cd_left() && FindNextFileA(fh, &fd));
        FindClose(fh);
    }
    fdestroy(hfdi);
    for (k = 0; g_cdwant[k]; k++) got += g_cdgot[k];
    logmsg("CD: searched %d cabinets (%s, %s), %d of %d originals extracted", ncab, dirs[0], dirs[1], got, k);
}

static u8 *orig_from_cd(const char *name, u32 *n) {
    char t[MAX_PATH]; u8 *d;
    cd_prefetch();
    path_join(t, g_stage, "ORIG_"); s_cat(t, name);
    if ((d = file_read(t, n))) logmsg("original %s from the Windows CD", name);
    return d;
}

static int ver_is(const u8 *d, u32 n, u32 ms, u32 ls_lo16, int match_build_only) {
    u32 a, b;
    if (!file_version(d, n, &a, &b)) return 0;
    if (match_build_only) return a == ms && (b >> 16) == ls_lo16;
    return a == ms && (b & 0xFFFF) == ls_lo16;
}
static int ok_2222(const u8 *d, u32 n) { return ver_is(d, n, 0x0004000A, 2222, 0); }
static int ok_3110(const u8 *d, u32 n) { return ver_is(d, n, 0x00040048, 3110, 1); }
static int ok_inf(const u8 *d, u32 n) {        /* a Windows original, not an INF from (English) NUSB */
    u8 save = d[n]; int r, i; u32 c = crc32(d, n);
    for (i = 0; g_nusb_inf_crc[i]; i++) if (g_nusb_inf_crc[i] == c) return 0;
    ((u8 *)d)[n] = 0; r = !s_istr((const char *)d, "Maximus"); ((u8 *)d)[n] = save;
    return r;
}

enum { SRC_NONE, SRC_BACKUP, SRC_INSTALLED, SRC_CD };
static u8 *find_original(const char *rel, int (*ok)(const u8 *, u32), u32 *n, int *src) {
    char p[MAX_PATH]; u8 *d;
    path_join(p, g_bak, rel);
    if ((d = file_read(p, n)) && ok(d, *n)) { *src = SRC_BACKUP; return d; }
    xfree(d);
    winpath(p, rel);
    if ((d = file_read(p, n)) && ok(d, *n)) { *src = SRC_INSTALLED; return d; }
    xfree(d);
    if ((d = orig_from_cd(path_name(rel), n)) && ok(d, *n)) { *src = SRC_CD; return d; }
    xfree(d);
    *src = SRC_NONE; *n = 0;
    return NULL;
}

/* ------------------------------------------------------------------ build */
enum { K_USER, K_USER32, K_EXPLORER, K_SYSTRAY, K_HOTPLUG, K_IOS, K_INF };
typedef struct { const char *rel, *file, *inf; int kind; int (*ok)(const u8 *, u32); int result, src, packed; char why[200]; } job_t;
static job_t g_jobs[] = {
    { "SYSTEM\\USER.EXE", "USER.EXE", 0, K_USER, ok_2222 },
    { "SYSTEM\\USER32.DLL", "USER32.DLL", 0, K_USER32, 0 },    /* NUSB file as is, but only together with USER.EXE */
    { "EXPLORER.EXE", "EXPLORER.EXE", 0, K_EXPLORER, ok_3110 },
    { "SYSTEM\\SYSTRAY.EXE", "SYSTRAY.EXE", 0, K_SYSTRAY, ok_2222 },
    { "SYSTEM\\HOTPLUG.DLL", "HOTPLUG.DLL", 0, K_HOTPLUG, 0 },
    { "SYSTEM\\VMM32\\IOS.VXD", "IOS.VXD", 0, K_IOS, ok_2222 },
    { "INF\\USB.INF", "USB.INF", "USB", K_INF, ok_inf },
    { "INF\\1394.INF", "1394.INF", "1394", K_INF, ok_inf },
    { "INF\\NTMAP.INF", "NTMAP.INF", "NTMAP", K_INF, ok_inf },
    { "INF\\NODRIVER.INF", "NODRIVER.INF", "NODRIVER", K_INF, ok_inf },
    { "INF\\USBSTOR.INF", "USBSTOR.INF", "USBSTOR", K_INF, 0 },
    { "INF\\USBNTMAP.INF", "USBNTMAP.INF", "USBNTMAP", K_INF, 0 },
    { "INF\\USB2.INF", "USB2.INF", "USB2", K_INF, 0 },
    { "INF\\_USB2UN.INF", "_USB2UN.INF", "_USB2UN", K_INF, 0 },
    { "_NUSB.INF", "_NUSB.INF", "_NUSB", K_INF, 0 },
};
#define NJOBS (int)(sizeof g_jobs / sizeof g_jobs[0])

static void build_job(job_t *j) {
    char sp[MAX_PATH]; blob_t t, l = { 0, 0 }; u8 *td, *ld = NULL; u32 tn, ln = 0; buf_t out = { 0 };
    path_join(sp, g_stage, j->file);
    if (j->kind == K_USER32 && g_jobs[0].result == P_OK) { j->result = P_OK; return; }
    if (j->kind == K_USER32 ||                                /* USER.EXE and USER32.DLL are one pair */
        (j->kind == K_EXPLORER && GetSystemMetrics(SM_DBCSENABLED))) {
        char ip[MAX_PATH];
        j->result = P_KEEP;
        s_cpy(j->why, j->kind == K_USER32 ? "kept together with USER.EXE"
                                          : "Far East (DBCS) Windows: its own EXPLORER.EXE code is kept");
        winpath(ip, j->rel);
        if (!file_exists(ip) || !CopyFileA(ip, sp, FALSE)) { j->result = P_FAIL; s_cat(j->why, " - copy failed"); }
        logmsg("build %-14s result=%d %s", j->file, j->result, j->why);
        return;
    }
    if (!(td = file_read(sp, &tn))) { j->result = P_FAIL; s_cpy(j->why, "missing in package"); return; }
    t.d = td; t.n = tn;
    if (j->ok && s_icmp(j->rel, "_NUSB.INF")) { ld = find_original(j->rel, j->ok, &ln, &j->src); l.d = ld; l.n = ln; }
    switch (j->kind) {
    case K_USER:     j->result = patch_user_exe(t, l, USER_CODE_CRC_2222, &out, j->why); break;
    case K_EXPLORER: j->result = patch_explorer(t, l, g_pk, &out, j->why); break;
    case K_SYSTRAY:  j->result = patch_systray(t, l, g_pk, &out, j->why); break;
    case K_HOTPLUG:  j->result = patch_hotplug(t, g_pk, &out, j->why); break;
    case K_IOS:      j->result = patch_ios(t, l, g_pk, &out, j->why); break;
    default:         j->result = patch_inf(t, l, g_pk, j->inf, &out, j->why); break;
    }
    j->packed = g_pk && (j->kind == K_HOTPLUG || j->kind == K_INF || (j->kind == K_SYSTRAY && j->src == SRC_NONE));
    if (j->result == P_OK && j->kind == K_USER && j->src == SRC_CD) {     /* keep the registration stamp */
        char ip[MAX_PATH]; u32 in; u8 *id;
        winpath(ip, j->rel);
        if (!((id = file_read(ip, &in)) && ok_2222(id, in) && user_exe_copy_stamp(out.p, out.n, id, in)))
            user_stamp_from_registry(out.p, out.n);         /* PC's USER.EXE is not a stamped original */
        xfree(id);
    }
    if (j->result == P_OK) {
        file_write(sp, out.p, out.n);
        if (j->kind != K_INF && keep_if_newer(j->rel, j->file, j->why))   /* never downgrade: installed file staged */
            j->result = P_KEEP;
    } else if (j->result == P_KEEP) {                    /* install the PC's own file again = no change */
        char ip[MAX_PATH];
        winpath(ip, j->rel);
        if (!file_exists(ip) || !CopyFileA(ip, sp, FALSE)) s_cat(j->why, " (English file used)");
    }
    logmsg("build %-14s result=%d src=%d %s", j->file, j->result, j->src, j->why);
    buf_free(&out); xfree(td); xfree(ld);
}

/* SYSDM.CPL left by the English NUSB (Windows ME version): put the original back */
static void repair_sysdm(void) {
    char p[MAX_PATH], sp[MAX_PATH]; u8 *d; u32 n, crc; int i, src, en = 0;
    winpath(p, "SYSTEM\\SYSDM.CPL");
    if (!(d = file_read(p, &n))) return;
    crc = crc32(d, n); xfree(d);
    for (i = 0; g_known[i].rel; i++) if (!s_icmp(g_known[i].rel, "SYSTEM\\SYSDM.CPL") && g_known[i].crc == crc && !s_icmp(g_known[i].kind, "EN")) en = 1;
    if (!en) return;
    if (!(d = find_original("SYSTEM\\SYSDM.CPL", ok_2222, &n, &src))) { logmsg("SYSDM.CPL: English ME version, no original found"); return; }
    path_join(sp, g_stage, "SYSDM_O.CPL");
    file_write(sp, d, n); xfree(d);
    if (replace_file(sp, p)) logmsg("SYSDM.CPL: original restored (source %d)", src);
}

static void report_usb(void *ctx, const char *path, const char *desc) { (void)ctx; (void)path; (void)desc; }

static int do_install(void) {
    static char text[6144], grp[5][1024];
    char langline[300], tmp[400];
    int i, keep = 0, nusb;
    load_pack(langline);                       /* already loaded in start(); copies the language line */
    patch_set_converter(conv);
    if (!stage_payload()) { msg(MB_ICONSTOP, M(X_STAGEFAIL)); return 2; }
    for (i = 0; i < 5; i++) grp[i][0] = 0;
    for (i = 0; i < NJOBS; i++) {
        job_t *j = &g_jobs[i]; int g;
        build_job(j);
        if (!s_icmp(j->file, "_NUSB.INF") || !s_icmp(j->file, "_USB2UN.INF")) continue;
        if (j->kind == K_USER32 && j->result == P_OK) continue;   /* plain NUSB file, nothing to report */
        if (j->result != P_OK) { g = 4; keep++; }
        else if (j->src == SRC_CD) g = 1;
        else if (j->src == SRC_BACKUP || j->src == SRC_INSTALLED) g = 0;
        else g = g_pk ? 2 : 3;
        if (grp[g][0]) s_cat(grp[g], ", ");
        if (g == 4) { wsprintfA(tmp, "%s (%s)", j->file, j->why); s_cat(grp[g], tmp); }
        else s_cat(grp[g], j->file);
    }
    nusb = usb_cleanup(NULL, TRUE, report_usb, NULL);
    s_cpy(text, langline); s_cat(text, "\n\n"); s_cat(text, M(X_HDR)); s_cat(text, "\n");
    {
        static const int hdr[5] = { X_G_LOCAL, X_G_CD, X_G_PACK, X_G_EN, X_G_KEEP }, order[5] = { 4, 0, 1, 2, 3 };
        for (i = 0; i < 5; i++) {
            int g = order[i];
            if (!grp[g][0]) continue;
            s_cat(text, M(hdr[g])); s_cat(text, ": "); s_cat(text, grp[g]); s_cat(text, "\n");
        }
    }
    if (keep) { s_cat(text, "\n"); s_cat(text, M(X_KEEPWARN)); s_cat(text, "\n"); }
    wsprintfA(tmp, M(X_PLAN), g_bak, nusb);
    s_cat(text, tmp);
    if (msg(MB_YESNO | (keep ? MB_ICONWARNING | MB_DEFBUTTON2 : MB_ICONQUESTION), text) != IDYES) { clear_stage(); return 1; }

    backup_files();
    {
        char regx[MAX_PATH];
        path_join(regx, g_bak, "USB_BAK.REG");
        nusb = usb_cleanup(regx, FALSE, NULL, NULL);
        logmsg("removed %d USB devices (export %s)", nusb, regx);
    }
    repair_sysdm();
    if (!run_inf(g_stage)) { clear_stage(); wininit_flush(); msg(MB_ICONSTOP, M(X_INFFAIL)); return 2; }
    clear_stage();
    wininit_flush();
    register_install(VERSION_STR);
    if (g_packfile[0]) {                       /* the uninstaller in the backup folder speaks the same language */
        char a[MAX_PATH], b[MAX_PATH];
        path_join(a, g_src, g_packfile); path_join(b, g_bak, g_packfile);
        CopyFileA(a, b, FALSE);
    }
    logmsg("install finished");
    if (msg(MB_YESNO | MB_ICONINFORMATION, M(X_DONE)) == IDYES) ExitWindowsEx(EWX_REBOOT, 0);
    return 0;
}

void __stdcall start(void) {
    char tmp[MAX_PATH], line[300];
    /* pack texts are needed already for the first messages (wrong OS, already installed) */
    GetModuleFileNameA(NULL, g_self, MAX_PATH);
    s_cpy(g_src, g_self); *(char *)path_name(g_src) = 0;
    GetTempPathA(MAX_PATH, tmp);
    path_join(g_stage, tmp, "NUSB_STG");
    load_pack(line);
    ExitProcess(common_main(VERSION_STR, do_install));
}
