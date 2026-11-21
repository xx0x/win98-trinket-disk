/* common.c - install / uninstall machinery of the setup: checks, backup, staging, WININIT.INI,
 * advpack, registration, uninstall. Texts come from setup.c through M(). */
#include "common.h"
#include "nefile.h"
#include "usbclean.h"
#include "manifest.h"

char g_win[MAX_PATH], g_src[MAX_PATH], g_stage[MAX_PATH], g_bak[MAX_PATH], g_self[MAX_PATH];

int msg(UINT flags, const char *text) { return MessageBoxA(NULL, text, M(M_TITLE), flags | MB_SETFOREGROUND); }
void winpath(char *out, const char *rel) { path_join(out, g_win, rel); }

BOOL check_os(char *desc) {
    OSVERSIONINFOA v;
    v.dwOSVersionInfoSize = sizeof v;
    GetVersionExA(&v);
    wsprintfA(desc, "%lu.%lu.%lu%s", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber & 0xFFFF, v.szCSDVersion);
    return v.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS && v.dwMajorVersion == 4 && v.dwMinorVersion == 10 &&
           (v.dwBuildNumber & 0xFFFF) == 2222;
}

/* ------------------------------------------------------------------ backup */

void backup_files(void) {
    char lst[MAX_PATH], full[MAX_PATH], bak[MAX_PATH], line[400];
    buf_t b = { 0 };
    int i, have_list;
    path_join(lst, g_bak, "NUSB.LST");
    have_list = file_exists(lst);       /* reinstall: keep the first, original backup */
    dir_make(g_bak);
    for (i = 0; g_targets[i]; i++) {
        BOOL ex;
        winpath(full, g_targets[i]);
        path_join(bak, g_bak, g_targets[i]);
        ex = file_exists(full);
        if (ex && !file_exists(bak)) {
            char dir[MAX_PATH]; s_cpy(dir, bak); *(char *)path_name(dir) = 0; dir_make(dir);
            if (!CopyFileA(full, bak, TRUE)) logmsg("backup FAILED %s", full);
        }
        wsprintfA(line, "%s|%d\r\n", g_targets[i], ex ? 1 : 0);
        buf_puts(&b, line);
    }
    if (!have_list) file_write(lst, b.p, b.n);
    buf_free(&b);
    logmsg("backup done -> %s (list %s)", g_bak, have_list ? "kept" : "written");
}

/* ------------------------------------------------------------------ staging */

BOOL stage_payload(void) {
    char pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    WIN32_FIND_DATAA fd; HANDLE h; int n = 0;
    dir_make(g_stage);
    path_join(pat, g_src, "*.*");
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        path_join(a, g_src, fd.cFileName);
        path_join(b, g_stage, fd.cFileName);
        if (CopyFileA(a, b, FALSE)) { SetFileAttributesA(b, FILE_ATTRIBUTE_NORMAL); n++; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    logmsg("staged %d files to %s", n, g_stage);
    return n > 0;
}

void clear_stage(void) {
    char pat[MAX_PATH], f[MAX_PATH];
    WIN32_FIND_DATAA fd; HANDLE h;
    path_join(pat, g_stage, "*.*");
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { path_join(f, g_stage, fd.cFileName); DeleteFileA(f); } }
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(g_stage);
}

/* ------------------------------------------------------------------ WININIT.INI */

buf_t g_wininit;
void wininit_add(const char *dst, const char *src) {
    char sd[MAX_PATH], ss[MAX_PATH];
    if (s_icmp(dst, "NUL") && GetShortPathNameA(dst, sd, MAX_PATH)) dst = sd;
    if (GetShortPathNameA(src, ss, MAX_PATH)) src = ss;
    buf_puts(&g_wininit, dst); buf_puts(&g_wininit, "="); buf_puts(&g_wininit, src); buf_puts(&g_wininit, "\r\n");
    logmsg("WININIT %s=%s", dst, src);
}
void wininit_flush(void) {
    char path[MAX_PATH]; u8 *old; u32 n = 0; buf_t out = { 0 };
    const char *hdr;
    if (!g_wininit.n) return;
    winpath(path, "WININIT.INI");
    old = file_read(path, &n);
    if (old && (old[n] = 0, hdr = s_istr((char *)old, "[rename]"))) {
        const char *eol = hdr; u32 cut;
        while (*eol && *eol != '\n') eol++;
        if (*eol) eol++;
        cut = (u32)(eol - (char *)old);
        buf_put(&out, old, cut);
        if (cut && old[cut - 1] != '\n') buf_puts(&out, "\r\n");
        buf_put(&out, g_wininit.p, g_wininit.n);
        buf_put(&out, old + cut, n - cut);
    } else {
        if (old) { buf_put(&out, old, n); buf_puts(&out, "\r\n"); }
        buf_puts(&out, "[rename]\r\n");
        buf_put(&out, g_wininit.p, g_wininit.n);
    }
    file_write(path, out.p, out.n);
    xfree(old); buf_free(&out); buf_free(&g_wininit);
}

/* copy over a possibly in-use file; falls back to WININIT.INI */
BOOL replace_file(const char *src, const char *dst) {
    char tmp[MAX_PATH]; int i;
    SetFileAttributesA(dst, FILE_ATTRIBUTE_NORMAL);
    if (CopyFileA(src, dst, FALSE)) return TRUE;
    s_cpy(tmp, dst); *(char *)path_name(tmp) = 0;
    for (i = 0; i < 1000; i++) {
        char name[16]; char t2[MAX_PATH];
        wsprintfA(name, "NUSB%04d.TMP", i);
        path_join(t2, tmp, name);
        if (!file_exists(t2)) {
            if (!CopyFileA(src, t2, TRUE)) return FALSE;
            wininit_add(dst, t2);
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ advpack */

typedef HRESULT(WINAPI *LAUNCHINFSECTIONEX)(HWND, HINSTANCE, LPSTR, INT);
BOOL run_inf(const char *dir) {
    char cmd[MAX_PATH + 64];
    HMODULE h = LoadLibraryA("ADVPACK.DLL");
    LAUNCHINFSECTIONEX f;
    HRESULT r;
    if (!h || !(f = (LAUNCHINFSECTIONEX)GetProcAddress(h, "LaunchINFSectionEx"))) { logmsg("advpack.dll not usable"); return FALSE; }
    path_join(cmd, dir, "_NUSB.INF");
    s_cat(cmd, ",DefaultInstall,,0,N");       /* flags 0 = normal UI, N = we handle the restart */
    logmsg("LaunchINFSectionEx %s", cmd);
    r = f(NULL, NULL, cmd, SW_SHOWNORMAL);
    logmsg("  -> 0x%08lx", (unsigned long)r);
    FreeLibrary(h);
    return r == S_OK || r == (HRESULT)ERROR_SUCCESS_REBOOT_REQUIRED || r == 0x00000BC2;
}

/* ------------------------------------------------------------------ registry */

void reg_set(HKEY root, const char *key, const char *name, const char *val) {
    HKEY k; DWORD disp;
    if (RegCreateKeyExA(root, key, 0, NULL, 0, KEY_WRITE, NULL, &k, &disp) == ERROR_SUCCESS) {
        RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)val, s_len(val) + 1);
        RegCloseKey(k);
    }
}
BOOL installed(void) {
    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, REG_NUSB, 0, KEY_READ, &k) != ERROR_SUCCESS) return FALSE;
    RegCloseKey(k);
    return TRUE;
}
void register_install(const char *version) {
    char unin[MAX_PATH + 8], selfbak[MAX_PATH];
    path_join(selfbak, g_bak, "NUSBSETUP.EXE");
    CopyFileA(g_self, selfbak, FALSE);
    reg_set(HKEY_LOCAL_MACHINE, REG_NUSB, "Version", version);
    reg_set(HKEY_LOCAL_MACHINE, REG_NUSB, "Backup", g_bak);
    wsprintfA(unin, "%s /U", selfbak);
    reg_set(HKEY_LOCAL_MACHINE, REG_UNINST, "DisplayName", M(M_DISPLAYNAME));
    reg_set(HKEY_LOCAL_MACHINE, REG_UNINST, "UninstallString", unin);
}

/* ------------------------------------------------------------------ uninstall */

int do_uninstall(void) {
    char lst[MAX_PATH], regx[MAX_PATH];
    u8 *d; u32 n; char *p, *line;
    int restored = 0, deleted = 0;
    path_join(lst, g_bak, "NUSB.LST");
    if (!(d = file_read(lst, &n))) {
        msg(MB_ICONSTOP, M(M_NO_BACKUP));
        return 2;
    }
    if (msg(MB_YESNO | MB_ICONQUESTION, M(M_UNINST_Q)) != IDYES) { xfree(d); return 1; }
    d[n] = 0;
    for (p = (char *)d; *p; ) {
        char full[MAX_PATH], bak[MAX_PATH], *bar;
        line = p;
        while (*p && *p != '\r' && *p != '\n') p++;
        while (*p == '\r' || *p == '\n') *p++ = 0;
        if (!(bar = (char *)s_istr(line, "|"))) continue;
        *bar = 0;
        winpath(full, line);
        path_join(bak, g_bak, line);
        if (bar[1] == '1' && file_exists(bak)) { if (replace_file(bak, full)) restored++; }
        else if (bar[1] == '0' && file_exists(full)) {
            SetFileAttributesA(full, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileA(full)) wininit_add("NUL", full);
            deleted++;
        }
    }
    xfree(d);
    {   /* SYSDM.CPL repaired from an English NUSB is left Czech on purpose */
        char f[MAX_PATH];
        winpath(f, "INF\\DRVDATA.BIN"); DeleteFileA(f);      /* driver database is rebuilt */
        winpath(f, "INF\\DRVIDX.BIN"); DeleteFileA(f);
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Active Setup\\Installed Components\\{10e93000-e548-11d3-9741-00500483cae0}");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\USB20U");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Setup\\Updates\\W98.SE\\UPD242975");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, REG_UNINST);
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, REG_NUSB);
    {
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Services\\Class\\USB", 0, KEY_WRITE, &k) == ERROR_SUCCESS) {
            RegDeleteValueA(k, "SilentInstall"); RegDeleteValueA(k, "SilentInstallNotify"); RegCloseKey(k);
        }
    }
    path_join(regx, g_bak, "USB_UNINST.REG");
    usb_cleanup(regx, FALSE, NULL, NULL);       /* controllers get re-detected with the original drivers */
    wininit_flush();
    logmsg("uninstall: %d restored, %d removed", restored, deleted);
    if (msg(MB_YESNO | MB_ICONINFORMATION, M(M_UNINST_DONE)) == IDYES)
        ExitWindowsEx(EWX_REBOOT, 0);
    return 0;
}

/* ------------------------------------------------------------------ entry */

int common_main(const char *version, int (*do_install)(void)) {
    char os[80], logp[MAX_PATH], tmp[MAX_PATH];
    const char *cmd = GetCommandLineA();
    int r, uninstall = s_istr(cmd, "/U") != NULL;

    GetWindowsDirectoryA(g_win, MAX_PATH);
    GetModuleFileNameA(NULL, g_self, MAX_PATH);
    s_cpy(g_src, g_self); *(char *)path_name(g_src) = 0;
    path_join(g_bak, g_win, "NUSB_BAK");
    GetTempPathA(MAX_PATH, tmp);
    path_join(g_stage, tmp, "NUSB_STG");
    path_join(logp, g_win, "NUSB.LOG");
    log_open(logp);
    logmsg("=== NUSBSETUP %s %s, source %s", version, uninstall ? "uninstall" : "install", g_src);

    if (!check_os(os)) {
        char t[400];
        wsprintfA(t, M(M_WRONG_OS), os);
        logmsg("wrong OS %s", os);
        msg(MB_ICONSTOP, t);
        log_close();
        return 2;
    }
    if (!uninstall && installed()) {
        r = msg(MB_YESNOCANCEL | MB_ICONQUESTION, M(M_INSTALLED));
        if (r == IDCANCEL) { log_close(); return 1; }
        if (r == IDNO) uninstall = 1;
    }
    r = uninstall ? do_uninstall() : do_install();
    log_close();
    return r;
}

BOOL user_stamp_from_registry(u8 *d, u32 n) {
    char owner[64] = "", org[64] = ""; HKEY k; DWORD t, l;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        l = sizeof owner; if (RegQueryValueExA(k, "RegisteredOwner", NULL, &t, (BYTE *)owner, &l) != ERROR_SUCCESS || t != REG_SZ) owner[0] = 0;
        l = sizeof org; if (RegQueryValueExA(k, "RegisteredOrganization", NULL, &t, (BYTE *)org, &l) != ERROR_SUCCESS || t != REG_SZ) org[0] = 0;
        RegCloseKey(k);
    }
    owner[sizeof owner - 1] = org[sizeof org - 1] = 0;
    if (!user_exe_set_stamp(d, n, owner, org)) return FALSE;
    logmsg("USER.EXE: registration stamp written from the registry (%s / %s)", owner, org);
    return TRUE;
}

BOOL keep_if_newer(const char *rel, const char *staged_name, char *why) {
    char ip[MAX_PATH], sp[MAX_PATH], a[40], b[40]; u8 *d; u32 n, ims = 0, ils = 0, sms = 0, sls = 0; BOOL ok;
    winpath(ip, rel);
    path_join(sp, g_stage, staged_name);
    if (!(d = file_read(ip, &n))) return FALSE;
    ok = file_version(d, n, &ims, &ils);
    xfree(d);
    if (!ok || !(d = file_read(sp, &n))) return FALSE;
    ok = file_version(d, n, &sms, &sls);
    xfree(d);
    if (!ok || ims < sms || (ims == sms && ils <= sls)) return FALSE;
    if (!CopyFileA(ip, sp, FALSE)) return FALSE;
    ver_str(a, ims, ils); ver_str(b, sms, sls);
    logmsg("%s: installed version %s is newer than %s - kept", rel, a, b);
    if (why) wsprintfA(why, "newer version %s installed - kept", a);
    return TRUE;
}
