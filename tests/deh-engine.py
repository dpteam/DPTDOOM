"""Bounded DeHackEd engine integration, synthetic patches, own local IWAD."""
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import re

HEADER = b'Patch File for DeHackEd v3.0\nDoom version = 19\nPatch format = 6\n\nMisc 0\n'


def wad(path, patches):
    data=bytearray(b'PWAD'+struct.pack('<ii',len(patches),0));records=[]
    for patch in patches:
        records.append((len(data),len(patch),b'DEHACKED'));data.extend(patch)
    struct.pack_into('<i',data,8,len(data))
    for record in records:data.extend(struct.pack('<ii8s',*record))
    path.write_bytes(data)


def thing_map(iwad, path, doomednum=2035):
    # Extract only to the test's temporary directory. Preserve map geometry,
    # replace THINGS by two starts + one barrel at a known map thing position.
    source=iwad.read_bytes();_,count,directory=struct.unpack_from('<4sii',source)
    entries=[]
    for i in range(count):
        offset,size,name=struct.unpack_from('<ii8s',source,directory+i*16)
        entries.append((name.rstrip(b'\0'),source[offset:offset+size]))
    index=next(i for i,(name,_) in enumerate(entries) if name==b'MAP01')
    level=entries[index:index+11]
    things=list(struct.iter_unpack('<hhhhh',dict(level)[b'THINGS']))
    starts=[t for t in things if t[3] in (1,2)]
    assert len(starts)==2
    position=max(things,key=lambda t:(t[0]-starts[0][0])**2+(t[1]-starts[0][1])**2)
    custom=starts+[(position[0],position[1],position[2],doomednum,7)]
    level=[(name,b''.join(struct.pack('<hhhhh',*t) for t in custom) if name==b'THINGS' else data) for name,data in level]
    body=bytearray(b'PWAD'+struct.pack('<ii',len(level),0));records=[]
    for name,data in level:
        records.append((len(body),len(data),name.ljust(8,b'\0')));body.extend(data)
    struct.pack_into('<i',body,8,len(body))
    for record in records:body.extend(struct.pack('<ii8s',*record))
    path.write_bytes(body)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--iwad',type=Path,required=True)
    args=parser.parse_args();exe=args.exe.resolve(strict=True);iwad=args.iwad.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='dptdoom-deh-') as temporary:
        root=Path(temporary)
        first=root/'first.deh';second=root/'second.deh';same=root/'other-path.deh';bad=root/'bad.deh';overlay=root/'patches.wad'
        first.write_bytes(HEADER+b'Initial Health = 77\nInitial Bullets = 23\n')
        same.write_bytes(first.read_bytes());second.write_bytes(HEADER+b'Initial Health = 88\n')
        bad.write_bytes(HEADER+b'Initial Health = 42\nFrame 13\nUnknown 3 = 1\n')
        wad(overlay,[HEADER+b'Initial Health = 90\n',HEADER+b'Initial Health = 91\n'])
        base=[str(exe),'-iwad',str(iwad),'-warp','1','-nomonsters','-headless-session-report']

        def run_in(work,extra,ok=True,tics=5):
            work.mkdir(exist_ok=True)
            result=subprocess.run(base+['-headless-tics',str(tics)]+extra,cwd=work,capture_output=True,text=True,timeout=20)
            text=result.stdout+result.stderr
            assert 'AddressSanitizer' not in text and 'runtime error:' not in text,text
            assert (result.returncode==0)==ok,(result.returncode,text)
            return text

        def run(name,extra,ok=True,tics=5):
            return root/name,run_in(root/name,extra,ok,tics)

        _,text=run('plain',[]);assert 'health=100 ammo=50' in text,text
        _,text=run('external',['-deh',str(first)]);assert 'health=77 ammo=23' in text,text
        _,text=run('ordered',['-deh',str(first),str(second)]);assert 'health=88 ammo=23' in text,text
        _,text=run('lumps',['-file',str(overlay),'-dehlump','-deh',str(first)])
        assert 'health=91 ammo=23' in text and 'loaded lump patch 3' in text,text
        _,text=run('lumps-disabled',['-file',str(overlay)]);assert 'health=100 ammo=50' in text,text
        ammo=root/'ammo.deh';ammo.write_bytes(HEADER+b'Initial Bullets = 50\nAmmo 0\nMax ammo = 450\nPer ammo = 17\nAmmo 1\nMax ammo = 60\nWeapon 1\nAmmo type = 1\n')
        _,text=run('ammo-weapon',['-deh',str(ammo)])
        assert 'Patch report: maxammo=450,60,300,50' in text and 'perammo=17,4,20,1 pistol-ammo=1' in text,text
        infight=root/'infight.deh';infight_copy=root/'infight-copy.deh'
        infight.write_bytes(HEADER+b'Monsters Infight = 1\n')
        infight_copy.write_bytes(infight.read_bytes())
        _,text=run('infight',['-deh',str(infight)])
        assert 'Patch misc: infight=1' in text,text
        _,text=run('infight-off',[]);assert 'Patch misc' not in text,text
        text_patch=root/'text.deh';text_copy=root/'text-copy.deh'
        old=b'Picked up the armor.';replacement=b'DPT armor message!'
        text_patch.write_bytes(HEADER+b'Text '+str(len(old)).encode()+b' '+str(len(replacement)).encode()+b'\n'+old+replacement)
        text_copy.write_bytes(text_patch.read_bytes())
        _,text=run('text',['-deh',str(text_patch)])
        assert 'Patch text: armor=DPT armor message! saved=game saved.' in text,text
        text_lump=root/'text-lump.wad';wad(text_lump,[text_patch.read_bytes()])
        _,text=run('text-lump',['-file',str(text_lump),'-dehlump'])
        assert 'Patch text: armor=DPT armor message! saved=game saved.' in text,text
        bex=root/'bex-strings.deh';bex_copy=root/'bex-strings-copy.deh'
        bex.write_bytes(HEADER+b'[STRINGS]\nMSGON = BEX messages on\nDETAILHI = BEX high detail\nGOTARMOR = BEX armor pickup\n')
        bex_copy.write_bytes(bex.read_bytes())
        _,text=run('bex-strings',['-deh',str(bex)])
        assert 'armor=BEX armor pickup saved=game saved. msgon=BEX messages on' in text,text
        bex_lump=root/'bex-strings.wad';wad(bex_lump,[bex.read_bytes()])
        _,text=run('bex-strings-lump',['-file',str(bex_lump),'-dehlump'])
        assert 'msgon=BEX messages on' in text,text
        bex_bad=root/'bex-bad.deh';bex_bad.write_bytes(HEADER+b'[STRINGS]\nNOSUCHSTRING = value\n')
        _,text=run('bex-unknown',['-deh',str(bex_bad)],False)
        assert 'unknown [STRINGS] name' in text,text
        bex_pars=root/'bex-pars.deh';bex_pars_copy=root/'bex-pars-copy.deh'
        bex_pars.write_bytes(HEADER+b'[PARS]\npar 1 45\npar 32 20\n')
        bex_pars_copy.write_bytes(bex_pars.read_bytes())
        _,text=run('bex-pars',['-deh',str(bex_pars)])
        assert 'par1=45 par32=20' in text,text
        bex_pars_lump=root/'bex-pars.wad';wad(bex_pars_lump,[bex_pars.read_bytes()])
        _,text=run('bex-pars-lump',['-file',str(bex_pars_lump),'-dehlump'])
        assert 'par1=45 par32=20' in text,text
        bex_pars_bad=root/'bex-pars-bad.deh'
        bex_pars_bad.write_bytes(HEADER+b'[PARS]\npar 1 1 30\n')
        _,text=run('bex-pars-wrong-form',['-deh',str(bex_pars_bad)],False)
        assert 'E?M? form requires a non-commercial IWAD' in text,text
        assert 'par1=45' not in text,text
        # [CODEPTR]: allowlisted mobj action on a mobj state is visible in the
        # spawned barrel report; the plain run has no action there.
        codeptr_map=root/'codeptr-map.wad';thing_map(iwad,codeptr_map)
        codeptr=root/'codeptr.deh';codeptr_copy=root/'codeptr-copy.deh'
        codeptr.write_bytes(HEADER+b'[CODEPTR]\nFrame 806 = A_FaceTarget\nFrame 807 = A_FaceTarget\n')
        codeptr_copy.write_bytes(codeptr.read_bytes())
        _,plain_cp=run('codeptr-plain',['-file',str(codeptr_map),'-deh',str(first)])
        assert 'action=none' in plain_cp,plain_cp
        _,text=run('codeptr-mobj',['-file',str(codeptr_map),'-deh',str(codeptr)])
        assert 'action=A_FaceTarget' in text,text
        codeptr_bare=root/'codeptr-bare.deh'
        codeptr_bare.write_bytes(HEADER+b'[CODEPTR]\nFrame 806 = FaceTarget\nFrame 807 = FaceTarget\n')
        _,text=run('codeptr-bare',['-file',str(codeptr_map),'-deh',str(codeptr_bare)])
        assert 'action=A_FaceTarget' in text,text
        # Weapon states accept psprite actions and reject mobj actions.
        codeptr_weapon=root/'codeptr-weapon.deh'
        codeptr_weapon.write_bytes(HEADER+b'[CODEPTR]\nFrame 10 = A_WeaponReady\n')
        run('codeptr-weapon',['-deh',str(codeptr_weapon)])
        for name,patch,needle in [
            ('codeptr-class',b'[CODEPTR]\nFrame 10 = A_Chase\n','action class does not match'),
            ('codeptr-class2',b'[CODEPTR]\nFrame 806 = A_WeaponReady\n','action class does not match'),
            ('codeptr-unknown',b'[CODEPTR]\nFrame 806 = NoSuchAction\n','not in the allowlist'),
            ('codeptr-range',b'[CODEPTR]\nFrame 999999 = A_Chase\n','frame out of range'),
            ('codeptr-null',b'[CODEPTR]\nFrame 0 = A_Chase\n','frame out of range')]:
            rejected=root/(name+'.deh');rejected.write_bytes(HEADER+patch)
            _,text=run(name,['-file',str(codeptr_map),'-deh',str(rejected)],False)
            assert needle in text,text
            assert 'Session report:' not in text,text
        codeptr_lump=root/'codeptr.wad';wad(codeptr_lump,[codeptr.read_bytes()])
        _,text=run('codeptr-lump',['-file',str(codeptr_map),str(codeptr_lump),'-dehlump'])
        assert 'action=A_FaceTarget' in text,text
        # A self-referential AI action is a mod hazard: A_Chase restarts the
        # spawn state when it has no target, so the engine must fail with a
        # bounded message instead of overflowing the stack.
        recursive=root/'codeptr-recursive.deh'
        recursive.write_bytes(HEADER+b'[CODEPTR]\nFrame 806 = A_Chase\nFrame 807 = A_Chase\n')
        _,text=run('codeptr-recursive',['-file',str(codeptr_map),'-deh',str(recursive)],False,tics=30)
        assert 'state chain recursion limit exceeded' in text,text
        things=root/'thing.deh';thing_copy=root/'thing-copy.deh';map_wad=root/'thing-map.wad';patched_map=root/'thing-patched-map.wad'
        thing_map(iwad,map_wad)
        thing_map(iwad,patched_map,4000)
        things.write_bytes(HEADER+b'Thing 31 (Barrel)\nID # = 4000\nBits = 2\nInitial frame = 806\nAlert sound = 1\nAttack sound = 2\nPain sound = 3\nDeath sound = 16\nAction sound = 8\nHit points = 123\nSpeed = 1\nWidth = 1572864\nHeight = 3932160\nMass = 250\nMissile damage = 7\nPain chance = 100\nReaction time = 20\n')
        # Both barrel animation states use existing TROOA frames, fullbright.
        things.write_bytes(things.read_bytes()+b'Frame 806\nSprite number = 0\nSprite subnumber = 32768\nFrame 807\nSprite number = 0\nSprite subnumber = 32768\n')
        thing_copy.write_bytes(things.read_bytes())
        _,plain_thing=run('plain-thing',['-file',str(map_wad),'-deh',str(first)])
        assert 'Patch thing: type=30 id=2035 flags=524294 state=807 death=808 sounds=0,0,0,82,0 health=20 radius=655360 height=2752512' in plain_thing,plain_thing
        _,patched_thing=run('patched-thing',['-file',str(patched_map),'-deh',str(things)])
        assert 'Patch thing: type=30 id=4000 flags=2 state=807 death=808 sounds=1,2,3,16,8 health=123 radius=1572864 height=3932160 speed=1 mass=250 damage=7 pain=100 reaction=20' in patched_thing,patched_thing
        assert 'Patch visual: type=30 sprite=0 frame=32768' in patched_thing,patched_thing
        embedded=root/'thing-lump.wad';wad(embedded,[things.read_bytes()])
        _,embedded_thing=run('thing-lump',['-file',str(patched_map),str(embedded),'-dehlump'])
        assert 'type=30 id=4000 flags=2 state=807 death=808 sounds=1,2,3,16,8 health=123 radius=1572864 height=3932160' in embedded_thing,embedded_thing
        assert 'Patch visual: type=30 sprite=0 frame=32768' in embedded_thing,embedded_thing
        missing_frame=root/'missing-frame.deh'
        missing_frame.write_bytes(HEADER+b'Frame 806\nSprite number = 0\nSprite subnumber = 28\n')
        _,text=run('missing-frame',['-file',str(map_wad),'-deh',str(missing_frame)],False)
        assert 'DeHackEd sprite validation: Frame 806:' in text and 'absent from loaded sprite resources' in text,text
        assert 'Session report:' not in text,text
        missing_lump=root/'missing-frame.wad';wad(missing_lump,[missing_frame.read_bytes()])
        _,text=run('missing-frame-lump',['-file',str(map_wad),str(missing_lump),'-dehlump'],False)
        assert 'DeHackEd sprite validation: Frame 806:' in text,text
        visual=root/'visual.deh';visual_copy=root/'visual-copy.deh'
        # Ready and raising pistol states draw an existing shotgun frame B.
        visual.write_bytes(HEADER+b'Frame 10\nSprite number = 1\nSprite subnumber = 32769\nFrame 12\nSprite number = 1\nSprite subnumber = 32769\n')
        visual_copy.write_bytes(visual.read_bytes())
        plain_work,_=run('visual-plain',['-headless-dump','frame.ppm'],tics=40)
        visual_work,text=run('visual-patched',['-deh',str(visual),'-headless-dump','frame.ppm'],tics=40)
        assert 'Patch weapon visual: sprite=1 frame=32769' in text,text
        visual_repeat,_=run('visual-repeat',['-deh',str(visual),'-headless-dump','frame.ppm'],tics=40)
        visual_lump=root/'visual-lump.wad';wad(visual_lump,[visual.read_bytes()])
        visual_embedded,_=run('visual-lump',['-file',str(visual_lump),'-dehlump','-headless-dump','frame.ppm'],tics=40)
        assert plain_work.joinpath('frame.ppm').read_bytes()!=visual_work.joinpath('frame.ppm').read_bytes()
        assert visual_work.joinpath('frame.ppm').read_bytes()==visual_repeat.joinpath('frame.ppm').read_bytes()
        assert visual_work.joinpath('frame.ppm').read_bytes()==visual_embedded.joinpath('frame.ppm').read_bytes()
        # The final combination is checked, not an intermediate assignment.
        repaired=root/'repaired-frame.deh'
        repaired.write_bytes(missing_frame.read_bytes()+b'Frame 806\nSprite subnumber = 0\n')
        run('repaired-frame',['-file',str(map_wad),'-deh',str(repaired)])
        # Psprite offsets: the raising pistol starts at 64/64 map units instead
        # of the vanilla bottom-of-screen position.
        offset=root/'psprite-offset.deh';offset_copy=root/'psprite-offset-copy.deh'
        offset.write_bytes(HEADER+b'Frame 12\nUnknown 1 = 64\nUnknown 2 = 64\n')
        offset_copy.write_bytes(offset.read_bytes())
        offset_plain_work,offset_plain_text=run('offset-plain',['-headless-dump','frame.ppm'])
        offset_work,text=run('offset-patched',['-deh',str(offset),'-headless-dump','frame.ppm'])
        assert 'sx=4194304' in text and 'sy=3801088' in text,text
        assert 'Patch weapon visual' not in offset_plain_text,offset_plain_text
        assert offset_plain_work.joinpath('frame.ppm').read_bytes()!=offset_work.joinpath('frame.ppm').read_bytes()
        offset_repeat,_=run('offset-repeat',['-deh',str(offset),'-headless-dump','frame.ppm'])
        offset_lump=root/'offset-lump.wad';wad(offset_lump,[offset.read_bytes()])
        offset_embedded,_=run('offset-lump',['-file',str(offset_lump),'-dehlump','-headless-dump','frame.ppm'])
        assert offset_work.joinpath('frame.ppm').read_bytes()==offset_repeat.joinpath('frame.ppm').read_bytes()
        assert offset_work.joinpath('frame.ppm').read_bytes()==offset_embedded.joinpath('frame.ppm').read_bytes()
        for name,patch in [('offset-mobj',HEADER+b'Frame 806\nUnknown 1 = 4\n'),
                           ('offset-range',HEADER+b'Frame 12\nUnknown 1 = 129\n'),
                           ('offset-neg',HEADER+b'Frame 12\nUnknown 2 = -129\n')]:
            rejected=root/(name+'.deh');rejected.write_bytes(patch)
            _,text=run(name,['-deh',str(rejected)],False)
            assert 'Unknown 1/2' in text and 'Session report:' not in text,text
        # Identical attack commands: standard pistol burns bullets; patched
        # pistol uses empty shell reserve and must not burn the 50 bullets.
        inputs=root/'attack.bin';inputs.write_bytes(b''.join(struct.pack('!bbhBBBB',0,0,0,1 if tic%30>=15 else 0,0,0,0) for tic in range(200)))
        command=[str(exe),'-iwad',str(iwad),'-warp','1','-nomonsters','-headless-tics','140',
                 '-headless-session-report','-headless-input',str(inputs)]
        duration=root/'duration.deh';duration_copy=root/'duration-copy.deh'
        duration.write_bytes(HEADER+b'Frame 13\nDuration = 60\nFrame 14\nDuration = 60\nFrame 15\nDuration = 60\nFrame 16\nDuration = 60\n')
        duration_copy.write_bytes(duration.read_bytes())
        transition=root/'transition.deh';transition_copy=root/'transition-copy.deh'
        transition.write_bytes(HEADER+b'Frame 13\nNext frame = 15\n')
        transition_copy.write_bytes(transition.read_bytes())
        weapon_frames=root/'weapon-frames.deh';weapon_frames_copy=root/'weapon-frames-copy.deh'
        weapon_frames.write_bytes(HEADER+b'Weapon 1\nShooting frame = 15\n')
        weapon_frames_copy.write_bytes(weapon_frames.read_bytes())
        fire_ammo={}
        for name,extra in [('standard-fire',[]),('patched-fire',['-deh',str(ammo)]),('slow-fire',['-deh',str(duration)])]:
            work=root/name;work.mkdir()
            result=subprocess.run(command+extra,cwd=work,capture_output=True,text=True,timeout=20)
            assert result.returncode==0 and 'AddressSanitizer' not in result.stderr,(result.stdout,result.stderr)
            match=re.search(r'Session report: .* health=100 ammo=(\d+)',result.stdout)
            assert match,result.stdout
            fire_ammo[name]=int(match[1])
            if name=='patched-fire':assert fire_ammo[name]==50,result.stdout
            elif name=='standard-fire':assert fire_ammo[name]<50,result.stdout
        assert fire_ammo['standard-fire']<fire_ammo['slow-fire']<50,fire_ammo
        # Embedded Duration patch takes the identical runtime path.
        duration_wad=root/'duration.wad';wad(duration_wad,[duration.read_bytes()])
        work=root/'slow-fire-lump';work.mkdir()
        result=subprocess.run(command+['-file',str(duration_wad),'-dehlump'],cwd=work,capture_output=True,text=True,timeout=20)
        assert result.returncode==0 and 'AddressSanitizer' not in result.stderr,(result.stdout,result.stderr)
        match=re.search(r'Session report: .* health=100 ammo=(\d+)',result.stdout)
        assert match and int(match[1])==fire_ammo['slow-fire'],result.stdout
        _,text=run('reject',['-deh',str(bad)],False);assert 'unsupported Frame field' in text,text
        _,text=run('missing',['-deh'],False);assert 'requires at least one' in text,text
        # Patched saves carry a bounded manifest: same patch set loads, any
        # different set (including no patches) is rejected before simulation.
        save_dir=root/'patched-save'
        text=run_in(save_dir,['-deh',str(first),'-headless-save-tic','2'])
        saves=list(save_dir.rglob('*.dsg'))
        assert len(saves)==1 and saves[0].stat().st_size>0,text
        assert saves[0].read_bytes()[24:31]==b'DPTDEH1',saves[0].read_bytes()[:32]
        text=run_in(save_dir,['-deh',str(first),'-loadgame','0'])
        assert 'health=77 ammo=23' in text,text
        text=run_in(save_dir,['-deh',str(second),'-loadgame','0'],False)
        assert 'different DeHackEd patches' in text,text
        assert 'Session report:' not in text,text
        text=run_in(save_dir,['-loadgame','0'],False)
        assert 'different DeHackEd patches' in text,text
        # Vanilla save stays byte-compatible and is rejected under active patches.
        vanilla_dir=root/'vanilla-save'
        text=run_in(vanilla_dir,['-headless-save-tic','2'])
        vanilla_save=list(vanilla_dir.rglob('*.dsg'))[0]
        assert vanilla_save.read_bytes()[24:31]==b'version',vanilla_save.read_bytes()[:32]
        text=run_in(vanilla_dir,['-loadgame','0'])
        assert 'health=100 ammo=50' in text,text
        text=run_in(vanilla_dir,['-deh',str(first),'-loadgame','0'],False)
        assert 'Vanilla savegame cannot be loaded' in text,text
        # Demo recording now carries a manifest; vanilla demos stay untouched.
        record_dir=root/'record'
        text=run_in(record_dir,['-deh',str(first),'-record','test'],False,tics=30)
        assert 'recorded' in text,text
        demo_files=list(record_dir.glob('*.lmp'))
        assert len(demo_files)==1,demo_files
        demo_bytes=demo_files[0].read_bytes()
        assert demo_bytes[0]==120 and demo_bytes[1:8]==b'DPTDEH1',demo_bytes[:12]
        assert demo_bytes[-1]==0x80,demo_bytes[-4:]
        # Same patch set replays; a different set or none is rejected.
        text=run_in(record_dir,['-deh',str(first),'-playdemo','test.lmp'])
        assert 'Playing demo' in text,text
        text=run_in(record_dir,['-deh',str(second),'-playdemo','test.lmp'],False)
        assert 'different DeHackEd patches' in text,text
        text=run_in(record_dir,['-playdemo','test.lmp'],False)
        assert 'recorded with DeHackEd patches' in text,text
        # Vanilla demo bytes keep version 109 and stay usable without patches.
        vanilla_demo=record_dir/'vanilla.lmp'
        text=run_in(record_dir,['-record','vanilla'],False,tics=30)
        assert vanilla_demo.is_file(),text
        vanilla_bytes=vanilla_demo.read_bytes()
        assert vanilla_bytes[0]==109,vanilla_bytes[:4]
        text=run_in(record_dir,['-deh',str(first),'-playdemo','vanilla.lmp'],False)
        assert 'Vanilla demo cannot be played' in text,text

        def pair(name,patches,success,expected='health=77 ammo=23',extra=()):
            with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
            dirs=[root/(name+'-host'),root/(name+'-guest')]
            for d in dirs:d.mkdir()
            logs=[(d/'output.txt').open('w') for d in dirs];processes=[]
            cmd=[str(exe),'-iwad',str(iwad),'-warp','1','-nomonsters','-net-timeout','1500',
                 '-headless-tics','70','-headless-session-report','-headless-trace','trace.txt']
            cmd+=list(extra)
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
                    assert all(expected in t for t in texts),texts
                else:
                    assert all(code!=0 for code in codes),texts
                    assert all('resource content/order mismatch' in t for t in texts),texts
                    assert not any('connected as player' in t for t in texts),texts
            finally:
                for p in processes:
                    if p.poll() is None:p.kill();p.wait()
                for log in logs:log.close()
        pair('same',[first,same],True);pair('different',[first,second],False)
        ammo_same=root/'ammo-copy.deh';ammo_same.write_bytes(ammo.read_bytes())
        pair('ammo-same',[ammo,ammo_same],True,'health=100 ammo=50')
        pair('ammo-different',[ammo,first],False)
        pair('text-same',[text_patch,text_copy],True,'health=100 ammo=50')
        text_different=root/'text-different.deh'
        text_different.write_bytes(HEADER+b'Text '+str(len(old)).encode()+b' '+str(len(b'Other message!')).encode()+b'\n'+old+b'Other message!')
        pair('text-different',[text_patch,text_different],False)
        pair('bex-strings-same',[bex,bex_copy],True,'msgon=BEX messages on')
        pair('bex-strings-different',[bex,first],False)
        pair('bex-pars-same',[bex_pars,bex_pars_copy],True,'par1=45 par32=20')
        pair('bex-pars-different',[bex_pars,first],False)
        pair('codeptr-same',[codeptr,codeptr_copy],True,'action=A_FaceTarget',('-file',str(codeptr_map)))
        pair('codeptr-different',[codeptr,first],False,extra=('-file',str(codeptr_map)))
        pair('infight-same',[infight,infight_copy],True,'infight=1')
        pair('infight-different',[infight,first],False)
        pair('thing-same',[things,thing_copy],True,'Patch visual: type=30 sprite=0 frame=32768',('-file',str(patched_map)))
        pair('thing-different',[things,first],False,extra=('-file',str(patched_map)))
        pair('visual-same',[visual,visual_copy],True,'Patch weapon visual: sprite=1 frame=32769')
        pair('visual-different',[visual,first],False)
        pair('offset-same',[offset,offset_copy],True,'sx=4194304')
        pair('offset-different',[offset,first],False)
        pair('duration-same',[duration,duration_copy],True,'health=100',('-headless-input',str(inputs)))
        pair('duration-different',[duration,first],False)
        # Repeating -fast initialization must not mutate the patched pistol
        # durations; the fast-transformed SARG range itself is not patchable.
        pair('duration-fast',[duration,duration_copy],True,'health=100',('-fast','-headless-input',str(inputs)))
        pair('transition-same',[transition,transition_copy],True,'health=100',('-headless-input',str(inputs)))
        pair('transition-different',[transition,first],False)
        # Skipping S_PISTOL2 skips A_FirePistol, so the same attack stream
        # leaves the clip unchanged. This verifies the real state graph path.
        _,transition_text=run('transition-engine',['-deh',str(transition),'-headless-input',str(inputs)],tics=140)
        match=re.search(r'Session report: .* health=100 ammo=(\d+)',transition_text)
        assert match and int(match[1])==50,transition_text
        _,weapon_text=run('weapon-frame-engine',['-deh',str(weapon_frames),'-headless-input',str(inputs)],tics=140)
        match=re.search(r'Session report: .* health=100 ammo=(\d+)',weapon_text)
        assert match and int(match[1])==50,weapon_text
        pair('weapon-frame-same',[weapon_frames,weapon_frames_copy],True,'health=100',('-headless-input',str(inputs)))
        pair('weapon-frame-different',[weapon_frames,first],False)
        print('DeHackEd engine: actual health/ammo/Thing, Frame visuals/psprite offsets and resource rejection, file/lump pixels, firing cadence, guards and network traces OK')


if __name__=='__main__':main()
