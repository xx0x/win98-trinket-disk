// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xgp98.c - XGP98: Xbox, Xbox 360, Xbox One and Xbox Series compatible USB game controllers
 *            for Windows 98 SE.
 *
 * An adaptation of the Linux Xbox gamepad driver (drivers/input/joystick/xpad.c) to Windows 98:
 * the device list, the protocols and the per-controller quirks come from xpad.c; the Windows 98
 * side is a WDM HID minidriver (like Windows 98's own HIDUSB.SYS), so Windows' HID class driver
 * and its joystick support (JOYHID.VXD, VJOYD.VXD) present the pad as a standard game controller.
 *
 * Linux xpad driver:
 *   Copyright (c) 2002 Marko Friedemann <mfr@bmx-chemnitz.de>
 *                 2004 Oliver Schwartz <Oliver.Schwartz@gmx.de>,
 *                      Steven Toth <steve@toth.demon.co.uk>,
 *                      Franz Lehner <franz@caos.at>,
 *                      Ivan Hawkes <blackhawk@ivanhawkes.com>
 *                 2005 Dominic Cerquetti <binary1230@yahoo.com>
 *                 2006 Adam Buchbinder <adam.buchbinder@gmail.com>
 *                 2007 Jan Kratochvil <honza@jikos.cz>
 *                 2010 Christoph Fritz <chf.fritz@googlemail.com>
 *   and the many contributors listed in the Linux kernel history of xpad.c, which is based on
 *   information from ITO Takayuki (euc.jp/periphs/xbox-controller.ja.html), the Free60 project
 *   (Xbox 360) and Pekka Poyry (quantus, Xbox One protocol).
 *
 * Windows 98 adaptation (XGP98): copyright (c) 2026 the win98-trinket-disk contributors.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the
 * GNU General Public License (source/COPYING) for details.
 *
 * What the driver does:
 *   - selects the USB configuration (all interfaces at alternate setting 0, so audio interfaces stay
 *     off) and opens the interrupt IN/OUT pipes of interface 0 (the pad; on the wireless receiver
 *     the first pad slot),
 *   - sends the start-up packets the pad type needs (Xbox One/Series: GIP power on, LED, auth done
 *     and xpad's vendor-specific init packets; Xbox 360: xpad's vendor request and the player LED;
 *     wireless receiver: presence inquiry and LED),
 *   - turns the pad's input packets into reports of a HID game pad, applying xpad's quirks
 *     (sticks off, triggers as buttons, Share button position).
 * Only kernel (NTKERN) functions that Windows 98 SE exports are used and no USBD.SYS functions, so
 * the driver works with the Windows 98 USB stack and with NUSB alike.
 *
 * HID report (15 bytes, no report ID):
 *   0  X  left stick X      0..65535      8  Rx left trigger    0..1023
 *   2  Y  left stick Y      (down = max)  10 Ry right trigger   0..1023
 *   4  Z  right stick X                   12 hat switch (d-pad) 0..7, 8 = centred
 *   6  Rz right stick Y                   13 buttons 1-12: A B X Y LB RB View Menu LS RS Guide Share
 *   (original Xbox: Black = button 5, White = button 6)
 */
#include <ddk/ntddk.h>
#include <usb.h>
#include <usbioctl.h>
#include "devices.h"

/* ------------------------------------------------------------------ no C runtime */
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}

/* ------------------------------------------------------------------ HID minidriver interface (hidport.h) */
#define HID_IOCTL(n) CTL_CODE(FILE_DEVICE_KEYBOARD, (n), METHOD_NEITHER, FILE_ANY_ACCESS)
#define XIOCTL_HID_GET_DEVICE_DESCRIPTOR  HID_IOCTL(0)
#define XIOCTL_HID_GET_REPORT_DESCRIPTOR  HID_IOCTL(1)
#define XIOCTL_HID_READ_REPORT            HID_IOCTL(2)
#define XIOCTL_HID_WRITE_REPORT           HID_IOCTL(3)
#define XIOCTL_HID_GET_STRING             HID_IOCTL(4)
#define XIOCTL_HID_ACTIVATE_DEVICE        HID_IOCTL(7)
#define XIOCTL_HID_DEACTIVATE_DEVICE      HID_IOCTL(8)
#define XIOCTL_HID_GET_DEVICE_ATTRIBUTES  HID_IOCTL(9)

typedef struct {
    ULONG Revision;                   /* HID_REVISION = 1 */
    PDRIVER_OBJECT DriverObject;
    PUNICODE_STRING RegistryPath;
    ULONG DeviceExtensionSize;
    BOOLEAN DevicesArePolled;
    UCHAR Reserved[3];
} HID_MINIDRIVER_REGISTRATION;

typedef struct {                      /* HIDCLASS's extension of the FDO */
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT NextDeviceObject;
    PVOID MiniDeviceExtension;
} HID_DEVICE_EXTENSION;

NTSTATUS NTAPI HidRegisterMinidriver(HID_MINIDRIVER_REGISTRATION *reg);

#pragma pack(push, 1)
typedef struct {
    UCHAR bLength, bDescriptorType;   /* 9, 0x21 */
    USHORT bcdHID;
    UCHAR bCountry, bNumDescriptors;
    UCHAR bReportType;                /* 0x22 */
    USHORT wReportLength;
} HID_DESC;
#pragma pack(pop)

typedef struct {
    ULONG Size;
    USHORT VendorID, ProductID, VersionNumber;
    USHORT Reserved[11];
} HID_ATTR;

static const UCHAR g_report_desc[] = {
    0x05, 0x01,                   /* Usage Page (Generic Desktop) */
    0x09, 0x05,                   /* Usage (Game Pad) */
    0xA1, 0x01,                   /* Collection (Application) */
    0x09, 0x30, 0x09, 0x31,       /*   Usage (X), Usage (Y)       left stick */
    0x09, 0x32, 0x09, 0x35,       /*   Usage (Z), Usage (Rz)      right stick */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x27, 0xFF, 0xFF, 0x00, 0x00, /*   Logical Maximum (65535) */
    0x75, 0x10, 0x95, 0x04,       /*   Report Size (16), Report Count (4) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x09, 0x33, 0x09, 0x34,       /*   Usage (Rx), Usage (Ry)     triggers */
    0x15, 0x00, 0x26, 0xFF, 0x03, /*   Logical 0..1023 */
    0x75, 0x10, 0x95, 0x02,
    0x81, 0x02,
    0x09, 0x39,                   /*   Usage (Hat switch)         d-pad */
    0x15, 0x00, 0x25, 0x07,       /*   Logical 0..7 */
    0x35, 0x00, 0x46, 0x3B, 0x01, /*   Physical 0..315 */
    0x65, 0x14,                   /*   Unit (degrees) */
    0x75, 0x04, 0x95, 0x01,
    0x81, 0x42,                   /*   Input (Data,Var,Abs,Null) */
    0x65, 0x00, 0x45, 0x00,       /*   Unit (none), Physical Maximum (0) */
    0x75, 0x04, 0x95, 0x01,
    0x81, 0x03,                   /*   Input (Const) padding */
    0x05, 0x09,                   /*   Usage Page (Button) */
    0x19, 0x01, 0x29, 0x0C,       /*   Usage Minimum (1), Maximum (12) */
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x0C,
    0x81, 0x02,
    0x75, 0x04, 0x95, 0x01,
    0x81, 0x03,                   /*   padding */
    0xC0                          /* End Collection */
};
#define REPORT_LEN 15
#define XGP_BUILD 11                       /* diagnostics: build number */

/* ------------------------------------------------------------------ device state */
typedef struct {
    PDEVICE_OBJECT next;              /* the USB device below us */
    USBD_PIPE_HANDLE pipe_in, pipe_out;
    USHORT vid, pid, rev;
    LONG seq;                         /* GIP sequence number */
    volatile LONG started;
    volatile LONG neutral_pending;    /* first read after start: report the neutral state at once */
    /* last controller state */
    UCHAR type, map, flags;           /* PAD_*, xpad mapping quirks, FLAG_* */
    volatile LONG init_done;          /* GIP init sent (FLAG_DELAY_INIT pads: after the announce) */
    KTIMER timer; KDPC dpc;           /* state refresh for pads without a heartbeat (all but Xbox One) */
    volatile LONG timer_on;
    volatile LONG reported;           /* a report was delivered since the last tick */
    volatile LONG refresh;            /* the next cancelled read is ours: deliver the current state */
    PIRP volatile cur_irp;            /* HID read waiting in the USB stack */
    LONG player;                      /* Xbox 360 wired: player number 0-31 for the ring LED, -1 none */
    USHORT btn;                       /* HID buttons 1-10 (A B X Y LB RB View Menu LS RS) */
    UCHAR dpad, guide, share;         /* d-pad bits: up, down, left, right */
    USHORT lt, rt;
    SHORT lx, ly, rx, ry;
} DEVEXT;

#define HIDEXT(d) ((HID_DEVICE_EXTENSION *)(d)->DeviceExtension)
#define EXT(d)    ((DEVEXT *)HIDEXT(d)->MiniDeviceExtension)
#define TAG       ' pgX'

#ifdef XGP_DIAG
/* diagnostics (build with -DXGP_DIAG): HKLM\Software\XGP98\<name> = DWORD; the key must exist */
static void dlog(PCWSTR name, ULONG v) {
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, L"\\Registry\\Machine\\Software\\XGP98", name, REG_DWORD, &v, sizeof v);
}

/* read-path counters, written to the registry by a work item (completion routines run at DISPATCH_LEVEL) */
/* explicitly initialized + -fno-zero-initialized-in-bss: no .bss section (it would break the
 * file-offset = RVA layout the Windows 98 driver loader expects) */
static volatile LONG g_reads = 0, g_done = 0, g_inputs = 0, g_log_pending = 0;
static volatile ULONG g_last_status = 0, g_last_len = 0;
static UCHAR g_last_pkt[16] = { 0 };
static WORK_QUEUE_ITEM g_log_item = { { 0 } };

static void NTAPI log_worker(PVOID ctx) {
    (void)ctx;
    InterlockedExchange(&g_log_pending, 0);
    dlog(L"Reads", g_reads); dlog(L"Completions", g_done); dlog(L"Inputs", g_inputs);
    dlog(L"LastStatus", g_last_status); dlog(L"LastLen", g_last_len);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, L"\\Registry\\Machine\\Software\\XGP98", L"LastPacket",
                          REG_BINARY, g_last_pkt, sizeof g_last_pkt);
}

static void log_kick(void) {
    if (InterlockedExchange(&g_log_pending, 1) == 0) {
        ExInitializeWorkItem(&g_log_item, log_worker, NULL);
        ExQueueWorkItem(&g_log_item, DelayedWorkQueue);
    }
}

#else
#define dlog(name, v) ((void)0)
#define log_kick() ((void)0)
#endif

static USHORT rd16(const UCHAR *p) { return (USHORT)(p[0] | (p[1] << 8)); }
static void wr16(UCHAR *p, USHORT v) { p[0] = (UCHAR)v; p[1] = (UCHAR)(v >> 8); }

/* ------------------------------------------------------------------ GIP packets */
enum { PAD_XBOX, PAD_360, PAD_360W, PAD_ONE };        /* = xpad's XTYPE_* (devices.h) */
#define MAP_TRIGGERS_TO_BUTTONS 0x02                   /* xpad mapping quirks */
#define MAP_STICKS_TO_NULL      0x04
#define MAP_SHARE_BUTTON        0x08
#define MAP_SHARE_OFFSET        0x40
#define FLAG_DELAY_INIT         0x01                   /* send the init packets after the announce */

/* Xbox One / Series (GIP): xpad's xboxone_init_packets, in order; byte 2 = sequence number */
static const UCHAR gip_hori_ack_id[] = { 0x01, 0x20, 0x00, 0x09, 0x00, 0x04, 0x20, 0x3a, 0x00, 0x00, 0x00, 0x80, 0x00 };
static const UCHAR gip_power_on[]    = { 0x05, 0x20, 0x00, 0x01, 0x00 };
static const UCHAR gip_s_init[]      = { 0x05, 0x20, 0x00, 0x0f, 0x06 };
static const UCHAR gip_extra_input[] = { 0x4d, 0x10, 0x01, 0x02, 0x07, 0x00 };
static const UCHAR gip_led_on[]      = { 0x0a, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14 };
static const UCHAR gip_auth_done[]   = { 0x06, 0x20, 0x00, 0x02, 0x01, 0x00 };
static const UCHAR gip_rumble_begin[] = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0f, 0x00, 0x00, 0x1d, 0x1d, 0xff, 0x00, 0x00 };
static const UCHAR gip_rumble_end[]  = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
typedef struct { USHORT vid, pid; const UCHAR *data; UCHAR len; } GIP_INIT;
#define GI(v, p, a) { v, p, a, sizeof a }
static const GIP_INIT g_gip_init[] = {
    GI(0x0e6f, 0x0165, gip_hori_ack_id), GI(0x0f0d, 0x0067, gip_hori_ack_id),
    GI(0, 0, gip_power_on),
    GI(0x045e, 0x02ea, gip_s_init), GI(0x045e, 0x0b00, gip_s_init), GI(0x045e, 0x0b00, gip_extra_input),
    GI(0, 0, gip_led_on), GI(0, 0, gip_auth_done),
    GI(0x24c6, 0x541a, gip_rumble_begin), GI(0x24c6, 0x542a, gip_rumble_begin), GI(0x24c6, 0x543a, gip_rumble_begin),
    GI(0x24c6, 0x541a, gip_rumble_end), GI(0x24c6, 0x542a, gip_rumble_end), GI(0x24c6, 0x543a, gip_rumble_end),
};
static const UCHAR gip_guide_ack[]   = { 0x01, 0x20, 0x00, 0x09, 0x00, 0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* Xbox 360 wired pads get player numbers like xpad's xpad_led_probe: the lowest free one, kept until the
 * pad is removed; the ring LED shows (number % 4) + 1 (LED command (number % 4) + 2: flash, then on) */
static volatile LONG g_players = 0;      /* bit n = player number n in use */

static LONG player_alloc(void) {
    LONG i;
    for (i = 0; i < 32; i++) {
        LONG bit = 1L << i;
        if (!(__sync_fetch_and_or(&g_players, bit) & bit)) return i;
    }
    return -1;
}

static void player_free(DEVEXT *e) {
    if (e->player >= 0) __sync_fetch_and_and(&g_players, ~(1L << e->player));
    e->player = -1;
}

/* Xbox 360 wireless receiver: pad presence inquiry and LED "player 1" */
static const UCHAR x360w_presence[]  = { 0x08, 0x00, 0x0f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const UCHAR x360w_led_p1[]    = { 0x00, 0x00, 0x08, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* ------------------------------------------------------------------ USB helpers */
static void build_xfer(struct _URB_BULK_OR_INTERRUPT_TRANSFER *u, USBD_PIPE_HANDLE pipe, BOOLEAN in, void *buf, ULONG len) {
    memset(u, 0, sizeof *u);
    u->Hdr.Length = sizeof *u;
    u->Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    u->PipeHandle = pipe;
    u->TransferFlags = in ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : USBD_TRANSFER_DIRECTION_OUT;
    u->TransferBufferLength = len;
    u->TransferBuffer = buf;
}

static void set_urb_ioctl(PIRP irp, PVOID urb) {
    PIO_STACK_LOCATION s = IoGetNextIrpStackLocation(irp);
    s->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    s->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    s->Parameters.Others.Argument1 = urb;
}

/* synchronous URB (PASSIVE_LEVEL only), 2 s timeout */
static NTSTATUS urb_sync(PDEVICE_OBJECT next, PVOID urb) {
    KEVENT ev; IO_STATUS_BLOCK io; PIRP irp; NTSTATUS st; LARGE_INTEGER to;
    KeInitializeEvent(&ev, NotificationEvent, FALSE);
    irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB, next, NULL, 0, NULL, 0, TRUE, &ev, &io);
    if (!irp) return STATUS_INSUFFICIENT_RESOURCES;
    IoGetNextIrpStackLocation(irp)->Parameters.Others.Argument1 = urb;
    st = IoCallDriver(next, irp);
    if (st == STATUS_PENDING) {
        to.QuadPart = -20000000LL;
        if (KeWaitForSingleObject(&ev, Executive, KernelMode, FALSE, &to) == STATUS_TIMEOUT) {
            IoCancelIrp(irp);
            KeWaitForSingleObject(&ev, Executive, KernelMode, FALSE, NULL);
        }
        st = io.Status;
    }
    return st;
}

/* asynchronous OUT packet, any IRQL <= DISPATCH_LEVEL */
typedef struct { struct _URB_BULK_OR_INTERRUPT_TRANSFER urb; UCHAR buf[64]; } OUTCTX;

static NTSTATUS NTAPI out_done(PDEVICE_OBJECT d, PIRP irp, PVOID ctx) {
    (void)d;
    ExFreePool(ctx);
    IoFreeIrp(irp);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static void send_async(DEVEXT *e, const UCHAR *pkt, ULONG len, UCHAR seq) {
    OUTCTX *c; PIRP irp;
    if (!e->pipe_out || len > sizeof c->buf) return;
    if (!(c = ExAllocatePoolWithTag(NonPagedPool, sizeof *c, TAG))) return;
    if (!(irp = IoAllocateIrp(e->next->StackSize, FALSE))) { ExFreePool(c); return; }
    memcpy(c->buf, pkt, len);
    c->buf[2] = seq;
    build_xfer(&c->urb, e->pipe_out, FALSE, c->buf, len);
    set_urb_ioctl(irp, &c->urb);
    IoSetCompletionRoutine(irp, out_done, c, TRUE, TRUE, TRUE);
    IoCallDriver(e->next, irp);
}

static NTSTATUS send_raw_sync(DEVEXT *e, const UCHAR *pkt, ULONG len) {
    struct _URB_BULK_OR_INTERRUPT_TRANSFER u; UCHAR buf[64];
    memcpy(buf, pkt, len);
    build_xfer(&u, e->pipe_out, FALSE, buf, len);
    return urb_sync(e->next, &u);
}

static NTSTATUS send_sync(DEVEXT *e, const UCHAR *pkt, ULONG len) {
    struct _URB_BULK_OR_INTERRUPT_TRANSFER u; UCHAR buf[64];
    memcpy(buf, pkt, len);
    buf[2] = (UCHAR)(InterlockedIncrement(&e->seq) - 1);
    build_xfer(&u, e->pipe_out, FALSE, buf, len);
    return urb_sync(e->next, &u);
}

/* ------------------------------------------------------------------ start: configuration + GIP init */
/* Pads other than Xbox One/Series send nothing until touched and have no heartbeat. Every 2 s, if no
 * report went out, the timer cancels the HID read waiting in the USB stack; read_done turns that
 * cancel into a report of the current state, so a program opening the joystick later gets a valid
 * report. (Windows 98 is single-processor: this DPC and the USB completion routines never overlap.) */
static void timer_arm(DEVEXT *e) {
    LARGE_INTEGER t; t.QuadPart = -20000000LL;
    KeSetTimer(&e->timer, t, &e->dpc);
}

static VOID NTAPI timer_tick(PKDPC dpc, PVOID ctx, PVOID a1, PVOID a2) {
    DEVEXT *e = ctx;
    (void)dpc; (void)a1; (void)a2;
    if (!e->started || !e->timer_on) return;
    if (!InterlockedExchange(&e->reported, 0) && e->cur_irp && !e->refresh) {
        e->refresh = 1;
        IoCancelIrp(e->cur_irp);
    }
    timer_arm(e);
}

static void timer_stop(DEVEXT *e) {
    if (InterlockedExchange(&e->timer_on, 0)) KeCancelTimer(&e->timer);
}

/* GIP start-up sequence: every packet of xpad's xboxone_init_packets that applies to this pad.
 * async = TRUE when called from a completion routine (DISPATCH_LEVEL) */
static void gip_init(DEVEXT *e, BOOLEAN async) {
    ULONG i;
    if (InterlockedExchange(&e->init_done, 1)) return;
    for (i = 0; i < sizeof g_gip_init / sizeof g_gip_init[0]; i++) {
        const GIP_INIT *g = &g_gip_init[i];
        if (g->vid && (g->vid != e->vid || g->pid != e->pid)) continue;
        if (async) send_async(e, g->data, g->len, (UCHAR)(InterlockedIncrement(&e->seq) - 1));
        else send_sync(e, g->data, g->len);
    }
}

/* xpad_start_input: "some third-party controllers Xbox 360-style controllers require this message to
 * finish initialization" - vendor IN request 1, value 0x100, index 0, 20 bytes; errors are ignored */
static void x360_vendor_request(DEVEXT *e) {
    struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST u; UCHAR buf[20];
    memset(&u, 0, sizeof u);
    u.Hdr.Length = sizeof u;
    u.Hdr.Function = URB_FUNCTION_VENDOR_INTERFACE;
    u.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    u.TransferBufferLength = sizeof buf;
    u.TransferBuffer = buf;
    u.Request = 0x01;
    u.Value = 0x100;
    u.Index = 0;
    urb_sync(e->next, &u);
}

/* = GET_USBD_INTERFACE_SIZE(n) of the DDK (16 bytes for an interface without endpoints); USBD walks the
 * interface list with exactly this size */
#define IFACE_SIZE(n) (sizeof(USBD_INTERFACE_INFORMATION) + (n) * sizeof(USBD_PIPE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION))

static NTSTATUS get_descriptor(PDEVICE_OBJECT next, UCHAR type, void *buf, ULONG len, ULONG *got) {
    struct _URB_CONTROL_DESCRIPTOR_REQUEST u; NTSTATUS st;
    memset(&u, 0, sizeof u);
    u.Hdr.Length = sizeof u;
    u.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    u.DescriptorType = type;
    u.TransferBuffer = buf;
    u.TransferBufferLength = len;
    st = urb_sync(next, &u);
    if (got) *got = u.TransferBufferLength;
    return st;
}

static NTSTATUS start_usb(DEVEXT *e) {
    UCHAR dd[18], head[9]; UCHAR *cfg = NULL, *p, *end; ULONG got, total, size, n_if = 0;
    struct _URB_SELECT_CONFIGURATION *sel = NULL; PUSBD_INTERFACE_INFORMATION ii; NTSTATUS st; ULONG i, k;

    e->started = 0; e->pipe_in = e->pipe_out = NULL;
    st = get_descriptor(e->next, USB_DEVICE_DESCRIPTOR_TYPE, dd, sizeof dd, &got);
    dlog(L"DevDesc", st); dlog(L"DevDescLen", got);
    if (!NT_SUCCESS(st) || got < sizeof dd) return NT_SUCCESS(st) ? STATUS_DEVICE_DATA_ERROR : st;
    e->vid = rd16(dd + 8); e->pid = rd16(dd + 10); e->rev = rd16(dd + 12);
    dlog(L"VidPid", ((ULONG)e->vid << 16) | e->pid);

    st = get_descriptor(e->next, USB_CONFIGURATION_DESCRIPTOR_TYPE, head, sizeof head, &got);
    if (!NT_SUCCESS(st) || got < sizeof head) return NT_SUCCESS(st) ? STATUS_DEVICE_DATA_ERROR : st;
    total = rd16(head + 2);
    dlog(L"CfgHead", st); dlog(L"CfgTotal", total);
    if (total < 9 || total > 4096) return STATUS_DEVICE_DATA_ERROR;
    if (!(cfg = ExAllocatePoolWithTag(NonPagedPool, total, TAG))) return STATUS_INSUFFICIENT_RESOURCES;
    st = get_descriptor(e->next, USB_CONFIGURATION_DESCRIPTOR_TYPE, cfg, total, &got);
    if (!NT_SUCCESS(st) || got < total) { st = NT_SUCCESS(st) ? STATUS_DEVICE_DATA_ERROR : st; goto out; }

    /* every interface at alternate setting 0 */
    size = sizeof(struct _URB_SELECT_CONFIGURATION) - sizeof(USBD_INTERFACE_INFORMATION);
    for (p = cfg, end = cfg + total; p + 2 <= end && p[0] >= 2; p += p[0])
        if (p[1] == USB_INTERFACE_DESCRIPTOR_TYPE && p[0] >= 9 && p[3] == 0) { size += IFACE_SIZE(p[4]); n_if++; }
    if (!n_if) { st = STATUS_DEVICE_DATA_ERROR; goto out; }
    if (!(sel = ExAllocatePoolWithTag(NonPagedPool, size, TAG))) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
    memset(sel, 0, size);
    sel->Hdr.Length = (USHORT)size;
    sel->Hdr.Function = URB_FUNCTION_SELECT_CONFIGURATION;
    sel->ConfigurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)cfg;
    ii = &sel->Interface;
    for (p = cfg; p + 2 <= end && p[0] >= 2; p += p[0]) {
        if (p[1] != USB_INTERFACE_DESCRIPTOR_TYPE || p[0] < 9 || p[3] != 0) continue;
        ii->Length = (USHORT)IFACE_SIZE(p[4]);
        ii->InterfaceNumber = p[2];
        ii->AlternateSetting = 0;
        ii->NumberOfPipes = p[4];
        for (k = 0; k < p[4]; k++) { ii->Pipes[k].MaximumTransferSize = 4096; ii->Pipes[k].PipeFlags = 0; }
        ii = (PUSBD_INTERFACE_INFORMATION)((UCHAR *)ii + ii->Length);
    }
    dlog(L"Interfaces", n_if);
    st = urb_sync(e->next, sel);
    dlog(L"SelectConfig", st); dlog(L"SelectUrb", sel->Hdr.Status);
    if (!NT_SUCCESS(st)) goto out;

    /* interrupt pipes of the GIP data interface (number 0, else the first one) */
    ii = &sel->Interface;
    for (i = 0; i < n_if; i++, ii = (PUSBD_INTERFACE_INFORMATION)((UCHAR *)ii + ii->Length)) {
        if (ii->InterfaceNumber != 0 && i != 0) continue;
        for (k = 0; k < ii->NumberOfPipes; k++) {
            if (ii->Pipes[k].PipeType != UsbdPipeTypeInterrupt) continue;
            if (ii->Pipes[k].EndpointAddress & 0x80) { if (!e->pipe_in) e->pipe_in = ii->Pipes[k].PipeHandle; }
            else if (!e->pipe_out) e->pipe_out = ii->Pipes[k].PipeHandle;
        }
        if (ii->InterfaceNumber == 0) break;
    }
    dlog(L"Pipes", (e->pipe_in ? 1 : 0) | (e->pipe_out ? 2 : 0));
    if (!e->pipe_in || !e->pipe_out) { st = STATUS_DEVICE_CONFIGURATION_ERROR; goto out; }

    /* pad type: xpad's device table, else the class of interface 0 like xpad's xpad_table:
     * Xbox = 58/42 ('X','B'), Xbox 360 = FF/5D/01, 360 wireless = FF/5D/81, Xbox One/Series = FF/47/D0 */
    e->type = PAD_ONE; e->map = 0; e->flags = 0;
    for (p = cfg; p + 2 <= end && p[0] >= 2; p += p[0])
        if (p[1] == USB_INTERFACE_DESCRIPTOR_TYPE && p[0] >= 9 && p[2] == 0 && p[3] == 0) {
            if (p[5] == 0x58 && p[6] == 0x42) e->type = PAD_XBOX;
            else if (p[5] == 0xFF && p[6] == 0x5D && p[7] == 0x01) e->type = PAD_360;
            else if (p[5] == 0xFF && p[6] == 0x5D && p[7] == 0x81) e->type = PAD_360W;
            break;
        }
    for (i = 0; i < sizeof g_devices / sizeof g_devices[0]; i++)
        if (g_devices[i].vid == e->vid && g_devices[i].pid == e->pid) {
            e->type = g_devices[i].type; e->map = g_devices[i].map; e->flags = g_devices[i].flags;
            break;
        }
    dlog(L"PadType", e->type);

    e->seq = 0; e->init_done = 0;
    if (e->type == PAD_360) {
        x360_vendor_request(e);                   /* some third-party pads need it (xpad_start_input) */
        UCHAR led[3] = { 0x01, 0x03, 0x02 };
        NTSTATUS t;
        if (e->player < 0) e->player = player_alloc();
        if (e->player > 0) led[2] = (UCHAR)(0x02 + e->player % 4);
        t = send_raw_sync(e, led, sizeof led); dlog(L"InitLed", t); (void)t;
    } else if (e->type == PAD_360W) {
        send_raw_sync(e, x360w_presence, sizeof x360w_presence);
        send_raw_sync(e, x360w_led_p1, sizeof x360w_led_p1);
    } else if (e->type == PAD_ONE && !(e->flags & FLAG_DELAY_INIT)) {
        gip_init(e, FALSE);
    }
    e->lx = e->ly = e->rx = e->ry = 0; e->lt = e->rt = 0; e->btn = 0; e->dpad = e->guide = e->share = 0;
    e->neutral_pending = 1;
    e->cur_irp = NULL; e->refresh = 0; e->reported = 0;
    e->started = 1;
    if (e->type != PAD_ONE) {                     /* Xbox One/Series have their own heartbeat */
        KeInitializeTimer(&e->timer);
        KeInitializeDpc(&e->dpc, timer_tick, e);
        e->timer_on = 1;
        timer_arm(e);
    }
    st = STATUS_SUCCESS;
out:
    if (sel) ExFreePool(sel);
    if (cfg) ExFreePool(cfg);
    return st;
}

/* ------------------------------------------------------------------ input */
static const UCHAR g_hat[16] = { 8, 0, 4, 8, 6, 7, 5, 6, 2, 1, 3, 2, 8, 0, 4, 8 };

static void make_report(DEVEXT *e, UCHAR *r) {
    USHORT b = e->btn;
    wr16(r + 0, (USHORT)(e->lx + 32768));
    wr16(r + 2, (USHORT)(32767 - e->ly));
    wr16(r + 4, (USHORT)(e->rx + 32768));
    wr16(r + 6, (USHORT)(32767 - e->ry));
    wr16(r + 8, e->lt > 1023 ? 1023 : e->lt);
    wr16(r + 10, e->rt > 1023 ? 1023 : e->rt);
    r[12] = g_hat[e->dpad & 0x0f];
    if (e->guide) b |= 1 << 10;                   /* Xbox / Guide button */
    if (e->share) b |= 1 << 11;                   /* Share (Series X|S) */
    wr16(r + 13, b);
}

/* bit i of the result = HID button i+1 if bit pos[i] of the 16-bit value is set */
static USHORT map_bits(USHORT v, const UCHAR *pos, int n) {
    USHORT b = 0; int i;
    for (i = 0; i < n; i++) if (v & (1 << pos[i])) b |= (USHORT)(1 << i);
    return b;
}
/*                                   A  B  X  Y  LB RB View Menu LS RS          */
static const UCHAR g_one_bits[10] = { 4, 5, 6, 7, 12, 13, 3, 2, 14, 15 };   /* p[4] | p[5] << 8 */
static const UCHAR g_360_bits[10] = { 12, 13, 14, 15, 8, 9, 5, 4, 6, 7 };   /* p[2] | p[3] << 8 */

static USHORT trig8(UCHAR v) { return (USHORT)((v * 1023 + 127) / 255); }

/* Xbox 360 input data (wired report, or the payload of a wireless one): xpad360_process_packet */
static void parse_360(DEVEXT *e, const UCHAR *p) {
    e->btn = map_bits(rd16(p + 2), g_360_bits, 10);
    e->dpad = p[2] & 0x0f;
    e->guide = (p[3] & 0x04) ? 1 : 0;
    e->lt = trig8(p[4]); e->rt = trig8(p[5]);
    e->lx = (SHORT)rd16(p + 6); e->ly = (SHORT)rd16(p + 8);
    e->rx = (SHORT)rd16(p + 10); e->ry = (SHORT)rd16(p + 12);
}

/* returns TRUE if a report should be delivered for this packet */
static BOOLEAN parse_packet(DEVEXT *e, const UCHAR *p, ULONG n) {
    BOOLEAN rep = FALSE;
    switch (e->type) {
    case PAD_XBOX:                                /* xpad_process_packet: 20-byte report */
        if (n < 20) break;
        e->dpad = p[2] & 0x0f;
        e->btn = 0;
        if (p[4]) e->btn |= 1 << 0;               /* A, B, X, Y (analog buttons) */
        if (p[5]) e->btn |= 1 << 1;
        if (p[6]) e->btn |= 1 << 2;
        if (p[7]) e->btn |= 1 << 3;
        if (p[8]) e->btn |= 1 << 4;               /* Black */
        if (p[9]) e->btn |= 1 << 5;               /* White */
        if (p[2] & 0x20) e->btn |= 1 << 6;        /* Back */
        if (p[2] & 0x10) e->btn |= 1 << 7;        /* Start */
        if (p[2] & 0x40) e->btn |= 1 << 8;        /* stick presses */
        if (p[2] & 0x80) e->btn |= 1 << 9;
        e->lt = trig8(p[10]); e->rt = trig8(p[11]);
        e->lx = (SHORT)rd16(p + 12); e->ly = (SHORT)rd16(p + 14);
        e->rx = (SHORT)rd16(p + 16); e->ry = (SHORT)rd16(p + 18);
        rep = TRUE;
        break;
    case PAD_360:
        if (n >= 14 && p[0] == 0x00 && p[1] >= 0x14) { parse_360(e, p); rep = TRUE; }
        else rep = n >= 3 && (p[0] == 0x01 || p[0] == 0x03);   /* LED / rumble status: repeat the state */
        break;
    case PAD_360W:                                /* xpad360w_process_packet */
        if (n >= 18 && p[1] == 0x01) { parse_360(e, p + 4); rep = TRUE; }
        else rep = n >= 2 && (p[0] & 0x08);       /* presence change: repeat the state */
        break;
    default:                                      /* Xbox One / Series (GIP) */
        if (n >= 18 && p[0] == 0x20) {            /* GIP_CMD_INPUT */
            e->btn = map_bits(rd16(p + 4), g_one_bits, 10);
            e->dpad = p[5] & 0x0f;
            e->lt = rd16(p + 6); e->rt = rd16(p + 8);
            e->lx = (SHORT)rd16(p + 10); e->ly = (SHORT)rd16(p + 12);
            e->rx = (SHORT)rd16(p + 14); e->ry = (SHORT)rd16(p + 16);
            if (e->map & MAP_SHARE_BUTTON) {
                ULONG off = (e->map & MAP_SHARE_OFFSET) ? 26 : 18;
                if (n >= off) e->share = p[n - off] & 1;
            }
            rep = TRUE;
        } else if (n >= 5 && p[0] == 0x07) {      /* GIP_CMD_VIRTUAL_KEY: Xbox button */
            if (p[1] == 0x30) send_async(e, gip_guide_ack, sizeof gip_guide_ack, p[2]);
            e->guide = (p[4] & 3) ? 1 : 0;
            rep = TRUE;
        } else if (n >= 4 && p[0] == 0x02) {      /* GIP_CMD_ANNOUNCE: delayed init */
            if (e->flags & FLAG_DELAY_INIT) gip_init(e, TRUE);
        } else if (n >= 4 && p[0] == 0x03) {      /* status / heartbeat every few seconds: repeat the */
            rep = TRUE;                           /* current state, so late readers get a valid report */
        }
        break;
    }
    if (rep) {                                    /* xpad's mapping quirks */
        if (e->map & MAP_STICKS_TO_NULL) e->lx = e->ly = e->rx = e->ry = 0;
        if (e->map & MAP_TRIGGERS_TO_BUTTONS) { e->lt = e->lt ? 1023 : 0; e->rt = e->rt ? 1023 : 0; }
    }
    return rep;
}

typedef struct { struct _URB_BULK_OR_INTERRUPT_TRANSFER urb; UCHAR buf[64]; PDEVICE_OBJECT dev; } RDCTX;

static NTSTATUS NTAPI read_done(PDEVICE_OBJECT unused, PIRP irp, PVOID ctx);

static void arm_read(PIRP irp, RDCTX *c) {
    DEVEXT *e = EXT(c->dev);
    build_xfer(&c->urb, e->pipe_in, TRUE, c->buf, sizeof c->buf);
    set_urb_ioctl(irp, &c->urb);
    IoSetCompletionRoutine(irp, read_done, c, TRUE, TRUE, TRUE);
    e->cur_irp = irp;
}

static NTSTATUS NTAPI read_done(PDEVICE_OBJECT unused, PIRP irp, PVOID ctx) {
    RDCTX *c = ctx; DEVEXT *e = EXT(c->dev); NTSTATUS st = irp->IoStatus.Status;
    (void)unused;
    e->cur_irp = NULL;
    if (st == STATUS_CANCELLED && InterlockedExchange(&e->refresh, 0) && e->started) {
        irp->Cancel = FALSE;                      /* our own cancel (timer_tick): repeat the state */
        st = STATUS_SUCCESS;
        make_report(e, (UCHAR *)irp->UserBuffer);
        irp->IoStatus.Information = REPORT_LEN;
        e->reported = 1;
        irp->IoStatus.Status = st;
        ExFreePool(c);
        return STATUS_SUCCESS;
    }
#ifdef XGP_DIAG
    InterlockedIncrement(&g_done);
    g_last_status = (ULONG)st; g_last_len = c->urb.TransferBufferLength;
    if (NT_SUCCESS(st)) memcpy(g_last_pkt, c->buf, sizeof g_last_pkt);
    log_kick();
#endif
    if (NT_SUCCESS(st)) {
        if (parse_packet(e, c->buf, c->urb.TransferBufferLength)) {
#ifdef XGP_DIAG
            InterlockedIncrement(&g_inputs);
#endif
            make_report(e, (UCHAR *)irp->UserBuffer);
            irp->IoStatus.Information = REPORT_LEN;
            e->reported = 1; e->refresh = 0;
        } else if (e->started) {                  /* not an input packet: read the next one */
            irp->IoStatus.Information = 0;
            arm_read(irp, c);
            IoCallDriver(e->next, irp);
            return STATUS_MORE_PROCESSING_REQUIRED;
        } else {
            st = STATUS_DEVICE_NOT_CONNECTED;
        }
    }
    if (!NT_SUCCESS(st)) irp->IoStatus.Information = 0;
    irp->IoStatus.Status = st;
    ExFreePool(c);
    return STATUS_SUCCESS;
}

static NTSTATUS submit_read(PDEVICE_OBJECT d, PIRP irp) {
    RDCTX *c = ExAllocatePoolWithTag(NonPagedPool, sizeof *c, TAG);
    if (!c) return STATUS_INSUFFICIENT_RESOURCES;
    c->dev = d;
#ifdef XGP_DIAG
    InterlockedIncrement(&g_reads);
#endif
    IoMarkIrpPending(irp);
    arm_read(irp, c);
    IoCallDriver(EXT(d)->next, irp);
    return STATUS_PENDING;
}

/* ------------------------------------------------------------------ dispatch */
static NTSTATUS NTAPI dispatch_ioctl(PDEVICE_OBJECT d, PIRP irp) {
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(irp);
    DEVEXT *e = EXT(d);
    ULONG out = s->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS st = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;

    switch (s->Parameters.DeviceIoControl.IoControlCode) {
    case XIOCTL_HID_GET_DEVICE_DESCRIPTOR: {
        HID_DESC *h = irp->UserBuffer;
        if (out < sizeof *h) { st = STATUS_BUFFER_TOO_SMALL; break; }
        memset(h, 0, sizeof *h);
        h->bLength = sizeof *h; h->bDescriptorType = 0x21; h->bcdHID = 0x0100;
        h->bNumDescriptors = 1; h->bReportType = 0x22; h->wReportLength = sizeof g_report_desc;
        irp->IoStatus.Information = sizeof *h;
        break;
    }
    case XIOCTL_HID_GET_REPORT_DESCRIPTOR:
        if (out < sizeof g_report_desc) { st = STATUS_BUFFER_TOO_SMALL; break; }
        memcpy(irp->UserBuffer, g_report_desc, sizeof g_report_desc);
        irp->IoStatus.Information = sizeof g_report_desc;
        break;
    case XIOCTL_HID_GET_DEVICE_ATTRIBUTES: {
        HID_ATTR *a = irp->UserBuffer;
        if (out < sizeof *a) { st = STATUS_BUFFER_TOO_SMALL; break; }
        memset(a, 0, sizeof *a);
        a->Size = sizeof *a; a->VendorID = e->vid; a->ProductID = e->pid; a->VersionNumber = e->rev;
        irp->IoStatus.Information = sizeof *a;
        break;
    }
    case XIOCTL_HID_READ_REPORT:
        if (out < REPORT_LEN) { st = STATUS_BUFFER_TOO_SMALL; break; }
        if (!e->started) { st = STATUS_DEVICE_NOT_READY; break; }
        if (InterlockedExchange(&e->neutral_pending, 0)) {   /* pads send nothing until touched */
            make_report(e, (UCHAR *)irp->UserBuffer);
            irp->IoStatus.Information = REPORT_LEN;
            e->reported = 1;
            break;
        }
        st = submit_read(d, irp);
        if (st == STATUS_PENDING) return st;
        break;
    case XIOCTL_HID_ACTIVATE_DEVICE:
    case XIOCTL_HID_DEACTIVATE_DEVICE:
        break;
    default:                                      /* write report (rumble), strings, ... */
        st = STATUS_NOT_SUPPORTED;
        break;
    }
    irp->IoStatus.Status = st;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return st;
}

static NTSTATUS NTAPI sync_done(PDEVICE_OBJECT d, PIRP irp, PVOID ctx) {
    (void)d; (void)irp;
    KeSetEvent((PKEVENT)ctx, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS NTAPI dispatch_pnp(PDEVICE_OBJECT d, PIRP irp) {
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(irp);
    DEVEXT *e = EXT(d);
    NTSTATUS st;

    switch (s->MinorFunction) {
    case IRP_MN_START_DEVICE: {
        KEVENT ev;
        KeInitializeEvent(&ev, NotificationEvent, FALSE);
        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, sync_done, &ev, TRUE, TRUE, TRUE);
        st = IoCallDriver(e->next, irp);
        if (st == STATUS_PENDING) {
            KeWaitForSingleObject(&ev, Executive, KernelMode, FALSE, NULL);
            st = irp->IoStatus.Status;
        }
        dlog(L"StartLower", st);
        if (NT_SUCCESS(st)) st = start_usb(e);
        dlog(L"Start", st);
        irp->IoStatus.Status = st;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return st;
    }
    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
    case IRP_MN_REMOVE_DEVICE:
        e->started = 0;
        timer_stop(e);
        if (s->MinorFunction == IRP_MN_REMOVE_DEVICE) player_free(e);
        break;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(e->next, irp);
}

static NTSTATUS NTAPI dispatch_power(PDEVICE_OBJECT d, PIRP irp) {
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(EXT(d)->next, irp);
}

static NTSTATUS NTAPI dispatch_pass(PDEVICE_OBJECT d, PIRP irp) {
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(EXT(d)->next, irp);
}

static NTSTATUS NTAPI add_device(PDRIVER_OBJECT drv, PDEVICE_OBJECT fdo) {
    DEVEXT *e = EXT(fdo);
    (void)drv;
    memset(e, 0, sizeof *e);
    e->player = -1;
    e->next = HIDEXT(fdo)->NextDeviceObject;
    dlog(L"AddDevice", 1);
    fdo->Flags |= DO_POWER_PAGABLE;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static VOID NTAPI unload(PDRIVER_OBJECT drv) { (void)drv; }

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg) {
    HID_MINIDRIVER_REGISTRATION r;
    drv->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = dispatch_ioctl;
    drv->MajorFunction[IRP_MJ_PNP] = dispatch_pnp;
    drv->MajorFunction[IRP_MJ_POWER] = dispatch_power;
    drv->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = dispatch_pass;
    drv->DriverExtension->AddDevice = add_device;
    drv->DriverUnload = unload;
    memset(&r, 0, sizeof r);
    r.Revision = 1;
    r.DriverObject = drv;
    r.RegistryPath = reg;
    r.DeviceExtensionSize = sizeof(DEVEXT);
    r.DevicesArePolled = FALSE;
    dlog(L"DriverEntry", XGP_BUILD);
    {
        NTSTATUS st = HidRegisterMinidriver(&r);
        dlog(L"HidRegister", st);
        return st;
    }
}
