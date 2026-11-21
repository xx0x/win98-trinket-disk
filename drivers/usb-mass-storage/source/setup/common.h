/* common.h - install / uninstall machinery of the NUSB 3.6 Universal setup (common.c) */
#ifndef NUSB_COMMON_H
#define NUSB_COMMON_H
#include "base.h"

#define REG_NUSB "Software\\NUSB"
#define REG_UNINST "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NUSB36CZ"

extern char g_win[MAX_PATH], g_src[MAX_PATH], g_stage[MAX_PATH], g_bak[MAX_PATH], g_self[MAX_PATH];

enum { M_TITLE, M_WRONG_OS, M_INSTALLED, M_NO_BACKUP, M_UNINST_Q, M_UNINST_DONE, M_DISPLAYNAME };
const char *M(int id);                         /* texts: implemented by each front end */

int msg(UINT flags, const char *text);
void winpath(char *out, const char *rel);
BOOL check_os(char *desc);
void backup_files(void);
BOOL stage_payload(void);
void clear_stage(void);
void wininit_add(const char *dst, const char *src);
void wininit_flush(void);
BOOL replace_file(const char *src, const char *dst);
BOOL run_inf(const char *dir);
/* stamp owner/organization from the registry into a USER.EXE image (see nefile.h) */
BOOL user_stamp_from_registry(u8 *d, u32 n);
/* installed file newer than the staged one (not NUSB's own): stage the installed file = no change */
BOOL keep_if_newer(const char *rel, const char *staged_name, char *why);
void reg_set(HKEY root, const char *key, const char *name, const char *val);
BOOL installed(void);
void register_install(const char *version);
int do_uninstall(void);
int common_main(const char *version, int (*do_install)(void));
#endif
