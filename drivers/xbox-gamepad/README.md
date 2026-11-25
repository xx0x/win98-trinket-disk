# XGP98 – Xbox controllers for Windows 98 SE

XGP98 is an adaptation of the Linux Xbox gamepad driver
([`xpad`](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c))
to Windows 98 SE. Like the Linux driver it is free software under the **GNU
General Public License, version 2 or later**.

It makes **Xbox 360, Xbox One and Xbox Series X|S controllers and compatible
third-party controllers** work as an ordinary game controller on Windows 98
Second Edition, connected with a USB cable: Control Panel → *Game Controllers*,
DirectInput and joystick-aware games see it like any USB gamepad. Original Xbox
controllers (with a USB adapter) and the first pad on an Xbox 360 wireless
receiver work too. The full list of 322 supported controllers is
[at the end](#supported-controllers).

| Files | Description |
|---|---|
| [`XGP98.INF`](XGP98.INF), [`XGP98.SYS`](XGP98.SYS) | the driver (copy both to a floppy, CD or folder) |
| [`source/`](source/) | driver source, build tools, licence (`source/COPYING`) |

## Install

1. Connect the controller. Windows finds a new USB device.
2. In the *Add New Hardware Wizard* choose *Search for the best driver* and
   point it at the folder / drive with `XGP98.INF` (or Device Manager → the
   device → *Update Driver* → *Have Disk*).
3. Windows installs the controller and then its own *HID-compliant game
   controller* (it may ask for the Windows 98 CD). If it asks to restart, do so.
4. Control Panel → *Game Controllers* lists the controller with status *OK*;
   *Properties* → *Test* shows all axes, the hat and the buttons.

Nothing else is needed: the USB and HID drivers of Windows 98 SE are enough,
and it works the same with [NUSB](../usb-mass-storage/) installed (it doesn't
use or replace any of its files).

## Mapping

| Controller | Windows 98 |
|---|---|
| Left stick | X / Y axes (*1 (x) and 2 (y)*) |
| Right stick | Z / Rz axes (*3*, *4*) – like most DirectInput dual-stick pads |
| Left / right trigger | Rx / Ry axes (*5*, *6*), separate |
| D-pad | Point of view hat, 8 directions |
| A, B, X, Y | buttons 1–4 |
| LB, RB (original Xbox: Black, White) | buttons 5, 6 |
| Back / View, Start / Menu | buttons 7, 8 |
| Left / right stick press | buttons 9, 10 |
| Xbox / Guide button | button 11 |
| Share (Xbox Series and some others) | button 12 |

Controllers that `xpad` marks as having no sticks (dance pads) report centred
sticks; digital triggers report fully pressed / released.

Not supported: **rumble** – Windows 98 games use rumble only through
DirectInput force feedback, which needs a HID force-feedback (PID)
implementation in the driver, not just a motor command; the headset jack; the
Elite paddles; more than the first pad on an Xbox 360 wireless receiver;
Bluetooth.

## How it works

`XGP98.SYS` is a WDM HID minidriver – the same kind of driver as Windows 98's
own `HIDUSB.SYS` – so Windows' HID class driver and its standard joystick
support (`JOYHID.VXD`, `VJOYD.VXD`) do the rest. Following `xpad`, the driver
recognises the controller type (from `xpad`'s device table or the USB interface
class), selects the USB configuration (audio interfaces stay off), sends the
start-up packets the type needs (Xbox One/Series: GIP power on, LED,
authentication and `xpad`'s vendor-specific init packets; Xbox 360: `xpad`'s
vendor request and the player-1 LED; wireless receiver: presence inquiry and
LED), and converts the controller's input packets into one common HID gamepad
report, applying `xpad`'s per-controller quirks. Controllers send nothing until
touched, so the driver reports the current state again every couple of seconds
while idle (Xbox One/Series: on the controller's own status packets; others:
from a timer) – a program that opens the joystick later then still sees the
neutral state. It uses only kernel functions
that Windows 98 SE provides; the build checks the imports and that the image
can be loaded by the Windows 98 driver loader.

## Tested

In QEMU (UTM) with Windows 98 SE Czech (without NUSB) and English (with NUSB),
using an Xbox 360 Controller (045E:028E) and an Xbox Series X|S Controller
(045E:0B12), both connected at the same time.

On real hardware with Windows 98 SE Czech (4.10.2222A) and NUSB installed.

## Building

Requirements: mingw-w64 (`i686-w64-mingw32-gcc`) and Python 3 with `pefile`.

```bash
python3 source/tools/gen_devices.py path/to/linux/drivers/input/joystick/xpad.c .
sh source/tools/build.sh
```

`gen_devices.py` regenerates the device table (`source/driver/devices.h`), the
INF and the list below from `xpad.c`'s `xpad_device[]` table; `build.sh`
writes `XGP98.SYS`. `XGP_CFLAGS=-DXGP_DIAG` builds a diagnostic version that
logs its start-up steps and packet counters to `HKLM\Software\XGP98` (create
the key first; export it with `regedit /e`).

| File | Purpose |
|---|---|
| `source/driver/xgp98.c` | the driver |
| `source/driver/devices.h` | generated: VID/PID → controller type, `xpad` quirks |
| `source/tools/gen_devices.py` | generates `devices.h`, `XGP98.INF`, the controller list |
| `source/tools/build.sh` | compiler / linker flags for a Windows 98 WDM driver |
| `source/tools/fixpe.py` | import check, 1:1 section layout check, PE checksum |


## Licence and credits

XGP98 is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version (`SPDX-License-Identifier: GPL-2.0-or-later`). It is distributed in the
hope that it will be useful, but WITHOUT ANY WARRANTY. See
[`source/COPYING`](source/COPYING).

* Based on the Linux `xpad` driver: Marko Friedemann, Oliver Schwartz, Steven
  Toth, Franz Lehner, Ivan Hawkes, Dominic Cerquetti, Adam Buchbinder, Jan
  Kratochvil, Christoph Fritz and the many other contributors to
  `drivers/input/joystick/xpad.c` (see the file header and the kernel history),
  with protocol information from ITO Takayuki, the Free60 project and Pekka
  Pöyry (quantus).
* Windows 98 adaptation: 2026, unofficial. Use at your own risk.


## Supported controllers

All controllers of `xpad`'s device table (322), generated from the Linux kernel
source. Controllers not in the list that follow the same protocols can be added
to the table.

| VID:PID | Controller | Type |
|---|---|---|
| 0079:18D4 | GPD Win 2 X-Box Controller | Xbox 360 |
| 0351:1000 | CRKD LP Blueberry Burst Pro Edition (Xbox) | Xbox 360 |
| 0351:2000 | CRKD LP Black Tribal Edition (Xbox) | Xbox 360 |
| 03EB:FF01 | Wooting One (Legacy) | Xbox 360 |
| 03EB:FF02 | Wooting Two (Legacy) | Xbox 360 |
| 03F0:038D | HyperX Clutch | Xbox 360 |
| 03F0:048D | HyperX Clutch | Xbox 360 |
| 03F0:0495 | HyperX Clutch Gladiate | Xbox One / Series |
| 03F0:07A0 | HyperX Clutch Gladiate RGB | Xbox One / Series |
| 03F0:08B6 | HyperX Clutch Gladiate | Xbox One / Series |
| 03F0:09B4 | HyperX Clutch Tanto | Xbox One / Series |
| 044F:0F00 | Thrustmaster Wheel | Xbox |
| 044F:0F03 | Thrustmaster Wheel | Xbox |
| 044F:0F07 | Thrustmaster, Inc. Controller | Xbox |
| 044F:0F10 | Thrustmaster Modena GT Wheel | Xbox |
| 044F:B326 | Thrustmaster Gamepad GP XID | Xbox 360 |
| 044F:D01E | ThrustMaster, Inc. ESWAP X 2 ELDEN RING EDITION | Xbox One / Series |
| 045E:0202 | Microsoft X-Box pad v1 (US) | Xbox |
| 045E:0285 | Microsoft X-Box pad (Japan) | Xbox |
| 045E:0287 | Microsoft Xbox Controller S | Xbox |
| 045E:0288 | Microsoft Xbox Controller S v2 | Xbox |
| 045E:0289 | Microsoft X-Box pad v2 (US) | Xbox |
| 045E:028E | Microsoft X-Box 360 pad | Xbox 360 |
| 045E:028F | Microsoft X-Box 360 pad v2 | Xbox 360 |
| 045E:0291 | Xbox 360 Wireless Receiver (XBOX) | Xbox 360 wireless receiver |
| 045E:02A9 | Xbox 360 Wireless Receiver (Unofficial) | Xbox 360 wireless receiver |
| 045E:02D1 | Microsoft X-Box One pad | Xbox One / Series |
| 045E:02DD | Microsoft X-Box One pad (Firmware 2015) | Xbox One / Series |
| 045E:02E3 | Microsoft X-Box One Elite pad | Xbox One / Series |
| 045E:02EA | Microsoft X-Box One S pad | Xbox One / Series |
| 045E:0719 | Xbox 360 Wireless Receiver | Xbox 360 wireless receiver |
| 045E:0B00 | Microsoft X-Box One Elite 2 pad | Xbox One / Series |
| 045E:0B0A | Microsoft X-Box Adaptive Controller | Xbox One / Series |
| 045E:0B12 | Microsoft Xbox Series S\|X Controller | Xbox One / Series |
| 046D:C21D | Logitech Gamepad F310 | Xbox 360 |
| 046D:C21E | Logitech Gamepad F510 | Xbox 360 |
| 046D:C21F | Logitech Gamepad F710 | Xbox 360 |
| 046D:C242 | Logitech Chillstream Controller | Xbox 360 |
| 046D:CA84 | Logitech Xbox Cordless Controller | Xbox |
| 046D:CA88 | Logitech Compact Controller for Xbox | Xbox |
| 046D:CA8A | Logitech Precision Vibration Feedback Wheel | Xbox |
| 046D:CAA3 | Logitech DriveFx Racing Wheel | Xbox 360 |
| 0502:1305 | Acer NGR200 | Xbox 360 |
| 056E:2004 | Elecom JC-U3613M | Xbox 360 |
| 05FD:1007 | Mad Catz Controller (unverified) | Xbox |
| 05FD:107A | InterAct 'PowerPad Pro' X-Box pad (Germany) | Xbox |
| 05FE:3030 | Chic Controller | Xbox |
| 05FE:3031 | Chic Controller | Xbox |
| 062A:0020 | Logic3 Xbox GamePad | Xbox |
| 062A:0033 | Competition Pro Steering Wheel | Xbox |
| 06A3:0200 | Saitek Racing Wheel | Xbox |
| 06A3:0201 | Saitek Adrenalin | Xbox |
| 06A3:F51A | Saitek P3600 | Xbox 360 |
| 0738:4503 | Mad Catz Racing Wheel | Xbox One / Series |
| 0738:4506 | Mad Catz 4506 Wireless Controller | Xbox |
| 0738:4516 | Mad Catz Control Pad | Xbox |
| 0738:4520 | Mad Catz Control Pad Pro | Xbox |
| 0738:4522 | Mad Catz LumiCON | Xbox |
| 0738:4526 | Mad Catz Control Pad Pro | Xbox |
| 0738:4530 | Mad Catz Universal MC2 Racing Wheel and Pedals | Xbox |
| 0738:4536 | Mad Catz MicroCON | Xbox |
| 0738:4540 | Mad Catz Beat Pad | Xbox |
| 0738:4556 | Mad Catz Lynx Wireless Controller | Xbox |
| 0738:4586 | Mad Catz MicroCon Wireless Controller | Xbox |
| 0738:4588 | Mad Catz Blaster | Xbox |
| 0738:45FF | Mad Catz Beat Pad (w/ Handle) | Xbox |
| 0738:4716 | Mad Catz Wired Xbox 360 Controller | Xbox 360 |
| 0738:4718 | Mad Catz Street Fighter IV FightStick SE | Xbox 360 |
| 0738:4726 | Mad Catz Xbox 360 Controller | Xbox 360 |
| 0738:4728 | Mad Catz Street Fighter IV FightPad | Xbox 360 |
| 0738:4736 | Mad Catz MicroCon Gamepad | Xbox 360 |
| 0738:4738 | Mad Catz Wired Xbox 360 Controller (SFIV) | Xbox 360 |
| 0738:4740 | Mad Catz Beat Pad | Xbox 360 |
| 0738:4743 | Mad Catz Beat Pad Pro | Xbox |
| 0738:4758 | Mad Catz Arcade Game Stick | Xbox 360 |
| 0738:4A01 | Mad Catz FightStick TE 2 | Xbox One / Series |
| 0738:6040 | Mad Catz Beat Pad Pro | Xbox |
| 0738:9871 | Mad Catz Portable Drum | Xbox 360 |
| 0738:B726 | Mad Catz Xbox controller - MW2 | Xbox 360 |
| 0738:B738 | Mad Catz MVC2TE Stick 2 | Xbox 360 |
| 0738:BEEF | Mad Catz JOYTECH NEO SE Advanced GamePad | Xbox 360 |
| 0738:CB02 | Saitek Cyborg Rumble Pad - PC/Xbox 360 | Xbox 360 |
| 0738:CB03 | Saitek P3200 Rumble Pad - PC/Xbox 360 | Xbox 360 |
| 0738:CB29 | Saitek Aviator Stick AV8R02 | Xbox 360 |
| 0738:F738 | Super SFIV FightStick TE S | Xbox 360 |
| 07FF:FFFF | Mad Catz GamePad | Xbox 360 |
| 0B05:1A38 | ASUS ROG RAIKIRI | Xbox One / Series |
| 0B05:1ABB | ASUS ROG RAIKIRI PRO | Xbox One / Series |
| 0B05:1C91 | ASUS ROG RAIKIRI II | Xbox 360 |
| 0B05:1C92 | ASUS ROG RAIKIRI II WIRELESS | Xbox 360 |
| 0B05:1C96 | ASUS ROG RAIKIRI II XBOX | Xbox One / Series |
| 0B05:1D04 | ASUS ROG RAIKIRI II XBOX WIRELESS | Xbox One / Series |
| 0C12:0005 | Intec wireless | Xbox |
| 0C12:8801 | Nyko Xbox Controller | Xbox |
| 0C12:8802 | Zeroplus Xbox Controller | Xbox |
| 0C12:8809 | RedOctane Xbox Dance Pad | Xbox |
| 0C12:880A | Pelican Eclipse PL-2023 | Xbox |
| 0C12:8810 | Zeroplus Xbox Controller | Xbox |
| 0C12:9902 | HAMA VibraX - *FAULTY HARDWARE* | Xbox |
| 0D2F:0002 | Andamiro Pump It Up pad | Xbox |
| 0DB0:1901 | Micro Star International Xbox360 Controller for Windows | Xbox 360 |
| 0E4C:1097 | Radica Gamester Controller | Xbox |
| 0E4C:1103 | Radica Gamester Reflex | Xbox |
| 0E4C:2390 | Radica Games Jtech Controller | Xbox |
| 0E4C:3510 | Radica Gamester | Xbox |
| 0E6F:0003 | Logic3 Freebird wireless Controller | Xbox |
| 0E6F:0005 | Eclipse wireless Controller | Xbox |
| 0E6F:0006 | Edge wireless Controller | Xbox |
| 0E6F:0008 | After Glow Pro Controller | Xbox |
| 0E6F:0105 | HSM3 Xbox360 dancepad | Xbox 360 |
| 0E6F:0113 | Afterglow AX.1 Gamepad for Xbox 360 | Xbox 360 |
| 0E6F:011F | Rock Candy Gamepad Wired Controller | Xbox 360 |
| 0E6F:0131 | PDP EA Sports Controller | Xbox 360 |
| 0E6F:0133 | Xbox 360 Wired Controller | Xbox 360 |
| 0E6F:0139 | Afterglow Prismatic Wired Controller | Xbox One / Series |
| 0E6F:013A | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:0146 | Rock Candy Wired Controller for Xbox One | Xbox One / Series |
| 0E6F:0147 | PDP Marvel Xbox 360 Controller | Xbox 360 |
| 0E6F:015C | PDP Xbox One Arcade Stick | Xbox One / Series |
| 0E6F:015D | PDP Mirror's Edge Official Wired Controller for Xbox One | Xbox One / Series |
| 0E6F:0161 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:0162 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:0163 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:0164 | PDP Battlefield One | Xbox One / Series |
| 0E6F:0165 | PDP Titanfall 2 | Xbox One / Series |
| 0E6F:0201 | Pelican PL-3601 'TSZ' Wired Xbox 360 Controller | Xbox 360 |
| 0E6F:0213 | Afterglow Gamepad for Xbox 360 | Xbox 360 |
| 0E6F:021F | Rock Candy Gamepad for Xbox 360 | Xbox 360 |
| 0E6F:0246 | Rock Candy Gamepad for Xbox One 2015 | Xbox One / Series |
| 0E6F:024C | PDP Victrix Pro BFG Wired Controller for Xbox | Xbox One / Series |
| 0E6F:02A0 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:02A1 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:02A2 | PDP Wired Controller for Xbox One - Crimson Red | Xbox One / Series |
| 0E6F:02A4 | PDP Wired Controller for Xbox One - Stealth Series | Xbox One / Series |
| 0E6F:02A6 | PDP Wired Controller for Xbox One - Camo Series | Xbox One / Series |
| 0E6F:02A7 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:02A8 | PDP Xbox One Controller | Xbox One / Series |
| 0E6F:02AB | PDP Controller for Xbox One | Xbox One / Series |
| 0E6F:02AD | PDP Wired Controller for Xbox One - Stealth Series | Xbox One / Series |
| 0E6F:02B3 | Afterglow Prismatic Wired Controller | Xbox One / Series |
| 0E6F:02B8 | Afterglow Prismatic Wired Controller | Xbox One / Series |
| 0E6F:0301 | Logic3 Controller | Xbox 360 |
| 0E6F:0346 | Rock Candy Gamepad for Xbox One 2016 | Xbox One / Series |
| 0E6F:0401 | Logic3 Controller | Xbox 360 |
| 0E6F:0413 | Afterglow AX.1 Gamepad for Xbox 360 | Xbox 360 |
| 0E6F:0501 | PDP Xbox 360 Controller | Xbox 360 |
| 0E6F:F900 | PDP Afterglow AX.1 | Xbox 360 |
| 0E8F:0201 | SmartJoy Frag Xpad/PS2 adaptor | Xbox |
| 0E8F:3008 | Generic xbox control (dealextreme) | Xbox |
| 0F0D:000A | Hori Co. DOA4 FightStick | Xbox 360 |
| 0F0D:000C | Hori PadEX Turbo | Xbox 360 |
| 0F0D:000D | Hori Fighting Stick EX2 | Xbox 360 |
| 0F0D:0016 | Hori Real Arcade Pro.EX | Xbox 360 |
| 0F0D:001B | Hori Real Arcade Pro VX | Xbox 360 |
| 0F0D:0063 | Hori Real Arcade Pro Hayabusa (USA) Xbox One | Xbox One / Series |
| 0F0D:0067 | HORIPAD ONE | Xbox One / Series |
| 0F0D:0078 | Hori Real Arcade Pro V Kai Xbox One | Xbox One / Series |
| 0F0D:008C | Real Arcade Pro 4 | Xbox 360 |
| 0F0D:00C5 | Hori Fighting Commander ONE | Xbox One / Series |
| 0F0D:00DC | HORIPAD FPS for Nintendo Switch | Xbox 360 |
| 0F0D:0151 | Hori Racing Wheel Overdrive for Xbox Series X | Xbox One / Series |
| 0F0D:0152 | Hori Racing Wheel Overdrive for Xbox Series X | Xbox One / Series |
| 0F0D:01B1 | HORI Taiko Controller DX | Xbox 360 |
| 0F0D:01B2 | HORI Taiko No Tatsujin Drum Controller | Xbox One / Series |
| 0F30:010B | Philips Recoil | Xbox |
| 0F30:0202 | Joytech Advanced Controller | Xbox |
| 0F30:8888 | BigBen XBMiniPad Controller | Xbox |
| 102C:FF0C | Joytech Wireless Advanced Controller | Xbox |
| 1038:1430 | SteelSeries Stratus Duo | Xbox 360 |
| 1038:1431 | SteelSeries Stratus Duo | Xbox 360 |
| 10F5:7005 | Turtle Beach Recon Controller | Xbox One / Series |
| 10F5:7008 | Turtle Beach Recon Controller | Xbox One / Series |
| 10F5:7073 | Turtle Beach Stealth Ultra Controller | Xbox One / Series |
| 11C9:55F0 | Nacon GC-100XF | Xbox 360 |
| 11FF:0511 | PXN V900 | Xbox 360 |
| 1209:2882 | Ardwiino Controller | Xbox 360 |
| 12AB:0004 | Honey Bee Xbox360 dancepad | Xbox 360 |
| 12AB:0301 | PDP AFTERGLOW AX.1 | Xbox 360 |
| 12AB:0303 | Mortal Kombat Klassic FightStick | Xbox 360 |
| 12AB:8809 | Xbox DDR dancepad | Xbox |
| 1430:4748 | RedOctane Guitar Hero X-plorer | Xbox 360 |
| 1430:8888 | TX6500+ Dance Pad (first generation) | Xbox |
| 1430:F801 | RedOctane Controller | Xbox 360 |
| 146B:0601 | BigBen Interactive XBOX 360 Controller | Xbox 360 |
| 146B:0604 | Bigben Interactive DAIJA Arcade Stick | Xbox 360 |
| 1532:0A00 | Razer Atrox Arcade Stick | Xbox One / Series |
| 1532:0A03 | Razer Wildcat | Xbox One / Series |
| 1532:0A29 | Razer Wolverine V2 | Xbox One / Series |
| 1532:0A57 | Razer Wolverine V3 Pro (Wired) | Xbox 360 |
| 1532:0A59 | Razer Wolverine V3 Pro (2.4 GHz Dongle) | Xbox 360 |
| 15E4:3F00 | Power A Mini Pro Elite | Xbox 360 |
| 15E4:3F0A | Xbox Airflo wired controller | Xbox 360 |
| 15E4:3F10 | Batarang Xbox 360 controller | Xbox 360 |
| 162E:BEEF | Joytech Neo-Se Take2 | Xbox 360 |
| 1689:FD00 | Razer Onza Tournament Edition | Xbox 360 |
| 1689:FD01 | Razer Onza Classic Edition | Xbox 360 |
| 1689:FE00 | Razer Sabertooth | Xbox 360 |
| 16D0:1103 | Azeron Cyro | Xbox 360 |
| 16D0:113C | Azeron Cyborg | Xbox 360 |
| 16D0:1192 | Azeron Classic/Compact | Xbox 360 |
| 16D0:1212 | Azeron Cyro Lefty | Xbox 360 |
| 16D0:12F7 | Azeron Cyborg II | Xbox 360 |
| 16D0:13EA | Azeron Keyzen | Xbox 360 |
| 17EF:6182 | Lenovo Legion Controller for Windows | Xbox 360 |
| 1949:041A | Amazon Game Controller | Xbox 360 |
| 1A86:E310 | Legion Go S | Xbox 360 |
| 1B1C:2B1F | Corsair Novablade Pro (Wired) | Xbox 360 |
| 1B1C:2B2B | Corsair Novablade Pro (2.4 GHz Receiver) | Xbox 360 |
| 1BAD:0002 | Harmonix Rock Band Guitar | Xbox 360 |
| 1BAD:0003 | Harmonix Rock Band Drumkit | Xbox 360 |
| 1BAD:0130 | Ion Drum Rocker | Xbox 360 |
| 1BAD:F016 | Mad Catz Xbox 360 Controller | Xbox 360 |
| 1BAD:F018 | Mad Catz Street Fighter IV SE Fighting Stick | Xbox 360 |
| 1BAD:F019 | Mad Catz Brawlstick for Xbox 360 | Xbox 360 |
| 1BAD:F021 | Mad Cats Ghost Recon FS GamePad | Xbox 360 |
| 1BAD:F023 | MLG Pro Circuit Controller (Xbox) | Xbox 360 |
| 1BAD:F025 | Mad Catz Call Of Duty | Xbox 360 |
| 1BAD:F027 | Mad Catz FPS Pro | Xbox 360 |
| 1BAD:F028 | Street Fighter IV FightPad | Xbox 360 |
| 1BAD:F02E | Mad Catz Fightpad | Xbox 360 |
| 1BAD:F030 | Mad Catz Xbox 360 MC2 MicroCon Racing Wheel | Xbox 360 |
| 1BAD:F036 | Mad Catz MicroCon GamePad Pro | Xbox 360 |
| 1BAD:F038 | Street Fighter IV FightStick TE | Xbox 360 |
| 1BAD:F039 | Mad Catz MvC2 TE | Xbox 360 |
| 1BAD:F03A | Mad Catz SFxT Fightstick Pro | Xbox 360 |
| 1BAD:F03D | Street Fighter IV Arcade Stick TE - Chun Li | Xbox 360 |
| 1BAD:F03E | Mad Catz MLG FightStick TE | Xbox 360 |
| 1BAD:F03F | Mad Catz FightStick SoulCaliber | Xbox 360 |
| 1BAD:F042 | Mad Catz FightStick TES+ | Xbox 360 |
| 1BAD:F080 | Mad Catz FightStick TE2 | Xbox 360 |
| 1BAD:F501 | HoriPad EX2 Turbo | Xbox 360 |
| 1BAD:F502 | Hori Real Arcade Pro.VX SA | Xbox 360 |
| 1BAD:F503 | Hori Fighting Stick VX | Xbox 360 |
| 1BAD:F504 | Hori Real Arcade Pro. EX | Xbox 360 |
| 1BAD:F505 | Hori Fighting Stick EX2B | Xbox 360 |
| 1BAD:F506 | Hori Real Arcade Pro.EX Premium VLX | Xbox 360 |
| 1BAD:F900 | Harmonix Xbox 360 Controller | Xbox 360 |
| 1BAD:F901 | Gamestop Xbox 360 Controller | Xbox 360 |
| 1BAD:F903 | Tron Xbox 360 controller | Xbox 360 |
| 1BAD:F904 | PDP Versus Fighting Pad | Xbox 360 |
| 1BAD:F906 | MortalKombat FightStick | Xbox 360 |
| 1BAD:FA01 | MadCatz GamePad | Xbox 360 |
| 1BAD:FD00 | Razer Onza TE | Xbox 360 |
| 1BAD:FD01 | Razer Onza | Xbox 360 |
| 1EE9:1590 | ZOTAC Gaming Zone | Xbox 360 |
| 20BC:5134 | BETOP BTP-KP50B Xinput Dongle | Xbox 360 |
| 20BC:514A | BETOP BTP-KP50C Xinput Dongle | Xbox 360 |
| 20D6:2001 | BDA Xbox Series X Wired Controller | Xbox One / Series |
| 20D6:2009 | PowerA Enhanced Wired Controller for Xbox Series X\|S | Xbox One / Series |
| 20D6:2064 | PowerA Wired Controller for Xbox | Xbox One / Series |
| 20D6:281F | PowerA Wired Controller For Xbox 360 | Xbox 360 |
| 20D6:400B | PowerA FUSION Pro 4 Wired Controller | Xbox One / Series |
| 20D6:890B | PowerA MOGA XP-Ultra Controller | Xbox One / Series |
| 2345:E00B | Machenike G5 Pro Controller | Xbox 360 |
| 24C6:5000 | Razer Atrox Arcade Stick | Xbox 360 |
| 24C6:5300 | PowerA MINI PROEX Controller | Xbox 360 |
| 24C6:5303 | Xbox Airflo wired controller | Xbox 360 |
| 24C6:530A | Xbox 360 Pro EX Controller | Xbox 360 |
| 24C6:531A | PowerA Pro Ex | Xbox 360 |
| 24C6:5397 | FUS1ON Tournament Controller | Xbox 360 |
| 24C6:541A | PowerA Xbox One Mini Wired Controller | Xbox One / Series |
| 24C6:542A | Xbox ONE spectra | Xbox One / Series |
| 24C6:543A | PowerA Xbox One wired controller | Xbox One / Series |
| 24C6:5500 | Hori XBOX 360 EX 2 with Turbo | Xbox 360 |
| 24C6:5501 | Hori Real Arcade Pro VX-SA | Xbox 360 |
| 24C6:5502 | Hori Fighting Stick VX Alt | Xbox 360 |
| 24C6:5503 | Hori Fighting Edge | Xbox 360 |
| 24C6:5506 | Hori SOULCALIBUR V Stick | Xbox 360 |
| 24C6:550D | Hori GEM Xbox controller | Xbox 360 |
| 24C6:550E | Hori Real Arcade Pro V Kai 360 | Xbox 360 |
| 24C6:5510 | Hori Fighting Commander ONE (Xbox 360/PC Mode) | Xbox 360 |
| 24C6:551A | PowerA FUSION Pro Controller | Xbox One / Series |
| 24C6:561A | PowerA FUSION Controller | Xbox One / Series |
| 24C6:581A | ThrustMaster XB1 Classic Controller | Xbox One / Series |
| 24C6:5B00 | ThrustMaster Ferrari 458 Racing Wheel | Xbox 360 |
| 24C6:5B02 | Thrustmaster, Inc. GPX Controller | Xbox 360 |
| 24C6:5B03 | Thrustmaster Ferrari 458 Racing Wheel | Xbox 360 |
| 24C6:5D04 | Razer Sabertooth | Xbox 360 |
| 24C6:FAFE | Rock Candy Gamepad for Xbox 360 | Xbox 360 |
| 2563:058D | OneXPlayer Gamepad | Xbox 360 |
| 294B:3303 | Snakebyte GAMEPAD BASE X | Xbox One / Series |
| 294B:3404 | Snakebyte GAMEPAD RGB X | Xbox One / Series |
| 2993:2001 | TECNO Pocket Go | Xbox 360 |
| 2DC8:2000 | 8BitDo Pro 2 Wired Controller fox Xbox | Xbox One / Series |
| 2DC8:200A | 8BitDo M30 Wired Controller for Xbox | Xbox One / Series |
| 2DC8:200F | 8BitDo Ultimate 3-mode Controller for Xbox | Xbox One / Series |
| 2DC8:201E | 8BitDo Pro 2 Wired Controller for Xbox | Xbox One / Series |
| 2DC8:3106 | 8BitDo Ultimate Wireless / Pro 2 Wired Controller | Xbox 360 |
| 2DC8:3109 | 8BitDo Ultimate Wireless Bluetooth | Xbox 360 |
| 2DC8:310A | 8BitDo Ultimate 2C Wireless Controller | Xbox 360 |
| 2DC8:310B | 8BitDo Ultimate 2 Wireless Controller | Xbox 360 |
| 2DC8:6001 | 8BitDo SN30 Pro | Xbox 360 |
| 2E24:0423 | Hyperkin DuchesS Xbox One pad | Xbox One / Series |
| 2E24:0652 | Hyperkin Duke X-Box One pad | Xbox One / Series |
| 2E24:072E | Hyperkin Xenon Controller | Xbox One / Series |
| 2E24:1688 | Hyperkin X91 X-Box One pad | Xbox One / Series |
| 2E95:0504 | SCUF Gaming Controller | Xbox One / Series |
| 31E3:1100 | Wooting One | Xbox 360 |
| 31E3:1200 | Wooting Two | Xbox 360 |
| 31E3:1210 | Wooting Lekker | Xbox 360 |
| 31E3:1220 | Wooting Two HE | Xbox 360 |
| 31E3:1230 | Wooting Two HE (ARM) | Xbox 360 |
| 31E3:1300 | Wooting 60HE (AVR) | Xbox 360 |
| 31E3:1310 | Wooting 60HE (ARM) | Xbox 360 |
| 3285:0603 | Nacon Pro Compact controller for Xbox | Xbox One / Series |
| 3285:0607 | Nacon GC-100 | Xbox 360 |
| 3285:0614 | Nacon Pro Compact | Xbox One / Series |
| 3285:0646 | Nacon Pro Compact | Xbox One / Series |
| 3285:0662 | Nacon Revolution5 Pro | Xbox 360 |
| 3285:0663 | Nacon Evol-X | Xbox One / Series |
| 3507:000B | ZENAIM LEVERLESS | Xbox 360 |
| 3537:1004 | GameSir T4 Kaleid | Xbox 360 |
| 3537:100F | GameSir Nova 2 Lite | Xbox 360 |
| 3537:1010 | GameSir G7 SE | Xbox One / Series |
| 3537:1040 | GameSir Nova Lite | Xbox 360 |
| 3537:109C | GameSir G7 Pro | Xbox One / Series |
| 3651:1000 | CRKD SG | Xbox 360 |
| 366C:0005 | ByoWave Proteus Controller | Xbox One / Series |
| 3767:0101 | Fanatec Speedster 3 Forceshock Wheel | Xbox |
| 37D7:2501 | Flydigi Apex 5 | Xbox 360 |
| 413D:2104 | Black Shark Green Ghost Gamepad | Xbox 360 |
| FFFF:FFFF | Chinese-made Xbox Controller | Xbox |
