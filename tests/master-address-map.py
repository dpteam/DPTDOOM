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
