"""Native master relay: explicit opt-in, lease auth, pair-only transport, bounds."""
import importlib.util
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('wire',Path(__file__).with_name('master-loopback.py'))
wire=importlib.util.module_from_spec(spec);spec.loader.exec_module(wire)
exe=str(Path(sys.argv[1]).resolve(strict=True))
with tempfile.TemporaryDirectory(prefix='dptdoom-relay-') as temporary:
    root=Path(temporary)
    for enabled in (False,True):
        with socket.socket() as r:r.bind(('127.0.0.1',0));port=r.getsockname()[1]
        with (root/'stdout.log').open('wb') as log:
            command=[exe,'--port',str(port),'--log',str(root/'events.log')]
            if enabled:command+=['--relay']
            process=subprocess.Popen(command,cwd=root,stdout=log,stderr=log)
            peers=[]
            try:
                end=time.monotonic()+10
                while b'listening on' not in (root/'stdout.log').read_bytes():
                    assert process.poll() is None;assert time.monotonic()<end;time.sleep(.02)
                def connect(op,body):
                    s=socket.create_connection(('127.0.0.1',port),timeout=5);s.sendall(wire.packet(op,body));return s
                def query(op,body=b''):
                    with connect(op,body) as s:return wire.reply(s,op)
                def attached(s,op):
                    magic,version,operation,size,status,reserved=wire.HEADER.unpack(wire.exact(s,16))
                    assert (magic,version,operation,size,status,reserved)==(b'DPTM',1,op,0,0,0)
                status,lease=query(2,wire.info());assert status==0
                auth=lease[:40]
                assert query(5,auth[:16]+bytes(24))[0] in (2,4)
                if not enabled:
                    assert query(5,auth)[0]==4;continue
                assert query(6,auth[:16])[0]==3 # no host relay channel yet
                host=connect(5,auth);peers.append(host)
                end=time.monotonic()+3
                while True:
                    row=wire.listing(query(1)[1])[0]
                    if struct.unpack('!I',row[156:160])[0]==1:break
                    assert time.monotonic()<end;time.sleep(.05)
                assert query(5,auth)[0]==3 # duplicate host cannot replace it
                guest=connect(6,auth[:16]);peers.append(guest)
                attached(host,5);attached(guest,6)
                assert wire.listing(query(1)[1])==[]
                assert query(6,auth[:16])[0]==2 # consumed lease not reusable
                payload=bytes(range(256))*40
                # Larger than a relay buffer: backpressure and byte-preservation.
                host.sendall(payload);assert wire.exact(guest,len(payload))==payload
                guest.sendall(payload[::-1]);assert wire.exact(host,len(payload))==payload[::-1]
                # Half-close must deliver all queued bytes before EOF.
                guest.sendall(payload);guest.shutdown(socket.SHUT_WR)
                assert wire.exact(host,len(payload))==payload;assert host.recv(1)==b''
                host.shutdown(socket.SHUT_WR);assert guest.recv(1)==b''
                guest.close();peers.remove(guest);host.close();peers.remove(host)
                # A waiting channel is closed when its authenticated lease is removed.
                status,lease=query(2,wire.info());assert status==0
                pending=connect(5,lease[:40]);peers.append(pending);time.sleep(.05)
                assert query(4,lease[:40])[0]==0;assert pending.recv(1)==b''
                # A disconnected host must lose its relay-available marker.
                status,lease=query(2,wire.info());assert status==0
                channel=connect(5,lease[:40]);time.sleep(.05);channel.close();time.sleep(.05)
                assert not struct.unpack('!I',wire.listing(query(1)[1])[0][156:160])[0]
                assert query(4,lease[:40])[0]==0
                # Bounded concurrent sessions: at most four host channels/source,
                # even when the directory lease limit would otherwise be freed.
                time.sleep(.5) # this is capacity, not the request-rate test
                for number in range(4):
                    status,item=query(2,wire.info(2500+number));assert status==0
                    channel=connect(5,item[:40]);peers.append(channel);time.sleep(.02)
                assert query(2,wire.info(2600))[0]==4
                text=(root/'events.log').read_text()
                assert 'relay-paired' in text and 'Relay: slot=' in text,text
                assert auth[16:].hex() not in text
            finally:
                for s in peers:s.close()
                process.terminate();process.wait(10)
    print('Native relay: opt-in, authenticated host, single paired guest, consumed listing, bounded transport/backpressure and cleanup OK')
