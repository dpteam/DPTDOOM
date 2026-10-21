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
                    # Exactly 1, not merely "bit 0 set": with --relay-port off the
                    # word must be byte-identical to the pre-feature format, so no
                    # port bits may leak in.
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

    # Dedicated relay port: the two roles must not mix, and the relay endpoint
    # must actually carry traffic.
    with socket.socket() as r:r.bind(('127.0.0.1',0));control_port=r.getsockname()[1]
    with socket.socket() as r:r.bind(('127.0.0.1',0));relay_port=r.getsockname()[1]
    with (root/'split.log').open('wb') as log:
        process=subprocess.Popen([exe,'--port',str(control_port),'--relay',
                                  '--relay-port',str(relay_port),
                                  '--log',str(root/'split-events.log')],
                                 cwd=root,stdout=log,stderr=log)
        peers=[]
        try:
            end=time.monotonic()+10
            while b'relay listening on' not in (root/'split.log').read_bytes():
                assert process.poll() is None,(root/'split.log').read_bytes()
                assert time.monotonic()<end;time.sleep(.02)

            def cquery(op,body=b''):
                with socket.create_connection(('127.0.0.1',control_port),timeout=5) as s:
                    s.sendall(wire.packet(op,body));return wire.reply(s,op)
            def rconnect(op,body):
                s=socket.create_connection(('127.0.0.1',relay_port),timeout=5)
                s.sendall(wire.packet(op,body));return s

            # Control operations still work on the control port.
            status,lease=cquery(2,wire.info());assert status==0
            auth=lease[:40]
            assert cquery(1)[0]==0

            # A relay operation on the control port is redirected: the client
            # cannot know the split port and the host attaches before it has any
            # row of its own, so the answer carries the port instead of a
            # refusal. Status 5 plus a 2-byte body, not an empty 400-style reply.
            status,body=cquery(5,auth)
            assert status==5,(status,body)
            assert body==struct.pack('!H',relay_port),(body,relay_port)
            # A control operation on the relay port is refused: there is no
            # alternative port to send it to, so this stays a plain rejection.
            assert wire.reply(rconnect(1,b''),1)[0]==1
            # The relay itself works on its own port.
            host=rconnect(5,auth);peers.append(host)
            # The listing must stay byte-identical to the pre-feature format:
            # only bit0 may appear in the flags word, never a packed port.
            end=time.monotonic()+3
            while True:
                status,body=cquery(1)
                assert status==0
                rows=wire.listing(body)
                if rows and struct.unpack('!I',rows[0][156:160])[0]&1:break
                assert time.monotonic()<end,(status,rows);time.sleep(.05)
            assert struct.unpack('!I',rows[0][156:160])[0]==1,rows[0][156:160]
            guest=rconnect(6,auth[:16]);peers.append(guest)
            magic,version,operation,size,status,reserved=wire.HEADER.unpack(wire.exact(host,16))
            assert (magic,version,operation,size,status)==(b'DPTM',1,5,0,0)
            magic,version,operation,size,status,reserved=wire.HEADER.unpack(wire.exact(guest,16))
            assert (magic,version,operation,size,status)==(b'DPTM',1,6,0,0)
            payload=bytes(range(256))*20
            host.sendall(payload);assert wire.exact(guest,len(payload))==payload

            text=(root/'split-events.log').read_text()
            assert 'relay-redirected-to-separate-port' in text,text
            assert 'control-op-on-relay-port' in text,text
        finally:
            for s in peers:s.close()
            process.terminate();process.wait(10)

    # --relay-port without --relay, or equal to --port, is a startup error.
    for extra,label in ((['--relay-port','25001'],'relay-port-without-relay'),
                        (['--relay','--relay-port','25135'],'relay-port-same')):
        result=subprocess.run([exe,'--port','25135']+extra,cwd=root,
                              capture_output=True,text=True,timeout=10)
        assert result.returncode!=0,(label,result.returncode)

    print('Native relay: opt-in, authenticated host, single paired guest, consumed listing, bounded transport/backpressure, dedicated relay port and cleanup OK')
