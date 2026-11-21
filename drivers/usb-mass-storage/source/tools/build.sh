#!/bin/sh
# build.sh [OUT.EXE] - build NUSB36UNIV.EXE from source/ (default output: ../../NUSB36UNIV.EXE)
#
# Needs: i686-w64-mingw32-gcc (mingw-w64), python3 with pefile, a C compiler for lzxcab.
# Env:   LZXCAB    LZX CAB compressor (default: $WORK/lzxcab, built from tools/lzxtool + $LIBLZX)
#        LIBLZX    checkout of https://github.com/elasota/liblzx (only to build lzxcab)
#        SFX_STUB  IExpress stub (default: the current NUSB36UNIV.EXE)
#        WORK      work folder (default: <repository>/tmp/nusb36univ-build, git-ignored)
#        PYTHON    python with pefile (default: python3)
set -e
here=$(cd "$(dirname "$0")" && pwd)          # source/tools
src=$(dirname "$here")                       # source
root=$(dirname "$src")                       # drivers/usb-mass-storage
out=${1:-$root/NUSB36UNIV.EXE}
work=${WORK:-$(cd "$root/../.." && pwd)/tmp/nusb36univ-build}
py=${PYTHON:-python3}
mkdir -p "$work/pkg"
find "$work/pkg" -type f -delete

# setup program
"$here/cc98.sh" "$work/NUSBSETU.EXE" "$src/setup/"*.c

# package = payload + setup program
cp -p "$src/payload/"* "$work/pkg/"
cp -p "$work/NUSBSETU.EXE" "$work/pkg/"

# LZX compressor
if [ -z "$LZXCAB" ]; then
  LZXCAB="$work/lzxcab"
  if [ ! -x "$LZXCAB" ]; then
    [ -n "$LIBLZX" ] || { echo "set LZXCAB (lzxcab binary) or LIBLZX (liblzx checkout)" >&2; exit 1; }
    cc -O2 -fno-fast-math -I "$here/lzxtool/shim" -I "$LIBLZX/liblzx" -o "$LZXCAB" "$here/lzxtool/lzxcab.c" \
      "$LIBLZX/liblzx/liblzx_compress_common.c" "$LIBLZX/liblzx/liblzx_lzx_common.c" "$LIBLZX/liblzx/liblzx_lzx_compress.c"
  fi
fi
export LZXCAB

# self-extractor
cp -p "${SFX_STUB:-$root/NUSB36UNIV.EXE}" "$work/stub.exe"
"$py" "$here/build_sfx.py" "$work/stub.exe" "$work/pkg" "$out" --cab "$work/nusb36univ.cab" \
  --title "NUSB 3.6 Universal - USB mass storage" --run NUSBSETU.EXE
ls -l "$out"
