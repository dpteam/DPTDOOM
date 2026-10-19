"""Misc-only DeHackEd engine integration, synthetic patches/lumps, own local IWAD."""
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

HEADER = b'Patch File for DeHackEd v3.0\nDoom version = 19\nPatch format = 6\n\nMisc 0\n'


def wad(path, patches):
    data=bytearray(b'PWAD'+struct.pack('<ii',len(patches),0));records=[]
    for patch in patches:
        records.append((len(data),len(patch),b'DEHACKED'));data.extend(patch)
    struct.pack_into('<i',data,8,len(data))
    for record in records:data.extend(struct.pack('<ii8s',*record))
    path.write_bytes(data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--iwad',type=Path,required=True)
    args=parser.parse_args();exe=args.exe.resolve(strict=True);iwad=args.iwad.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='dptdoom-deh-') as temporary:
        root=Path(temporary)
        first=root/'first.deh';second=root/'second.deh';same=root/'other-path.deh';bad=root/'bad.deh';overlay=root/'patches.wad'
        first.write_bytes(HEADER+b'Initial Health = 77\nInitial Bullets = 23\n')
        same.write_bytes(first.read_bytes());second.write_bytes(HEADER+b'Initial Health = 88\n')
        bad.write_bytes(HEADER+b'Initial Health = 42\nThing 1\nHit points = 4\n')
        wad(overlay,[HEADER+b'Initial Health = 90\n',HEADER+b'Initial Health = 91\n'])
        base=[str(exe),'-iwad',str(iwad),'-warp','1','-nomonsters','-headless-tics','5','-headless-session-report']

        def run(name,extra,ok=True):
            work=root/name;work.mkdir()
            result=subprocess.run(base+extra,cwd=work,capture_output=True,text=True,timeout=20)
            text=result.stdout+result.stderr
            assert 'AddressSanitizer' not in text and 'runtime error:' not in text,text
            assert (result.returncode==0)==ok,(result.returncode,text)
            return work,text

        _,text=run('plain',[]);assert 'health=100 ammo=50' in text,text
        _,text=run('external',['-deh',str(first)]);assert 'health=77 ammo=23' in text,text
        _,text=run('ordered',['-deh',str(first),str(second)]);assert 'health=88 ammo=23' in text,text
        _,text=run('lumps',['-file',str(overlay),'-dehlump','-deh',str(first)])
        assert 'health=91 ammo=23' in text and 'loaded lump patch 3' in text,text
        _,text=run('lumps-disabled',['-file',str(overlay)]);assert 'health=100 ammo=50' in text,text
        _,text=run('reject',['-deh',str(bad)],False);assert 'unsupported section' in text,text
        _,text=run('missing',['-deh'],False);assert 'requires at least one' in text,text
        for name,extra in [('save',['-headless-save-tic','2']),('load',['-loadgame','0']),('record',['-record','test'])]:
            work,text=run(name,['-deh',str(first)]+extra,False)
            assert ('vanilla save/load disabled' if name!='record' else 'recording disabled') in text,text
            assert not list(work.rglob('*.dsg')) and not list(work.glob('*.lmp'))

        def pair(name,patches,success):
            with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
            dirs=[root/(name+'-host'),root/(name+'-guest')]
            for d in dirs:d.mkdir()
            logs=[(d/'output.txt').open('w') for d in dirs];processes=[]
            cmd=[str(exe),'-iwad',str(iwad),'-warp','1','-nomonsters','-net-timeout','1500',
                 '-headless-tics','70','-headless-session-report','-headless-trace','trace.txt']
            try:
                processes.append(subprocess.Popen(cmd+['-net-host',str(port),'-deh',str(patches[0])],cwd=dirs[0],stdout=logs[0],stderr=logs[0]))
                end=time.monotonic()+15
                while 'listening on' not in (dirs[0]/'output.txt').read_text():
                    assert processes[0].poll() is None,(dirs[0]/'output.txt').read_text()
                    assert time.monotonic()<end;time.sleep(.02)
                processes.append(subprocess.Popen(cmd+['-net-join',f'127.0.0.1:{port}','-deh',str(patches[1])],cwd=dirs[1],stdout=logs[1],stderr=logs[1]))
                codes=[p.wait(20) for p in processes];texts=[(d/'output.txt').read_text() for d in dirs]
                assert all('AddressSanitizer' not in t for t in texts),texts
                if success:
                    assert codes==[0,0],texts
                    assert (dirs[0]/'trace.txt').read_bytes()==(dirs[1]/'trace.txt').read_bytes()
                    assert all('health=77 ammo=23' in t for t in texts),texts
                else:
                    assert all(code!=0 for code in codes),texts
                    assert all('resource content/order mismatch' in t for t in texts),texts
                    assert not any('connected as player' in t for t in texts),texts
            finally:
                for p in processes:
                    if p.poll() is None:p.kill();p.wait()
                for log in logs:log.close()
        pair('same',[first,same],True);pair('different',[first,second],False)
        print('DeHackEd engine: actual health/ammo, external/lump opt-in order, strict rejection, save/record guards, matching/mismatched network digest OK')


if __name__=='__main__':main()
