#!/bin/sh
# cc98.sh OUT.EXE src.c [...]  - freestanding Windows 98 compatible i386 PE (no C runtime)
out="$1"; shift
exec i686-w64-mingw32-gcc -m32 -Os -march=i586 -mno-sse -ffreestanding -fno-stack-protector \
  -fno-asynchronous-unwind-tables -fno-ident -nostdlib -mno-stack-arg-probe -fno-builtin \
  -finput-charset=UTF-8 -fexec-charset=CP1250 -Wall -Wno-unused-variable -Wno-unused-function \
  "$@" -o "$out" \
  -Wl,-e,_start@0 -Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
  -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 -Wl,--disable-dynamicbase \
  -Wl,--disable-nxcompat -Wl,--disable-reloc-section -s \
  -lkernel32 -luser32 -ladvapi32 -lgcc
