"""PE resource helpers: read all resources, rebuild .rsrc, string tables, dialogs, menus."""
import struct, pefile
def read_all(path):
    pe=pefile.PE(path)
    out=[]
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        tid=t.id if t.name is None else str(t.name)
        for n in t.directory.entries:
            nid=n.id if n.name is None else str(n.name)
            for l in n.directory.entries:
                ds=l.data.struct
                out.append([tid,nid,l.id,pe.get_data(ds.OffsetToData,ds.Size),ds.CodePage])
    return pe,out
def _sortkey(k):
    return (0,k.upper()) if isinstance(k,str) else (1,k)
def build_rsrc(entries, rva):
    """entries: list of [type,name,lang,data,codepage]; returns bytes of .rsrc placed at rva"""
    tree={}
    for t,n,l,d,cp in entries: tree.setdefault(t,{}).setdefault(n,{})[l]=(d,cp)
    # layout: directories, then strings, then data entries, then data
    dirs=[]  # (node, entries list)
    def dirsize(cnt): return 16+8*cnt
    # compute directory blob order: root, type dirs, name dirs
    types=sorted(tree,key=_sortkey)
    order=[('root',None)]
    for t in types: order.append(('type',t))
    for t in types:
        for n in sorted(tree[t],key=_sortkey): order.append(('name',(t,n)))
    off=0; dir_off={}
    for kind,key in order:
        dir_off[(kind,key)]=off
        if kind=='root': cnt=len(types)
        elif kind=='type': cnt=len(tree[key])
        else: cnt=len(tree[key[0]][key[1]])
        off+=dirsize(cnt)
    # strings
    strs={}
    for t in types:
        if isinstance(t,str): strs.setdefault(t,None)
        for n in tree[t]:
            if isinstance(n,str): strs.setdefault(n,None)
    for s in strs:
        strs[s]=off; off+=2+2*len(s)
    off=(off+3)&~3
    # data entries
    de_off={}
    for t in types:
        for n in sorted(tree[t],key=_sortkey):
            for l in sorted(tree[t][n]):
                de_off[(t,n,l)]=off; off+=16
    # data
    data_off={}
    for t in types:
        for n in sorted(tree[t],key=_sortkey):
            for l in sorted(tree[t][n]):
                off=(off+7)&~7
                data_off[(t,n,l)]=off; off+=len(tree[t][n][l][0])
    total=(off+7)&~7
    b=bytearray(total)
    def put_dir(o,keys,targets,is_leaf_parent):
        named=[k for k in keys if isinstance(k,str)]; ids=[k for k in keys if not isinstance(k,str)]
        struct.pack_into('<IIHHHH',b,o,0,0,4,0,len(named),len(ids))
        p=o+16
        for k,tgt in zip(keys,targets):
            nm=(0x80000000|strs[k]) if isinstance(k,str) else k
            struct.pack_into('<II',b,p,nm,tgt); p+=8
    tk=types
    put_dir(0,tk,[0x80000000|dir_off[('type',t)] for t in tk],False)
    for t in types:
        nk=sorted(tree[t],key=_sortkey)
        put_dir(dir_off[('type',t)],nk,[0x80000000|dir_off[('name',(t,n))] for n in nk],False)
        for n in nk:
            lk=sorted(tree[t][n])
            put_dir(dir_off[('name',(t,n))],lk,[de_off[(t,n,l)] for l in lk],True)
    for s,o in strs.items():
        struct.pack_into('<H',b,o,len(s)); b[o+2:o+2+2*len(s)]=s.encode('utf-16le')
    for k,o in de_off.items():
        d,cp=tree[k[0]][k[1]][k[2]]
        struct.pack_into('<IIII',b,o,rva+data_off[k],len(d),cp,0)
        b[data_off[k]:data_off[k]+len(d)]=d
    return bytes(b)
def pe_checksum(data, csum_off):
    total=0; n=len(data)
    d=bytes(data)+b'\0'*((-n)%4)
    for k in range(0,len(d),4):
        if k==csum_off: continue
        total+=struct.unpack_from('<I',d,k)[0]
        total=(total&0xffffffff)+(total>>32)
    total=(total&0xffff)+(total>>16); total=(total&0xffff)+(total>>16)
    return (total+n)&0xffffffff
def replace_rsrc(path_in, entries, path_out):
    """Rebuild .rsrc with manual header patching (no pefile.write, which corrupts delay imports)."""
    raw=bytearray(open(path_in,'rb').read())
    pe=pefile.PE(data=bytes(raw),fast_load=True)
    peo=struct.unpack_from('<I',raw,0x3c)[0]; opt=peo+24
    assert struct.unpack_from('<H',raw,opt)[0]==0x10b
    nsec=struct.unpack_from('<H',raw,peo+6)[0]; soh=struct.unpack_from('<H',raw,peo+20)[0]
    sh=opt+soh
    fa=struct.unpack_from('<I',raw,opt+36)[0]; sa=struct.unpack_from('<I',raw,opt+32)[0]
    dd=opt+96
    al=lambda x,a:(x+a-1)//a*a
    secs=[]
    for k in range(nsec):
        o=sh+40*k
        vs,va,rsz,rp=struct.unpack_from('<IIII',raw,o+8)
        secs.append(dict(hdr=o,vs=vs,va=va,rsz=rsz,rp=rp,ch=struct.unpack_from('<I',raw,o+36)[0]))
    rva_rsrc=struct.unpack_from('<I',raw,dd+16)[0]
    ri=[k for k,x in enumerate(secs) if x['va']==rva_rsrc]; assert len(ri)==1; ri=ri[0]
    rs=secs[ri]
    lastend=max(x['rp']+x['rsz'] for x in secs)
    overlay=bytes(raw[lastend:])
    newr=build_rsrc(entries, rs['va'])
    out=bytearray(raw[:rs['rp']])
    rawsize=al(len(newr),fa)
    out+=newr+b'\0'*(rawsize-len(newr))
    struct.pack_into('<I',out,rs['hdr']+8,len(newr)); struct.pack_into('<I',out,rs['hdr']+16,rawsize)
    vnext=al(rs['va']+len(newr),sa)
    for x in secs[ri+1:]:
        data=raw[x['rp']:x['rp']+x['rsz']]
        newraw=len(out); out+=data
        dv=vnext-x['va']
        struct.pack_into('<I',out,x['hdr']+12,vnext); struct.pack_into('<I',out,x['hdr']+20,newraw)
        span=max(x['vs'],x['rsz'])
        for k in range(16):
            v=struct.unpack_from('<I',out,dd+8*k)[0]
            if k!=4 and v and x['va']<=v<x['va']+span: struct.pack_into('<I',out,dd+8*k,v+dv)
        x['nva']=vnext; x['nrp']=newraw
        vnext=al(vnext+span,sa)
    ov_delta=len(out)-lastend
    out+=overlay
    struct.pack_into('<II',out,dd+16,rs['va'],len(newr))
    # SizeOfImage
    lastsec=secs[-1]; lva=lastsec.get('nva',lastsec['va']) if lastsec is not rs else rs['va']
    lspan=max(lastsec['vs'],lastsec['rsz']) if lastsec is not rs else len(newr)
    struct.pack_into('<I',out,opt+56,al(lva+lspan,sa))
    # SizeOfInitializedData
    tot=0
    for k in range(nsec):
        o=sh+40*k
        if struct.unpack_from('<I',out,o+36)[0]&0x40: tot+=struct.unpack_from('<I',out,o+16)[0]
    struct.pack_into('<I',out,opt+8,tot)
    # debug directory raw pointers into overlay
    drva,dsz=struct.unpack_from('<II',out,dd+48)
    if drva:
        for k in range(nsec):
            o=sh+40*k; vs,va,rsz,rp=struct.unpack_from('<IIII',out,o+8)
            if va<=drva<va+max(vs,rsz):
                fo=rp+drva-va
                for e in range(dsz//28):
                    prd=struct.unpack_from('<I',out,fo+28*e+24)[0]
                    if prd>=lastend: struct.pack_into('<I',out,fo+28*e+24,prd+ov_delta)
    struct.pack_into('<I',out,opt+64,0)
    struct.pack_into('<I',out,opt+64,pe_checksum(out,opt+64))
    open(path_out,'wb').write(out)
# ---------- string tables
def st_decode(d):
    out=[];p=0
    for i in range(16):
        n=struct.unpack_from('<H',d,p)[0]; p+=2
        out.append(d[p:p+2*n].decode('utf-16le')); p+=2*n
    return out
def st_encode(lst):
    b=b''
    for s in lst: b+=struct.pack('<H',len(s))+s.encode('utf-16le')
    return b
# ---------- dialogs (DLGTEMPLATE and DLGTEMPLATEEX)
def _sz_or_ord(d,p):
    w=struct.unpack_from('<H',d,p)[0]
    if w==0xffff: return struct.unpack_from('<H',d,p+2)[0],p+4
    if w==0: return '',p+2
    e=p
    while struct.unpack_from('<H',d,e)[0]!=0: e+=2
    return d[p:e].decode('utf-16le'),e+2
def _w_sz_or_ord(v):
    if isinstance(v,int): return struct.pack('<HH',0xffff,v)
    return v.encode('utf-16le')+b'\0\0'
def dlg_parse(d):
    p=0; dlg={}
    if struct.unpack_from('<HH',d,0)==(1,0xffff):
        dlg['ex']=True
        dlg['helpid'],dlg['exstyle'],dlg['style'],cnt,dlg['x'],dlg['y'],dlg['cx'],dlg['cy']=struct.unpack_from('<IIIHhhhh',d,4); p=26
    else:
        dlg['ex']=False
        dlg['style'],dlg['exstyle'],cnt,dlg['x'],dlg['y'],dlg['cx'],dlg['cy']=struct.unpack_from('<IIHhhhh',d,0); p=18
    dlg['menu'],p=_sz_or_ord(d,p); dlg['cls'],p=_sz_or_ord(d,p); dlg['title'],p=_sz_or_ord(d,p)
    if dlg['style']&0x40:  # DS_SETFONT
        if dlg['ex']:
            dlg['pt'],dlg['weight'],dlg['italic'],dlg['charset']=struct.unpack_from('<HHBB',d,p); p+=6
        else:
            dlg['pt']=struct.unpack_from('<H',d,p)[0]; p+=2
        dlg['font'],p=_sz_or_ord(d,p)
    items=[]
    for i in range(cnt):
        p=(p+3)&~3; it={}
        if dlg['ex']:
            it['helpid'],it['exstyle'],it['style'],it['x'],it['y'],it['cx'],it['cy'],it['id']=struct.unpack_from('<IIIhhhhI',d,p); p+=24
        else:
            it['style'],it['exstyle'],it['x'],it['y'],it['cx'],it['cy'],it['id']=struct.unpack_from('<IIhhhhH',d,p); p+=18
        it['cls'],p=_sz_or_ord(d,p); it['text'],p=_sz_or_ord(d,p)
        n=struct.unpack_from('<H',d,p)[0]; p+=2
        it['extra']=d[p:p+n]; p+=n
        items.append(it)
    dlg['items']=items
    return dlg
def dlg_build(dlg):
    b=bytearray(); items=dlg['items']
    if dlg['ex']:
        b+=struct.pack('<HHIIIHhhhh',1,0xffff,dlg['helpid'],dlg['exstyle'],dlg['style'],len(items),dlg['x'],dlg['y'],dlg['cx'],dlg['cy'])
    else:
        b+=struct.pack('<IIHhhhh',dlg['style'],dlg['exstyle'],len(items),dlg['x'],dlg['y'],dlg['cx'],dlg['cy'])
    for k in ('menu','cls','title'):
        v=dlg[k]; b+=(b'\0\0' if v=='' else _w_sz_or_ord(v))
    if dlg['style']&0x40:
        if dlg['ex']: b+=struct.pack('<HHBB',dlg['pt'],dlg['weight'],dlg['italic'],dlg['charset'])
        else: b+=struct.pack('<H',dlg['pt'])
        b+=_w_sz_or_ord(dlg['font'])
    for it in items:
        while len(b)%4: b+=b'\0'
        if dlg['ex']: b+=struct.pack('<IIIhhhhI',it['helpid'],it['exstyle'],it['style'],it['x'],it['y'],it['cx'],it['cy'],it['id'])
        else: b+=struct.pack('<IIhhhhH',it['style'],it['exstyle'],it['x'],it['y'],it['cx'],it['cy'],it['id'])
        b+=_w_sz_or_ord(it['cls']) if it['cls']!='' else b'\0\0'
        b+=(b'\0\0' if it['text']=='' else _w_sz_or_ord(it['text']))
        b+=struct.pack('<H',len(it['extra']))+it['extra']
    return bytes(b)
# ---------- menus (standard MENUHEADER only)
def menu_parse(d):
    ver,hs=struct.unpack_from('<HH',d,0); assert ver==0, 'MENUEX unsupported'
    p=4+hs; items=[]
    def rec(p,lst):
        while True:
            fl=struct.unpack_from('<H',d,p)[0]; p+=2
            it={'flags':fl}
            if not fl&0x10: it['id']=struct.unpack_from('<H',d,p)[0]; p+=2
            e=p
            while struct.unpack_from('<H',d,e)[0]: e+=2
            it['text']=d[p:e].decode('utf-16le'); p=e+2
            if fl&0x10:
                it['sub']=[]; p=rec(p,it['sub'])
            lst.append(it)
            if fl&0x80: return p
    rec(p,items); return items
def menu_build(items):
    b=bytearray(struct.pack('<HH',0,0))
    def rec(lst):
        for it in lst:
            b.extend(struct.pack('<H',it['flags']))
            if not it['flags']&0x10: b.extend(struct.pack('<H',it['id']))
            b.extend(it['text'].encode('utf-16le')+b'\0\0')
            if it['flags']&0x10: rec(it['sub'])
    rec(items); return bytes(b)
