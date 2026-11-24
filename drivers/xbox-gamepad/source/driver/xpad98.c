/*
 * xpad98.c - XPAD98: Xbox One / Xbox Series X|S controller (GIP protocol, USB) for Windows 98 SE.
 *
 * A WDM HID minidriver (like Windows 98's own HIDUSB.SYS) on top of the USB stack:
 *   - selects the USB configuration (all interfaces at alternate setting 0, so the audio
 *     interface stays off) and finds the interrupt IN/OUT pipes of interface 0,
 *   - sends the GIP start-up packets (power on, LED on, authentication done),
 *   - turns GIP input packets into reports of a HID game pad, so Windows 98 installs its own
 *     "HID-compliant game controller" (JOYHID.VXD/VJOYD) on top: Control Panel - Game
 *     Controllers and DirectInput games see an ordinary joystick.
 *
 * Protocol details follow Linux drivers/input/joystick/xpad.c.
 * Only KERNEL (NTKERN) functions that Windows 98 SE exports are used; no USBD.SYS imports, so
 * the driver works with the Windows 98 USB stack and with NUSB alike.
 *
 * HID report (15 bytes, no report ID):
 *   0  X  left stick X      0..65535      8  Rx left trigger    0..1023
 *   2  Y  left stick Y      (down = max)  10 Ry right trigger   0..1023
 *   4  Z  right stick X                   12 hat switch (d-pad) 0..7, 8 = centred
 *   6  Rz right stick Y                   13 buttons 1-12: A B X Y LB RB View Menu LS RS Guide Share
 */
#include <ddk/ntddk.h>
#include <usb.h>
#include <usbioctl.h>

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
#define XPAD_BUILD 7                       /* diagnostics: build number */

/* ------------------------------------------------------------------ device state */
typedef struct {
    PDEVICE_OBJECT next;              /* the USB device below us */
    USBD_PIPE_HANDLE pipe_in, pipe_out;
    USHORT vid, pid, rev;
    LONG seq;                         /* GIP sequence number */
    volatile LONG started;
    volatile LONG neutral_pending;    /* first read after start: report the neutral state at once */
    /* last controller state */
    UCHAR b4, b5, guide, share;
    USHORT lt, rt;
    SHORT lx, ly, rx, ry;
} DEVEXT;

#define HIDEXT(d) ((HID_DEVICE_EXTENSION *)(d)->DeviceExtension)
#define EXT(d)    ((DEVEXT *)HIDEXT(d)->MiniDeviceExtension)
#define TAG       'dapX'

#ifdef XPAD_DIAG
/* diagnostics (build with -DXPAD_DIAG): HKLM\Software\XPAD98\<name> = DWORD; the key must exist */
static void dlog(PCWSTR name, ULONG v) {
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, L"\\Registry\\Machine\\Software\\XPAD98", name, REG_DWORD, &v, sizeof v);
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
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, L"\\Registry\\Machine\\Software\\XPAD98", L"LastPacket",
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
static const UCHAR gip_power_on[] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
static const UCHAR gip_s_init[]   = { 0x05, 0x20, 0x00, 0x0f, 0x06 };   /* One S / Elite 2 after Bluetooth use */
static const UCHAR gip_led_on[]   = { 0x0a, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14 };
static const UCHAR gip_auth_done[] = { 0x06, 0x20, 0x00, 0x02, 0x01, 0x00 };
static const UCHAR gip_guide_ack[] = { 0x01, 0x20, 0x00, 0x09, 0x00, 0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };

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

static NTSTATUS send_sync(DEVEXT *e, const UCHAR *pkt, ULONG len) {
    struct _URB_BULK_OR_INTERRUPT_TRANSFER u; UCHAR buf[64];
    memcpy(buf, pkt, len);
    buf[2] = (UCHAR)(InterlockedIncrement(&e->seq) - 1);
    build_xfer(&u, e->pipe_out, FALSE, buf, len);
    return urb_sync(e->next, &u);
}

/* ------------------------------------------------------------------ start: configuration + GIP init */
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

    /* GIP start-up sequence (xpad's xboxone_init_packets for Microsoft pads) */
    e->seq = 0;
    { NTSTATUS t = send_sync(e, gip_power_on, sizeof gip_power_on); dlog(L"InitPowerOn", t); (void)t; }
    if (e->vid == 0x045e && (e->pid == 0x02ea || e->pid == 0x0b00)) send_sync(e, gip_s_init, sizeof gip_s_init);
    { NTSTATUS t = send_sync(e, gip_led_on, sizeof gip_led_on); dlog(L"InitLed", t); (void)t; }
    { NTSTATUS t = send_sync(e, gip_auth_done, sizeof gip_auth_done); dlog(L"InitAuth", t); (void)t; }
    e->lx = e->ly = e->rx = e->ry = 0; e->lt = e->rt = 0; e->b4 = e->b5 = e->guide = e->share = 0;
    e->neutral_pending = 1;
    e->started = 1;
    st = STATUS_SUCCESS;
out:
    if (sel) ExFreePool(sel);
    if (cfg) ExFreePool(cfg);
    return st;
}

/* ------------------------------------------------------------------ input */
static const UCHAR g_hat[16] = { 8, 0, 4, 8, 6, 7, 5, 6, 2, 1, 3, 2, 8, 0, 4, 8 };

static void make_report(DEVEXT *e, UCHAR *r) {
    USHORT b = 0;
    wr16(r + 0, (USHORT)(e->lx + 32768));
    wr16(r + 2, (USHORT)(32767 - e->ly));
    wr16(r + 4, (USHORT)(e->rx + 32768));
    wr16(r + 6, (USHORT)(32767 - e->ry));
    wr16(r + 8, e->lt > 1023 ? 1023 : e->lt);
    wr16(r + 10, e->rt > 1023 ? 1023 : e->rt);
    r[12] = g_hat[e->b5 & 0x0f];                  /* up, down, left, right */
    if (e->b4 & 0x10) b |= 1 << 0;                /* A */
    if (e->b4 & 0x20) b |= 1 << 1;                /* B */
    if (e->b4 & 0x40) b |= 1 << 2;                /* X */
    if (e->b4 & 0x80) b |= 1 << 3;                /* Y */
    if (e->b5 & 0x10) b |= 1 << 4;                /* LB */
    if (e->b5 & 0x20) b |= 1 << 5;                /* RB */
    if (e->b4 & 0x08) b |= 1 << 6;                /* View */
    if (e->b4 & 0x04) b |= 1 << 7;                /* Menu */
    if (e->b5 & 0x40) b |= 1 << 8;                /* left stick press */
    if (e->b5 & 0x80) b |= 1 << 9;                /* right stick press */
    if (e->guide)     b |= 1 << 10;               /* Xbox button */
    if (e->share)     b |= 1 << 11;               /* Share (Series X|S) */
    wr16(r + 13, b);
}

/* returns TRUE if a report should be delivered for this packet */
static BOOLEAN parse_packet(DEVEXT *e, const UCHAR *p, ULONG n) {
    if (n >= 18 && p[0] == 0x20) {                /* GIP_CMD_INPUT */
        e->b4 = p[4]; e->b5 = p[5];
        e->lt = rd16(p + 6); e->rt = rd16(p + 8);
        e->lx = (SHORT)rd16(p + 10); e->ly = (SHORT)rd16(p + 12);
        e->rx = (SHORT)rd16(p + 14); e->ry = (SHORT)rd16(p + 16);
        if (e->vid == 0x045e && e->pid == 0x0b12 && n >= 26) e->share = p[n - 26] & 1;
        return TRUE;
    }
    if (n >= 5 && p[0] == 0x07) {                 /* GIP_CMD_VIRTUAL_KEY: Xbox button */
        if (p[1] == 0x30) send_async(e, gip_guide_ack, sizeof gip_guide_ack, p[2]);
        e->guide = (p[4] & 3) ? 1 : 0;
        return TRUE;
    }
    if (n >= 4 && p[0] == 0x03)                   /* GIP status / heartbeat, every few seconds: repeat the */
        return TRUE;                              /* current state, so late readers get a valid report     */
    return FALSE;                                 /* announce, acks ... */
}

typedef struct { struct _URB_BULK_OR_INTERRUPT_TRANSFER urb; UCHAR buf[64]; PDEVICE_OBJECT dev; } RDCTX;

static NTSTATUS NTAPI read_done(PDEVICE_OBJECT unused, PIRP irp, PVOID ctx);

static void arm_read(PIRP irp, RDCTX *c) {
    DEVEXT *e = EXT(c->dev);
    build_xfer(&c->urb, e->pipe_in, TRUE, c->buf, sizeof c->buf);
    set_urb_ioctl(irp, &c->urb);
    IoSetCompletionRoutine(irp, read_done, c, TRUE, TRUE, TRUE);
}

static NTSTATUS NTAPI read_done(PDEVICE_OBJECT unused, PIRP irp, PVOID ctx) {
    RDCTX *c = ctx; DEVEXT *e = EXT(c->dev); NTSTATUS st = irp->IoStatus.Status;
    (void)unused;
#ifdef XPAD_DIAG
    InterlockedIncrement(&g_done);
    g_last_status = (ULONG)st; g_last_len = c->urb.TransferBufferLength;
    if (NT_SUCCESS(st)) memcpy(g_last_pkt, c->buf, sizeof g_last_pkt);
    log_kick();
#endif
    if (NT_SUCCESS(st)) {
        if (parse_packet(e, c->buf, c->urb.TransferBufferLength)) {
#ifdef XPAD_DIAG
            InterlockedIncrement(&g_inputs);
#endif
            make_report(e, (UCHAR *)irp->UserBuffer);
            irp->IoStatus.Information = REPORT_LEN;
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
#ifdef XPAD_DIAG
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
    dlog(L"DriverEntry", XPAD_BUILD);
    {
        NTSTATUS st = HidRegisterMinidriver(&r);
        dlog(L"HidRegister", st);
        return st;
    }
}
