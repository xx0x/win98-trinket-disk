# NUSB 3.6 Universal – USB mass storage for Windows 98 SE

USB flash drives, card readers, cameras and similar devices plus a generic
USB 2.0 (EHCI) stack for **Windows 98 Second Edition (4.10.2222 A) in any
language**. It's a rebuild of *Maximus Decim Native USB (NUSB) 3.6* for
universal use.

The original NUSB is English-only: it copies English `USER.EXE`,
`EXPLORER.EXE`, `SYSTRAY.EXE`, `SYSDM.CPL`, INF files … over the system, which
leaves a non-English Windows half-English (and breaks Far East versions).
This package ships the same drivers, but builds the changed system files **on
the target PC** from NUSB's new program code and the texts of the PC's own
files. Windows stays in its own language; texts that don't exist in Windows 98
yet come from a language pack or stay English.

It's also simpler and safer to install – which makes it worth using on English
Windows 98 SE too. The original asks you to remove all USB controllers, USB
devices and unknown devices in Device Manager by hand before installing; this
setup does that itself (USB keyboards and mice are kept). It checks the Windows
version and every file it replaces, shows what it will do before changing
anything, backs up all replaced files and can uninstall completely – the
original can only remove its USB 2.0 stack.

| | |
|---|---|
| [`NUSB36UNIV.EXE`](NUSB36UNIV.EXE) | the installer (single file, 864 768 bytes) |
| [`source/`](source/) | setup program sources, package files, build tools |

## Install

Run `NUSB36UNIV.EXE` on Windows 98 SE. The setup

1. checks the Windows version and finds the original of every file it updates
   (see below); a file that can't be built safely is left unchanged,
2. shows one screen with the detected language / language pack and the
   prepared files grouped by source, and asks before changing anything,
3. backs up every file it replaces to `C:\WINDOWS\NUSB_BAK`,
4. removes the USB controllers, root hubs, USB disks and unknown USB devices
   from the device registry itself – USB keyboards and mice (HID) are kept;
   the removed entries are exported to `NUSB_BAK\USB_BAK.REG`,
5. installs the drivers and offers a restart.

After the restart Windows detects the USB controllers again and installs the
new drivers (it may ask for the Windows 98 CD). Restart once more afterwards.

* **Uninstall:** *Add/Remove Programs* → *NUSB 3.6 …*, or
  `C:\WINDOWS\NUSB_BAK\NUSBSETUP.EXE /U`. The original files are restored at
  the next start of Windows and the USB devices are re-detected with the
  original drivers (Windows asks for the Windows 98 CD).
* **Reinstall / repair:** run the installer again; it offers reinstall or
  uninstall.
* **Log:** `C:\WINDOWS\NUSB.LOG`.

## How the system files are built

| File | Program code | Texts |
|---|---|---|
| `USER.EXE` 4.10.2231 | NUSB 2231 segments (only onto a `USER.EXE` 4.10.2222 whose code checksum matches) | resources of the PC's `USER.EXE`; registration stamp kept |
| `USER32.DLL` 4.10.2231 | NUSB file – only together with `USER.EXE` (the two are a pair) | – |
| `EXPLORER.EXE` 4.72.3612.1700 | NUSB | all resources of the PC's 4.72.3110 `EXPLORER.EXE` |
| `SYSTRAY.EXE` 4.10.2224 | NUSB | the PC's `SYSTRAY.EXE` + new strings / "unsafe removal" dialog from the pack |
| `HOTPLUG.DLL` 4.10.2224 | NUSB | language pack (the file doesn't exist in Windows 98) |
| `IOS.VXD` 4.10.2225 | NUSB | boot messages of the PC's `IOS.VXD` (OEM code page) |
| `*.INF` | NUSB | `[Strings]` of the PC's INF where the key exists, the rest from the pack |

The driver binaries (`*.SYS`, `*.PDR`, `DISKTSD.VXD`, `CDVSD.VXD`) are NUSB's
own; their code is the same in every language of Windows 98.

**Originals** are taken, in this order, from the setup's backup (reinstall),
the installed file, or the **Windows 98 CD** (Setup's `SourcePath` and
`C:\WINDOWS\OPTIONS\CABS`, all cabinets of the set, read with Windows'
`CABINET.DLL`). Every original is validated (version, code checksum, not an
NUSB file). This also repairs a system where the English NUSB was installed:
its files are recognized and replaced, the Windows ME `SYSDM.CPL` it installs
is put back to the original.

**Safety:**

* A file that can't be built safely is left unchanged and listed under
  *!! LEFT UNCHANGED* with the reason – USB storage still works, only the tray
  icon / dialogs may be English or missing.
* A newer installed file is never downgraded.
* The registered user/organization that Windows Setup stamps into `USER.EXE`
  is kept (copied from the PC's file, or written from the registry exactly as
  Setup does).
* The localized files are copied without Windows' version check, so the
  install doesn't stop at "a file of another language exists – keep it?"
  prompts (whose default answer would keep the English NUSB files).
* **Far East Windows** (Korean, Japanese, Chinese) has its own builds of
  `USER.EXE`, `USER32.DLL` and `EXPLORER.EXE`. There the `USER.EXE` code check
  fails, so `USER.EXE` and its partner `USER32.DLL` stay unchanged (mixing
  NUSB's Western `USER32.DLL` with a Korean `USER.EXE` crashes Windows at
  start-up), and `EXPLORER.EXE` is kept as well. What these three files carry
  are general Microsoft hotfixes, not USB support (`USER.EXE` 4.10.2231 is the
  fix for a hang when the Windows logo key is pressed with a journal hook
  active, KB291362, plus earlier shutdown / 1394 supplement fixes; Explorer
  4.72.3612 is the IE 5.5 SP2 build), so USB storage works the same – only
  those fixes are missing.

## Language packs

A pack is a UTF-8 text file `LXXXX.TXT` named after the hex LANGID of Windows
(`L0405.TXT` Czech, `L0415.TXT` Polish, `L0407.TXT` German, …). The setup takes
the exact LANGID, else a pack of the same primary language. A pack whose
`CodePage` doesn't match the system's ANSI code page is ignored. Missing
entries fall back to English.

To add a language, copy [`source/payload/L0405.TXT`](source/payload/L0405.TXT),
translate the values (keep the keys), save it as `LXXXX.TXT` in
`source/payload/` and rebuild. Sections: `[PACK]` (LangID, CodePage,
OEMCodePage, Name), `[SETUP]` (setup messages), `[VERSION]`, `[SYSTRAY.*]`,
`[HOTPLUG.*]`, `[IOS]`, `[INF]`, `[INF.<name>]` – format in
[`source/setup/pack.h`](source/setup/pack.h).

## Tested

UTM / QEMU, 2026-11-21:

* **Czech Windows 98 SE**, clean install: controllers re-detected with Czech
  names (incl. *Rozšířený hostitelský řadič USB 2.0 Intel ICH9 Family*), USB
  stick read/write verified on the host, Czech unplug icon / dialogs, system
  stays Czech.
* **Czech, over an installed English NUSB 3.6** with the Windows 98 CD in the
  drive: all originals taken from the CD cabinets, no prompts while copying,
  result byte-identical to the clean install, `SYSDM.CPL` restored.
* **Uninstall** (Czech): all 14 replaced files restored byte-identical, all
  58 added files removed, original USB drivers re-detected from the CD.
* **Korean Windows 98 SE** (no Korean pack): `USER.EXE`, `USER32.DLL`,
  `EXPLORER.EXE` kept, the rest built from the Korean originals; controllers
  re-detected, USB stick at 480 Mb/s as *이동식 디스크 (E:)*, read/write
  verified, *Stop USB Disk* / *Safe To Remove Hardware* work (English texts).
* **English Windows 98 SE**: everything built from the PC's own originals
  (incl. `USER.EXE` – Western code); controllers re-detected incl. *Intel ICH9
  Family USB2 Enhanced Host Controller*, USB stick at 480 Mb/s as *Removable
  Disk (E:)*, read/write verified, safe removal works; repeated cold boots fine.

## Windows 98 in UTM – notes

* **"Plug and Play BIOS" (code 24), no USB controllers at all:** SeaBIOS'
  PnP BIOS doesn't work with Windows 98, so the PCI bus is never enumerated.
  Device Manager → *System devices* → *Plug and Play BIOS* → *Driver* →
  *Update Driver* → list all drivers → show all hardware → *(Standard system
  devices)* → **PCI bus**, then let Windows detect the PCI devices. If it can't
  find files on the CD right after the IDE controller was installed (the CD
  drive is briefly gone), cancel that device, restart, and remove / refresh it
  in Device Manager. Remove the legacy *Cirrus Logic* display entry left from
  Setup.
* **Restarts hang on the boot logo** (Windows' own restart, also during
  Setup): the CPU sits in a real-mode loop. Shut down completely or power the
  VM off and start it again; Setup continues where it stopped. After
  installing NUSB use *Shut Down* (not a hard power-off of a running Windows):
  cutting power right after the install can lose the pending file
  replacements, and Windows then hangs once and recovers in Safe Mode.
* Inserting the Windows CD at the desktop opens its autorun window, which takes
  the keyboard focus – close it before sending keys to the VM.
* UTM's `ne2k_isa` network card is at I/O 300, **IRQ 9** (Setup proposes 3).
* The UTM USB tablet works with Windows 98 SE's own HID driver once the PCI
  bus is fixed; before that use the keyboard.
* Don't install guest additions; exchange files with ISO images.

## Source

```
source/
  setup/    the setup program NUSBSETU.EXE – freestanding Win32 C (no C runtime;
            KERNEL32/USER32/ADVAPI32, CABINET.DLL and ADVPACK.DLL loaded at run time)
  payload/  package files: NUSB 3.6 drivers and English templates, install INF
            _NUSB.INF, language packs LXXXX.TXT, README.TXT
  tools/    build.sh and helpers
```

| File | Purpose |
|---|---|
| `setup/setup.c` | front end: language pack, originals (incl. CD extraction via FDI), build jobs, report |
| `setup/common.c` | checks, backup, staging, `WININIT.INI`, advpack install, registration, uninstall |
| `setup/usbclean.c` | USB device-registry clean-up (keeps HID) |
| `setup/patch.c`, `pe.c`, `nefile.c` | file builders and the PE/NE resource engine |
| `setup/pack.c` | language-pack parser |
| `setup/manifest.h` | fixed data: target files, known checksums |
| `tools/build.sh` | builds `NUSB36UNIV.EXE` |
| `tools/cc98.sh` | mingw-w64 flags for a Windows 98 PE (subsystem 4.0, no CRT, CP1250 source strings) |
| `tools/build_sfx.py`, `rsrc.py`, `cabwriter.py` | IExpress self-extractor: LZX CAB + resources |
| `tools/lzxtool/lzxcab.c` | LZX:21 CAB compressor on top of liblzx (the NUSB wextract rejects MSZIP) |
| `tools/qmp.py` | drives a UTM/QEMU VM over QMP (screenshots, typing, mouse) for testing |

### Building

Requirements: mingw-w64 (`i686-w64-mingw32-gcc`), Python 3 with `pefile`, a
C compiler, and a checkout of [liblzx](https://github.com/elasota/liblzx)
(LGPL, not included).

```bash
LIBLZX=/path/to/liblzx sh source/tools/build.sh
```

This compiles `NUSBSETU.EXE`, packs it with `source/payload/` into an LZX
cabinet and writes `NUSB36UNIV.EXE`, using the current `NUSB36UNIV.EXE` as
the IExpress stub (`SFX_STUB` to use another; `LZXCAB` for a prebuilt
compressor; intermediate files go to the repository's `tmp/`).

## Open question: `USER.EXE`, `USER32.DLL` and `EXPLORER.EXE`

During the rebuild the question came up whether the package should update
these three files at all. They don't seem to be needed for USB storage:

* NUSB's install INF is Microsoft's *Windows 98 Second Edition Q242975 Update*
  (the 1394 Storage Supplement, which brought the *Unplug or Eject Hardware*
  applet and the storage-stack updates) extended with the USB drivers. NUSB
  ships `USER.EXE` 4.10.2231 – the newest cumulative build of the hotfixes it
  lists (242975, 260067 mapped-drives shutdown, 291362 Windows-logo-key hang
  with a journal hook) – probably so that systems that already had them aren't
  downgraded. `USER32.DLL` is its pair. `EXPLORER.EXE` 4.72.3612.1700 is the
  IE 5.5 SP2 build ("fix IE55SP2" in the INF); why NUSB includes it is unknown.
* Comparing `USER.EXE` 2222 and 2231 (changed code mapped to the nearest named
  functions), the real changes are in the message loop, application start-up,
  keyboard-layout / hook handling and task switching – consistent with those
  hotfixes, nothing USB-specific.
* On Korean Windows, where all three stay unchanged, USB storage worked
  completely (USB 2.0, read/write, tray icon, safe removal).
* Other updates (e.g. IE6, later hotfixes) installed *before* this package are
  never downgraded; installed *after* it, they simply replace the files. The
  uninstaller, however, restores its backups without checking whether a file
  was updated since – it could put back an older file.

Options considered: keep the NUSB behaviour; drop the three files (USB storage
stack only – simpler, no Far East special case, but without those unrelated
hotfixes); or make them optional.

**Decision: the NUSB behaviour is preserved, at least for now.** A future,
deeper research and testing round may revisit it – validating whether dropping
them makes sense and weighing the advantages and disadvantages. Open points: the
*unsafe removal* warning (pulling a stick without stopping it, can't be
triggered in QEMU) on real hardware without the new `USER.EXE`, the interaction
with IE6, and an uninstaller that restores only files still unchanged since the
install.

## Credits / licences

* NUSB 3.6 – Maximus Decim; the driver files are Microsoft's (Windows ME /
  2000 / XP updates). Self-extractor: Microsoft IExpress (wextract) from NUSB 3.6.
* liblzx – Eric Lasota, based on wimlib by Eric Biggers (LGPL).
* Universal setup and Czech language pack – unofficial, 2026. Use at your own risk.
