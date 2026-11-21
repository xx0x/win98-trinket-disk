/*
 * manifest.h - fixed data of the setup (hand-maintained).
 *
 * g_targets       every file the install INF (_NUSB.INF) writes, relative to C:\WINDOWS;
 *                 these are backed up before installing and restored on uninstall.
 * g_known         SYSDM.CPL of the English NUSB (Windows ME version) and the Windows 98 SE
 *                 original, to repair a system where the English NUSB was installed.
 * g_nusb_inf_crc  CRC32 of the English NUSB 3.3/3.6 INF files - never used as an "original".
 * USER_CODE_CRC_2222  CRC32 of the code (segments + relocations) of USER.EXE 4.10.2222, Western
 *                 builds (identical in Czech and English); Far East builds differ.
 */
static const char *const g_targets[] = {
    "EXPLORER.EXE",
    "SYSTEM32\\DRIVERS\\1394BUS.SYS",
    "SYSTEM32\\DRIVERS\\OHCI1394.SYS",
    "SYSTEM32\\DRIVERS\\SBP2PORT.SYS",
    "SYSTEM32\\DRIVERS\\NTMAP.SYS",
    "SYSTEM32\\DRIVERS\\USBAUTH.SYS",
    "SYSTEM32\\DRIVERS\\USBNTMAP.SYS",
    "SYSTEM32\\DRIVERS\\USBSTOR.SYS",
    "SYSTEM32\\DRIVERS\\USBU2A.SYS",
    "SYSTEM32\\DRIVERS\\USBEHCI.SYS",
    "SYSTEM32\\DRIVERS\\USBHUB20.SYS",
    "SYSTEM32\\DRIVERS\\USBPORT.SYS",
    "SYSTEM32\\DRIVERS\\WDMSTUB.SYS",
    "SYSTEM32\\DRIVERS\\OPENHCI.SYS",
    "SYSTEM32\\DRIVERS\\UHCD.SYS",
    "SYSTEM32\\DRIVERS\\USBD.SYS",
    "SYSTEM32\\DRIVERS\\USBHUB.SYS",
    "SYSTEM32\\DRIVERS\\USBCCGP.SYS",
    "SYSTEM32\\DRIVERS\\QV2KUX.SYS",
    "SYSTEM\\VMM32\\IOS.VXD",
    "SYSTEM\\HOTPLUG.DLL",
    "SYSTEM\\SYSTRAY.EXE",
    "SYSTEM\\USER.EXE",
    "SYSTEM\\USER32.DLL",
    "SYSTEM\\IOSUBSYS\\NTMAPHLP.PDR",
    "SYSTEM\\IOSUBSYS\\DISKTSD.VXD",
    "SYSTEM\\IOSUBSYS\\USBMPHLP.PDR",
    "SYSTEM\\IOSUBSYS\\CDVSD.VXD",
    "HELP\\HARDWARE.HLP",
    "INF\\1394.INF",
    "INF\\NTMAP.INF",
    "INF\\USBSTOR.INF",
    "INF\\USBNTMAP.INF",
    "INF\\NODRIVER.INF",
    "INF\\USB2.INF",
    "INF\\USB.INF",
    "INF\\_USB2UN.INF",
    "OPTIONS\\CABS\\1394BUS.SYS",
    "OPTIONS\\CABS\\OHCI1394.SYS",
    "OPTIONS\\CABS\\SBP2PORT.SYS",
    "OPTIONS\\CABS\\NTMAP.SYS",
    "OPTIONS\\CABS\\IOS.VXD",
    "OPTIONS\\CABS\\HOTPLUG.DLL",
    "OPTIONS\\CABS\\SYSTRAY.EXE",
    "OPTIONS\\CABS\\HARDWARE.HLP",
    "OPTIONS\\CABS\\1394.INF",
    "OPTIONS\\CABS\\NTMAP.INF",
    "OPTIONS\\CABS\\NTMAPHLP.PDR",
    "OPTIONS\\CABS\\USER.EXE",
    "OPTIONS\\CABS\\USER32.DLL",
    "OPTIONS\\CABS\\DISKTSD.VXD",
    "OPTIONS\\CABS\\USBAUTH.SYS",
    "OPTIONS\\CABS\\USBMPHLP.PDR",
    "OPTIONS\\CABS\\USBNTMAP.SYS",
    "OPTIONS\\CABS\\USBSTOR.SYS",
    "OPTIONS\\CABS\\USBSTOR.INF",
    "OPTIONS\\CABS\\USBNTMAP.INF",
    "OPTIONS\\CABS\\NODRIVER.INF",
    "OPTIONS\\CABS\\EXPLORER.EXE",
    "OPTIONS\\CABS\\USB2.INF",
    "OPTIONS\\CABS\\USBEHCI.SYS",
    "OPTIONS\\CABS\\USBHUB20.SYS",
    "OPTIONS\\CABS\\USBPORT.SYS",
    "OPTIONS\\CABS\\WDMSTUB.SYS",
    "OPTIONS\\CABS\\CDVSD.VXD",
    "OPTIONS\\CABS\\OPENHCI.SYS",
    "OPTIONS\\CABS\\UHCD.SYS",
    "OPTIONS\\CABS\\USBD.SYS",
    "OPTIONS\\CABS\\USBHUB.SYS",
    "OPTIONS\\CABS\\USBCCGP.SYS",
    "OPTIONS\\CABS\\USB.INF",
    "OPTIONS\\CABS\\QV2KUX.SYS",
    0 };

typedef struct { const char *rel; const char *kind; unsigned crc, size; const char *ver; } known_t;
static const known_t g_known[] = {
    { "SYSTEM\\SYSDM.CPL", "ORIG", 0x41624c3du, 389379, "4.10.0.2222" },
    { "SYSTEM\\SYSDM.CPL", "EN", 0xf4b8cff2u, 389920, "4.90.0.3001" },
    { 0 } };

/* CRC32 of every English NUSB 3.3/3.6 INF - never used as a "local original" */
static const unsigned g_nusb_inf_crc[] = {0x044bd079u, 0x0655895cu, 0x388780f6u, 0x528142f6u, 0x551a94dcu, 0x65ef4fa2u, 0x86392c8au, 0x8ab8cd6eu, 0x90de75ddu, 0xb9ab9839u, 0xbf59d786u, 0xc48968e0u, 0xe44475ebu, 0xeb8aa135u, 0 };

/* see above */
#define USER_CODE_CRC_2222 0x155500feu
