"""Native master config + registration/list/auth over real TCP; simulated hairpin."""
import importlib.util
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('wire',Path(__file__).with_name('master-loopback.py'))
wire=importlib.util.module_from_spec(spec);spec.loader.exec_module(wire)
exe=str(Path(sys.argv[1]).resolve(strict=True))
with tempfile.TemporaryDirectory(prefix='dptdoom-hairpin-') as temporary:
    root=Path(temporary)
    config=root/'master.conf'
    for invalid in ('address-map=0.0.0.0/0=203.0.113.9', 'unknown=value',
                    'address-map=192.168.1.0/24=203.0.113.9\naddress-map=192.168.1.0/24=203.0.113.10'):
        config.write_text(invalid)
        result=subprocess.run([exe,'--config',str(config)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'invalid config line' in result.stderr,result
    for invalid in ('public-address-mode=dns\n',
                    'public-address-mode=api\npublic-api=http://example.org\nhairpin-subnet=127.0.0.1/32\n',
                    'public-address-mode=manual\npublic-ip=10.0.0.1\nhairpin-subnet=127.0.0.1/32\n'):
        config.write_text(invalid)
        result=subprocess.run([exe,'--config',str(config)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'invalid public address' in result.stderr,result
    # A bad bind address from the config must fail loudly instead of silently
    # falling back to loopback, which would look like "the server is up" while
    # no remote client can ever connect.
    config.write_text('bind=not-an-ip\n')
    result=subprocess.run([exe,'--config',str(config)],cwd=root,capture_output=True,timeout=10)
    assert result.returncode!=0 and b'must be numeric IPv4' in result.stdout+result.stderr,result
    # A config path that does not exist yet is created as a commented template,
    # so a first run gives something to edit instead of an error.
    fresh=root/'fresh.conf'
    assert not fresh.exists()
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
    with (root/'fresh.log').open('wb') as log:
        process=subprocess.Popen([exe,'--port',str(port),'--config',str(fresh)],
                                 cwd=root,stdout=log,stderr=log)
        try:
            deadline=time.monotonic()+10
            while b'listening on' not in (root/'fresh.log').read_bytes():
                assert process.poll() is None
                assert time.monotonic()<deadline
                time.sleep(.02)
        finally:
            process.terminate();process.wait(10)
    text=(root/'fresh.log').read_bytes()
    assert b'wrote a commented template' in text,text
    body=fresh.read_text()
    # The template must be inert: every directive commented out, otherwise the
    # master would silently start with settings the operator never chose.
    assert body.strip(), 'template is empty'
    directives=[l for l in body.splitlines() if l.strip() and not l.lstrip().startswith('#')]
    assert not directives, directives
    assert 'bind=0.0.0.0' in body and 'address-map=' in body, body
    # A mistyped directory must still fail rather than "succeed" with a master
    # that ignores an existing config elsewhere.
    missing=root/'nosuchdir'/'x.conf'
    result=subprocess.run([exe,'--config',str(missing)],cwd=root,capture_output=True,timeout=10)
    assert result.returncode!=0,result

    # Ban list: loaded from disk, applied to the TCP source address, and a
    # malformed rule fails startup instead of loading a partial list.
    def with_bans(name, body):
        bans=root/(name+'.txt')
        if body is not None:
            bans.write_text(body)
        conf=root/(name+'.conf')
        conf.write_text('banlist=%s\n'%bans)
        return conf

    def try_connect(conf, name):
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/(name+'.log')
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(conf),
                                      '--log',str(root/(name+'-events.log'))],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None, logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                rejected=False
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                        s.sendall(wire.packet(1,b''))
                        wire.reply(s,1)
                except OSError:
                    rejected=True
                return rejected, logpath.read_bytes()
            finally:
                process.terminate();process.wait(10)

    # No ban file: nothing banned.
    rejected,text=try_connect(with_bans('ban-none',None),'ban-none')
    assert not rejected,text
    # Comment-only file: nothing banned.
    rejected,text=try_connect(with_bans('ban-empty','# nothing\n\n'),'ban-empty')
    assert not rejected,text
    # A rule that does not match the client must not block it.
    rejected,text=try_connect(with_bans('ban-other','198.51.100.1\n'),'ban-other')
    assert not rejected,text
    # Banning loopback blocks a loopback client, and the reason is logged.
    rejected,text=try_connect(with_bans('ban-self','# block local\n127.0.0.1\n'),'ban-self')
    assert rejected,text
    assert b'banned-source' in text,text

    # Malformed rule: whole reload fails, no partial list is applied.
    bad=with_bans('ban-bad','127.0.0.1\nnot-an-ip\n')
    result=subprocess.run([exe,'--port','25177','--config',str(bad)],
                          cwd=root,capture_output=True,text=True,timeout=10)
    assert result.returncode!=0,result
    assert 'invalid ban rule' in result.stderr,result
    # bind= in the config persists the operator's choice; loopback stays the
    # default, and an explicit --bind must still win over the config.
    for expected,extra in (('0.0.0.0',[]),('127.0.0.1',['--bind','127.0.0.1'])):
        config.write_text('bind=0.0.0.0\n')
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/'bind.log'
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(config)]+extra,
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                text=logpath.read_bytes()
                assert ('listening on %s:'%expected).encode() in text,text
                # The loopback hint must appear only when remote clients really
                # cannot connect, otherwise it becomes noise operators ignore.
                assert (b'bound to loopback' in text)==(expected=='127.0.0.1'),text
            finally:
                process.terminate();process.wait(10)
    for mode in ('off','map','manual','dns-failure'):
        mapped=mode in ('map','manual')
        config.write_text({'off':'# inert\n','map':'address-map=127.0.0.1/32=203.0.113.9\n',
            'manual':'public-address-mode=manual\npublic-ip=203.0.113.9\nhairpin-subnet=127.0.0.1/32\n',
            'dns-failure':'public-address-mode=dns\npublic-host=localhost\nhairpin-subnet=127.0.0.1/32\n'}[mode])
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        with (root/'stdout.log').open('wb') as log:
            cmd=[exe,'--port',str(port),'--log',str(root/'events.log')]
            if mode!='off':cmd+=['--config',str(config)]
            process=subprocess.Popen(cmd,cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in (root/'stdout.log').read_bytes():
                    assert process.poll() is None
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                def request(op,body=b''):
                    with socket.create_connection(('127.0.0.1',port),timeout=4) as s:
                        s.sendall(wire.packet(op,body));return wire.reply(s,op)
                if mode=='dns-failure':
                    # Only the OS localhost resolver is used; no Internet in CI.
                    start=time.monotonic();assert request(1)[0]==0;assert time.monotonic()-start<1
                    assert request(2,wire.info())[0]==4
                    assert not wire.listing(request(1)[1])
                    continue
                status,lease=request(2,wire.info());assert status==0
                row=wire.listing(request(1)[1])[0]
                assert row[16:20]==socket.inet_aton('203.0.113.9' if mapped else '127.0.0.1')
                assert row[24:156]==wire.info()
                assert request(3,lease[:40])[0]==0
                assert request(4,lease[:40])[0]==0
                assert wire.listing(request(1)[1])==[]
                if mapped:
                    events=(root/'events.log').read_text()
                    assert 'mapped=yes' in events and 'advertised=203.0.113.9:2342' in events
                    assert lease[16:40].hex() not in events
            finally:
                process.terminate();process.wait(10)
    print('Native master: default unchanged, explicit config mapping, source-bound heartbeat/remove and diagnostics OK')
