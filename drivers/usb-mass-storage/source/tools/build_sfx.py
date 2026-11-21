"""build_sfx.py STUB PKGDIR OUT [--cab FILE] [--title TEXT] [--run CMD]

Put the files of PKGDIR into an IExpress (wextract) self-extractor. STUB is an IExpress EXE made
from the NUSB 3.6 one (normally the previous NUSB36UNIV.EXE); only CABINET, FILESIZES, TITLE,
RUNPROGRAM and LICENSE are replaced, the extractor's (English) UI is kept. The CAB is LZX:21 -
this wextract rejects MSZIP. Needs pefile and lzxcab ($LZXCAB)."""
import argparse, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rsrc import read_all, replace_rsrc
from cabwriter import write_cab

ap = argparse.ArgumentParser()
ap.add_argument('stub'); ap.add_argument('pkg'); ap.add_argument('out')
ap.add_argument('--cab', default=None)
ap.add_argument('--title', default='NUSB 3.6 Universal - USB mass storage')
ap.add_argument('--run', default='NUSBSETU.EXE')
a = ap.parse_args()

files = sorted(os.listdir(a.pkg))
cab = a.cab or a.out + '.cab'
if os.path.exists(cab):
    os.remove(cab)
write_cab(cab, [os.path.join(a.pkg, f) for f in files], compress='lzx')
sizes = [os.path.getsize(os.path.join(a.pkg, f)) for f in files]
# FILESIZES: KB needed for cluster sizes 512 B .. 64 KB, then the total in bytes
fs = [sum(((s + c - 1) // c) * c for s in sizes) // 1024 for c in [512 * (2 ** i) for i in range(8)]] + [sum(sizes)]

_, e = read_all(a.stub)
def setrc(name, data):
    hit = [x for x in e if x[0] == 10 and x[1] == name]
    assert len(hit) == 1, name
    hit[0][3] = data
setrc('CABINET', open(cab, 'rb').read())
setrc('FILESIZES', struct.pack('<9I', *fs))
setrc('TITLE', a.title.encode('ascii') + b'\0')
setrc('RUNPROGRAM', a.run.encode('ascii') + b'\0')
setrc('LICENSE', b'<None>\0')
replace_rsrc(a.stub, e, a.out)
print('files', len(files), 'cab', os.path.getsize(cab), 'exe', os.path.getsize(a.out))
