/*
 * usbclean.c - remove USB host controllers, hubs and USB storage devices from the
 * Windows 98 device tree (what "Remove" in Device Manager does), so Windows
 * re-detects them with the new drivers after a restart.
 *
 * Removed:  Enum\PCI\*   with Class=USB or a USB class code (PCI\CC_0C03..) in its IDs
 *                        (includes driverless "PCI Universal Serial Bus" controllers)
 *           Enum\USB\ROOT_HUB*\*
 *           Enum\USB\*   with Class USB / Unknown / none / hdc / DiskDrive / SCSIAdapter / Storage
 *           Enum\USBSTOR\*
 *           + the driver key Services\Class\<Driver> of each removed device
 *           + the same entries under Config\<profile>\Enum
 * Kept:     HID (keyboards, mice), printers, modems, network, imaging, audio...
 *
 * Every removed key is first exported to a REGEDIT4 file, so it can be restored
 * by double-clicking the file.
 */
#include "base.h"
#include "usbclean.h"

static buf_t g_reg;   /* REGEDIT4 export */

static void reg_escape(buf_t *b, const char *s) {
    for (; *s; s++) {
        if (*s == '\\' || *s == '"') buf_put(b, "\\", 1);
        buf_put(b, s, 1);
    }
}

static void export_key(HKEY root, const char *rootname, const char *path) {
    HKEY k; char name[260]; u8 data[2048]; DWORD i = 0, nl, dl, type, j;
    char tmp[16];
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    buf_puts(&g_reg, "\r\n["); buf_puts(&g_reg, rootname); buf_puts(&g_reg, "\\"); buf_puts(&g_reg, path); buf_puts(&g_reg, "]\r\n");
    for (;;) {
        nl = sizeof name; dl = sizeof data - 1;
        if (RegEnumValueA(k, i++, name, &nl, NULL, &type, data, &dl) != ERROR_SUCCESS) break;
        if (nl == 0) buf_puts(&g_reg, "@=");
        else { buf_puts(&g_reg, "\""); reg_escape(&g_reg, name); buf_puts(&g_reg, "\"="); }
        if (type == REG_SZ) {
            data[dl] = 0;
            buf_puts(&g_reg, "\""); reg_escape(&g_reg, (char *)data); buf_puts(&g_reg, "\"");
        } else if (type == REG_DWORD && dl == 4) {
            wsprintfA(tmp, "dword:%08lx", *(DWORD *)data); buf_puts(&g_reg, tmp);
        } else {
            if (type == REG_BINARY) buf_puts(&g_reg, "hex:");
            else { wsprintfA(tmp, "hex(%lx):", type); buf_puts(&g_reg, tmp); }
            for (j = 0; j < dl; j++) { wsprintfA(tmp, j + 1 < dl ? "%02x," : "%02x", data[j]); buf_puts(&g_reg, tmp); }
        }
        buf_puts(&g_reg, "\r\n");
    }
    /* subkeys (recursive) */
    for (i = 0;; i++) {
        char sub[260], full[1024]; DWORD sl = sizeof sub;
        if (RegEnumKeyExA(k, i, sub, &sl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        wsprintfA(full, "%s\\%s", path, sub);
        export_key(root, rootname, full);
    }
    RegCloseKey(k);
}

static BOOL get_sz(HKEY k, const char *name, char *out, DWORD cap) {
    DWORD type, n = cap - 1;
    out[0] = 0;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)out, &n) != ERROR_SUCCESS || type != REG_SZ) { out[0] = 0; return FALSE; }
    out[n < cap ? n : cap - 1] = 0;
    return TRUE;
}

static int key_has_subkeys(const char *path) {
    HKEY k; char sub[260]; DWORD sl = sizeof sub; int r;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    r = RegEnumKeyExA(k, 0, sub, &sl, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
    RegCloseKey(k);
    return r;
}

static BOOL should_remove(const char *enumerator, const char *devid, const char *cls, const char *ids) {
    if (!s_icmp(enumerator, "USBSTOR")) return TRUE;
    if (!s_icmp(enumerator, "PCI"))
        return !s_icmp(cls, "USB") || s_istr(ids, "CC_0C03") != NULL;
    if (!s_icmp(enumerator, "USB")) {
        if (s_istarts(devid, "ROOT_HUB")) return TRUE;
        return !cls[0] || !s_icmp(cls, "USB") || !s_icmp(cls, "Unknown") || !s_icmp(cls, "hdc") ||
               !s_icmp(cls, "DiskDrive") || !s_icmp(cls, "SCSIAdapter") || !s_icmp(cls, "Storage");
    }
    return FALSE;
}

/* collect first, delete after (deleting while enumerating shifts indexes) */
typedef struct { char path[400]; char driver[64]; char desc[160]; } item_t;

int usb_cleanup(const char *reg_export_path, BOOL dry_run, usb_report_fn report, void *ctx) {
    static const char *enums[] = { "PCI", "USB", "USBSTOR" };
    item_t *items = xalloc(sizeof(item_t) * 256);
    int n = 0, e, i;
    for (e = 0; e < 3; e++) {
        char p1[64]; HKEY k1; DWORD i1;
        wsprintfA(p1, "Enum\\%s", enums[e]);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, p1, 0, KEY_READ, &k1) != ERROR_SUCCESS) continue;
        for (i1 = 0;; i1++) {
            char devid[260], p2[400]; DWORD sl = sizeof devid; HKEY k2; DWORD i2;
            if (RegEnumKeyExA(k1, i1, devid, &sl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            wsprintfA(p2, "%s\\%s", p1, devid);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, p2, 0, KEY_READ, &k2) != ERROR_SUCCESS) continue;
            for (i2 = 0; n < 256; i2++) {
                char inst[260], p3[400], cls[64], hw[1024], cid[1024], ids[2100]; DWORD sl2 = sizeof inst; HKEY k3;
                if (RegEnumKeyExA(k2, i2, inst, &sl2, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
                wsprintfA(p3, "%s\\%s", p2, inst);
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, p3, 0, KEY_READ, &k3) != ERROR_SUCCESS) continue;
                get_sz(k3, "Class", cls, sizeof cls);
                get_sz(k3, "HardwareID", hw, sizeof hw);
                get_sz(k3, "CompatibleIDs", cid, sizeof cid);
                s_cpy(ids, hw); s_cat(ids, ","); s_cat(ids, cid);
                if (should_remove(enums[e], devid, cls, ids)) {
                    s_cpy(items[n].path, p3);
                    get_sz(k3, "Driver", items[n].driver, sizeof items[n].driver);
                    get_sz(k3, "DeviceDesc", items[n].desc, sizeof items[n].desc);
                    n++;
                }
                RegCloseKey(k3);
            }
            RegCloseKey(k2);
        }
        RegCloseKey(k1);
    }
    if (!dry_run && n) {
        g_reg.n = 0;
        buf_puts(&g_reg, "REGEDIT4\r\n\r\n; NUSB: USB devices removed before driver installation\r\n");
        for (i = 0; i < n; i++) {
            char drv[200];
            export_key(HKEY_LOCAL_MACHINE, "HKEY_LOCAL_MACHINE", items[i].path);
            if (items[i].driver[0]) {
                wsprintfA(drv, "System\\CurrentControlSet\\Services\\Class\\%s", items[i].driver);
                export_key(HKEY_LOCAL_MACHINE, "HKEY_LOCAL_MACHINE", drv);
            }
        }
        file_write(reg_export_path, g_reg.p, g_reg.n);
        buf_free(&g_reg);
    }
    for (i = 0; i < n; i++) {
        if (report) report(ctx, items[i].path, items[i].desc);
        logmsg("USB %s: %s [%s] driver=%s", dry_run ? "would remove" : "remove", items[i].path, items[i].desc, items[i].driver);
        if (dry_run) continue;
        if (items[i].driver[0]) {
            char drv[200];
            wsprintfA(drv, "System\\CurrentControlSet\\Services\\Class\\%s", items[i].driver);
            RegDeleteKeyA(HKEY_LOCAL_MACHINE, drv);
        }
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, items[i].path);          /* Win9x deletes subkeys too */
        {   /* hardware profiles: Config\0001\Enum\... */
            int pr;
            for (pr = 1; pr <= 9; pr++) {
                char cp[500]; wsprintfA(cp, "Config\\%04d\\%s", pr, items[i].path);
                RegDeleteKeyA(HKEY_LOCAL_MACHINE, cp);
            }
        }
        {   /* drop the device-id key when its last instance is gone */
            char parent[400]; int l;
            s_cpy(parent, items[i].path);
            for (l = s_len(parent); l > 0 && parent[l] != '\\'; l--) ;
            parent[l] = 0;
            if (!key_has_subkeys(parent)) RegDeleteKeyA(HKEY_LOCAL_MACHINE, parent);
        }
    }
    xfree(items);
    return n;
}
