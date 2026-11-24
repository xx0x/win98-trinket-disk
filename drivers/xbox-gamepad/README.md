# XPAD98 – Xbox One / Xbox Series controller for Windows 98 SE

A USB driver that makes Xbox One and Xbox Series X|S controllers work as an
ordinary game controller on **Windows 98 Second Edition**: Control Panel →
*Game Controllers*, DirectInput and joystick-aware games see it like any USB
gamepad. Connection over a USB cable only.

| | |
|---|---|
| [`XPAD98.INF`](XPAD98.INF), [`XPAD98.SYS`](XPAD98.SYS) | the driver (copy both to a floppy, CD or folder) |
| [`source/`](source/) | driver source and build tools |

Supported (USB IDs in the INF): Xbox One Controller (045E:02D1, 02DD), Xbox One
Elite (02E3), Xbox One S (02EA), Xbox Elite Series 2 (0B00), Xbox Series X|S
(0B12). Other controllers speaking the same protocol (GIP) can be added to the
INF.

## Install

1. Connect the controller. Windows finds a new *USB Device*.
2. In the *Add New Hardware Wizard* choose *Search for the best driver* and
   point it at the folder / drive with `XPAD98.INF` (or Device Manager →
   the device → *Update Driver* → *Have Disk*).
3. Windows installs *Xbox … Controller* and then its own *HID-compliant game
   controller* (it may ask for the Windows 98 CD). If it asks to restart, do so.
4. Control Panel → *Game Controllers* lists the controller with status *OK*;
   *Properties* → *Test* shows all axes, the hat and the buttons.

It needs nothing else: the USB and HID drivers of Windows 98 SE are enough, and
it works the same with [NUSB](../usb-mass-storage/) installed (it doesn't use
or replace any of its files).

## Mapping

| Controller | Windows 98 | |
|---|---|---|
| Left stick | X / Y axes (*1 (x) and 2 (y)*) | |
| Right stick | Z / Rz axes (*3*, *4*) | like most DirectInput dual-stick pads |
| Left / right trigger | Rx / Ry axes (*5*, *6*) | separate, 0–1023 |
| D-pad | Point of view hat | 8 directions |
| A, B, X, Y | buttons 1–4 | |
| LB, RB | buttons 5, 6 | |
| View, Menu | buttons 7, 8 | |
| Left / right stick press | buttons 9, 10 | |
| Xbox button | button 11 | |
| Share (Series X\|S) | button 12 | |

Not supported: rumble, the headset jack, wireless (Xbox Wireless Adapter,
Bluetooth).

## How it works

`XPAD98.SYS` is a WDM HID minidriver – the same kind of driver as Windows 98's
own `HIDUSB.SYS` – so Windows' HID class driver and its standard joystick
support (`JOYHID.VXD`, `VJOYD.VXD`) do the rest. The driver

* selects the controller's USB configuration (all interfaces at alternate
  setting 0 – the audio interface stays off) and opens the interrupt pipes of
  the data interface,
* sends the controller's start-up packets (power on, LED, authentication done),
* converts its input packets into reports of a HID game pad (15 bytes: four
  16-bit stick axes, two trigger axes, hat, 12 buttons), acknowledges the Xbox
  button packet as the controller requires, and repeats the current state on
  the controller's periodic status packets so that a program opening the
  joystick later sees a valid state.

The protocol details follow Linux's `xpad` driver (`drivers/input/joystick/xpad.c`).
The driver only uses kernel functions that Windows 98 SE provides and no
`USBD.SYS` functions; the build checks both its imports and that the image can
be loaded by the Windows 98 driver loader.

## Tested

UTM / QEMU with a real Xbox Series X|S controller passed through over USB,
2026-11-24. In every case *Game Controllers* shows *Xbox Series X|S Controller –
OK* and the sticks, triggers, hat and buttons respond on the Test page:

* **English Windows 98 SE** with NUSB 3.6 Universal – all axes, the hat (8
  directions) and all 12 buttons checked one by one.
* **Czech Windows 98 SE without NUSB** – the pad on a USB 1.1 controller, no
  restart needed after installing.
* **Czech Windows 98 SE with NUSB** – the pad on the USB 2.0 controller; after
  installing NUSB the already installed XPAD98 kept working.
* **Korean Windows 98 SE** – same behaviour as on Czech.

Not tested yet: other controller models, real hardware.

Note for UTM: UTM puts passed-through USB devices on the VM's USB 2.0
controller, which Windows 98 only sees with a USB 2.0 driver such as NUSB (QEMU
doesn't hand them to the USB 1.1 companions). To test without NUSB, the
redirection slots have to be moved to the VM's USB 1.1 (PIIX3) controller, e.g.
behind an emulated hub, through QEMU's monitor (QMP `device_add usb-hub` on
`usb-bus.0`, then `usb-redir` devices on its ports). On a real PC the pad simply
runs on a USB 1.1 port without NUSB.

## Building

Requirements: mingw-w64 (`i686-w64-mingw32-gcc`) and Python 3 with `pefile`.

```bash
sh source/tools/build.sh
```

Writes `XPAD98.SYS`. `XPAD_CFLAGS=-DXPAD_DIAG` builds a diagnostic version that
logs its start-up steps and packet counters to `HKLM\Software\XPAD98` (create
the key first; export it with `regedit /e`).

| File | Purpose |
|---|---|
| `source/driver/xpad98.c` | the driver |
| `source/tools/build.sh` | compiler / linker flags for a Windows 98 WDM driver |
| `source/tools/fixpe.py` | import check, 1:1 section layout check, PE checksum |

## Credits / licence

* Protocol information: Linux `xpad` driver and its contributors.
* Driver – unofficial, 2026. Use at your own risk.
