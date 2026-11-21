"""Minimal Microsoft Cabinet writer (single folder; stored, MSZIP or LZX:21 via lzxcab), conservative for
old FDI (wextract) readers. The NUSB 3.6 wextract accepts LZX:21 and stored, not MSZIP.

Each CFDATA block holds <= 32768 uncompressed bytes, compressed independently as
'CK' + raw deflate stream (BFINAL set). Data block checksums are 0 (= not checked by FDI).
"""
import struct, zlib, os, time, sys


def dos_datetime(t):
    lt = time.localtime(t)
    d = ((lt.tm_year - 1980) << 9) | (lt.tm_mon << 5) | lt.tm_mday
    tm = (lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2)
    return d, tm


LZXCAB = os.environ.get('LZXCAB') or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lzxtool', 'lzxcab')


def lzx_blocks(data, bits=21, level=100):
    import subprocess, tempfile
    with tempfile.TemporaryDirectory() as td:
        i = os.path.join(td, 'in.bin'); o = os.path.join(td, 'out.chunks')
        open(i, 'wb').write(data)
        subprocess.run([LZXCAB, str(bits), str(level), i, o], check=True, capture_output=True)
        b = open(o, 'rb').read()
    blocks, p = [], 0
    while p < len(b):
        cs, us = struct.unpack_from('<II', b, p); p += 8
        blocks.append((b[p:p + cs], us)); p += cs
    return blocks


def write_cab(out, paths, compress=True):
    """compress: False = stored, True/'mszip' = MSZIP, 'lzx' = LZX:21 (what IExpress uses)."""
    files = []
    data = bytearray()
    for p in paths:
        b = open(p, 'rb').read()
        name = os.path.basename(p).encode('cp1250')
        d, t = dos_datetime(os.path.getmtime(p))
        files.append((len(b), len(data), d, t, 0x20, name))
        data += b
    blocks = []
    if compress == 'lzx':
        blocks = lzx_blocks(bytes(data))
        assert sum(u for _, u in blocks) == len(data)
    for i in (range(0, len(data), 32768) if compress != 'lzx' else []):
        chunk = bytes(data[i:i + 32768])
        if compress:
            c = zlib.compressobj(9, zlib.DEFLATED, -15)
            comp = b'CK' + c.compress(chunk) + c.flush(zlib.Z_FINISH)
        else:
            comp = chunk
        blocks.append((comp, len(chunk)))
    hdr_size = 36
    folder_size = 8
    files_size = sum(16 + len(f[5]) + 1 for f in files)
    off_files = hdr_size + folder_size
    off_data = off_files + files_size
    total = off_data + sum(8 + len(c) for c, _ in blocks)
    out_b = bytearray()
    out_b += struct.pack('<4sIIIIIBBHHHHH', b'MSCF', 0, total, 0, off_files, 0, 3, 1, 1, len(files), 0, 0x1234, 0)
    tcomp = 0x1503 if compress == 'lzx' else (1 if compress else 0)
    out_b += struct.pack('<IHH', off_data, len(blocks), tcomp)
    for size, uoff, d, t, attr, name in files:
        out_b += struct.pack('<IIHHHH', size, uoff, 0, d, t, attr) + name + b'\0'
    assert len(out_b) == off_data
    for comp, ulen in blocks:
        out_b += struct.pack('<IHH', 0, len(comp), ulen) + comp
    assert len(out_b) == total
    open(out, 'wb').write(out_b)
    return total


if __name__ == '__main__':
    print(write_cab(sys.argv[1], sys.argv[2:]))
