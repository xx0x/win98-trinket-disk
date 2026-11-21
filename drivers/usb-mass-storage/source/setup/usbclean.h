#ifndef NUSB_USBCLEAN_H
#define NUSB_USBCLEAN_H
#include "base.h"
typedef void (*usb_report_fn)(void *ctx, const char *regpath, const char *desc);
/* Returns number of devices found (dry_run) or removed. Exports removed keys to reg_export_path. */
int usb_cleanup(const char *reg_export_path, BOOL dry_run, usb_report_fn report, void *ctx);
#endif
