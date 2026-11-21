NUSB 3.6 Universal - USB mass storage drivers for Windows 98 SE (any language)
=============================================================================

Windows 98 Second Edition (4.10.2222 A) only.

Universal drivers for USB flash drives, card readers, cameras and similar
devices, plus a generic USB 2.0 (EHCI) stack - the drivers of Maximus Decim
Native USB (NUSB) 3.6, repackaged for every language of Windows 98 SE.

Unlike the original NUSB, this package does NOT copy English system files over
your Windows. The setup builds the updated USER.EXE, EXPLORER.EXE, SYSTRAY.EXE,
IOS.VXD and the INF files on your PC: new program code from NUSB + the texts of
your own Windows files, so your Windows stays in its own language. Texts that
don't exist in Windows 98 yet (the "Unplug or Eject Hardware" applet, new device
names) come from a language pack LXXXX.TXT, or stay English when there is no
pack for your language.

Install
-------
Run NUSB36UNIV.EXE (or NUSBSETU.EXE from this folder). The setup

 1. checks the Windows version and finds the original of every file it updates
    (backup, installed file, or the Windows 98 CD); each original is verified,
    a file that can't be built safely is left unchanged (with a warning),
 2. shows what it prepared and asks before changing anything,
 3. backs up every file it replaces to C:\WINDOWS\NUSB_BAK,
 4. removes the USB controllers, hubs and USB disks from Device Manager itself
    (USB keyboards and mice are kept), saved to C:\WINDOWS\NUSB_BAK\USB_BAK.REG,
 5. installs the drivers and offers a restart.

After the restart Windows detects the USB controllers again and installs the
new drivers (it may ask for the Windows 98 CD). Restart once more afterwards.

On Far East Windows (Korean, Japanese, Chinese) USER.EXE, USER32.DLL and
EXPLORER.EXE of your system are kept - their program code differs from the
Western one. USB disks work the same.

Uninstall: Control Panel - Add/Remove Programs - "NUSB 3.6 ...", or
C:\WINDOWS\NUSB_BAK\NUSBSETUP.EXE /U. The original files come back on the next
start of Windows. Log: C:\WINDOWS\NUSB.LOG.

Language packs
--------------
L0405.TXT = Czech. A pack is a UTF-8 text file named after the hex language ID
of Windows (L0415.TXT Polish, L0407.TXT German, ...). Copy L0405.TXT, translate
the values and keep the keys. Missing entries fall back to English.

Use at your own risk.
