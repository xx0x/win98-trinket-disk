#!/usr/bin/env python3
"""Tiny QMP client for driving the UTM Win98 VM.
usage: qmp.py shot OUT.png | key k1 [k2..] (combo with '-': ctrl-alt-delete) | type "text" | raw JSON | hmp "cmd"
"""
import socket, json, sys, time, os, subprocess
HOST,PORT='127.0.0.1',int(os.environ.get('QMP_PORT','4444'))
SHOTDIR=os.path.expanduser('~/Library/Containers/com.utmapp.QEMUHelper/Data/tmp')
class Q:
    def __init__(s):
        s.s=socket.create_connection((HOST,PORT),timeout=30); s.f=s.s.makefile('rwb')
        s.recv(); s.cmd('qmp_capabilities')
    def recv(s):
        while True:
            l=s.f.readline()
            if not l: raise EOFError
            m=json.loads(l)
            if 'event' in m: continue
            return m
    def cmd(s,name,**args):
        m={'execute':name}
        if args: m['arguments']=args
        s.f.write((json.dumps(m)+'\n').encode()); s.f.flush()
        r=s.recv()
        if 'error' in r: raise RuntimeError(r['error'])
        return r.get('return')
KEYMAP={' ':'spc','\n':'ret','\t':'tab','-':'minus','=':'equal','[':'bracket_left',']':'bracket_right',';':'semicolon',"'":'apostrophe','\\':'backslash',',':'comma','.':'dot','/':'slash','`':'grave_accent'}
SHIFTED={'!':'1','@':'2','#':'3','$':'4','%':'5','^':'6','&':'7','*':'8','(':'9',')':'0','_':'minus','+':'equal','{':'bracket_left','}':'bracket_right',':':'semicolon','"':'apostrophe','|':'backslash','<':'comma','>':'dot','?':'slash','~':'grave_accent'}
def keys_for(ch):
    if ch.isalpha() and ch.isascii():
        return (['shift'] if ch.isupper() else [])+[ch.lower()]
    if ch.isdigit(): return [ch]
    if ch in KEYMAP: return [KEYMAP[ch]]
    if ch in SHIFTED: return ['shift',SHIFTED[ch]]
    raise ValueError(repr(ch))
CZ={':':['shift','dot'],'\\':['alt_r','q'],'.':['dot'],',':['comma'],'_':['shift','slash'],'-':['slash'],'/':['shift','bracket_left'],' ':['spc'],'\n':['ret'],'"':['shift','semicolon'],'!':['shift','apostrophe'],'?':['shift','comma'],'(':['shift','bracket_right'],')':['bracket_right'],'%':['shift','equal'],'=':['minus'],'+':['1'],'*':['shift','8']}
def cz_keys(ch):
    if ch in CZ: return CZ[ch]
    if ch.isdigit(): return ['shift',ch]
    if ch.isalpha() and ch.isascii():
        k={'y':'z','z':'y'}.get(ch.lower(),ch.lower())
        return (['shift'] if ch.isupper() else [])+[k]
    raise ValueError(repr(ch))
def send(q,keys,hold=80):
    q.cmd('send-key',keys=[{'type':'qcode','data':k} for k in keys],**{'hold-time':hold})
if __name__=='__main__':
    q=Q(); a=sys.argv[1:]
    op=a[0]
    if op=='shot':
        out=a[1]; tmp=os.path.join(SHOTDIR,'shot.ppm')
        q.cmd('screendump',filename=tmp)
        subprocess.run(['sips','-s','format','png',tmp,'--out',out],capture_output=True)
        print(out)
    elif op=='key':
        for k in a[1:]:
            send(q,k.split('-')); time.sleep(0.15)
    elif op=='type':
        # US keyboard layout in the guest (op 'cz' types with the Czech layout)
        for ch in a[1]:
            send(q,keys_for(ch)); time.sleep(0.05)
    elif op=='cz':
        for ch in a[1]:
            send(q,cz_keys(ch)); time.sleep(0.08)
    elif op in ('aclick','adclick','arclick','amove'):
        # absolute (USB tablet) - coordinates in pixels of a W x H screen (default 640x480)
        W,H=(int(a[3]),int(a[4])) if len(a)>4 else (640,480)
        x=int(int(a[1])*32767/(W-1)); y=int(int(a[2])*32767/(H-1))
        q.cmd('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':x}},{'type':'abs','data':{'axis':'y','value':y}}])
        time.sleep(0.25)
        btn={'aclick':'left','adclick':'left','arclick':'right'}.get(op)
        if btn:
            for i in range(2 if op=='adclick' else 1):
                q.cmd('input-send-event',events=[{'type':'btn','data':{'down':True,'button':btn}}]); time.sleep(0.07)
                q.cmd('input-send-event',events=[{'type':'btn','data':{'down':False,'button':btn}}]); time.sleep(0.1)
    elif op in ('click','dclick','rclick','move'):
        x,y=int(a[1]),int(a[2])
        def rel(dx,dy):
            q.cmd('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}])
            time.sleep(0.012)
        for i in range(60): rel(-60,-60)
        time.sleep(0.2)
        cx=cy=0
        while cx<x or cy<y:
            dx=min(2,x-cx); dy=min(2,y-cy); rel(dx,dy); cx+=dx; cy+=dy
        time.sleep(0.3)
        btn={'click':'left','dclick':'left','rclick':'right'}.get(op)
        if btn:
            n=2 if op=='dclick' else 1
            for i in range(n):
                q.cmd('input-send-event',events=[{'type':'btn','data':{'down':True,'button':btn}}]); time.sleep(0.06)
                q.cmd('input-send-event',events=[{'type':'btn','data':{'down':False,'button':btn}}]); time.sleep(0.08)
    elif op=='raw':
        m=json.loads(a[1]); print(json.dumps(q.cmd(m['execute'],**m.get('arguments',{})),indent=1))
    elif op=='hmp':
        print(q.cmd('human-monitor-command',**{'command-line':a[1]}))
