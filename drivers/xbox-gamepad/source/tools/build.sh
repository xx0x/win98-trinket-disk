#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# build.sh [OUT.SYS] - build XGP98.SYS (default: ../../XGP98.SYS)
# Needs: i686-w64-mingw32-gcc (mingw-w64), python3 with pefile ($PYTHON).
# XGP_CFLAGS=-DXGP_DIAG builds the diagnostic version (logs to HKLM\Software\XGP98, key must exist).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(dirname "$here")
root=$(dirname "$src")
out=${1:-$root/XGP98.SYS}
py=${PYTHON:-python3}
inc=$(i686-w64-mingw32-gcc -print-sysroot)/i686-w64-mingw32/include
i686-w64-mingw32-gcc -Os -march=i586 -mno-sse -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-asynchronous-unwind-tables -fno-ident -fno-zero-initialized-in-bss -fno-common -mno-stack-arg-probe -Wall -Wno-multichar \
  -D_X86_=1 $XGP_CFLAGS -D_WIN32_WINNT=0x0601 -DNTDDI_VERSION=0x06010000 -I"$inc/ddk" \
  -nostdlib -o "$out" "$src/driver/xgp98.c" \
  -Wl,--subsystem,native -Wl,-e,_DriverEntry@8 -Wl,--image-base,0x10000 \
  -Wl,--section-alignment,0x80 -Wl,--file-alignment,0x80 -Wl,--enable-reloc-section \
  -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -s \
  -lntoskrnl -lhal -lhidclass -lgcc
"$py" "$here/fixpe.py" "$out"
