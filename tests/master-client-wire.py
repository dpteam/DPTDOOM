"""Hostile directory responses sent to the real shared C client."""
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time

exe = str(Path(sys.argv[1]).resolve(strict=True))


def case(response, expected="bad", slow=False, diagnostic=None):
    errors = []
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(10)
        def peer():
            try:
                with server.accept()[0] as sock:
                    sock.settimeout(5)
                    request = b""
                    while len(request) < 16:
                        request += sock.recv(16-len(request))
                    assert request[:4] == b"DPTM"
                    if slow:
                        time.sleep(3)
                    else:
                        for start in range(0, len(response), 3):
                            sock.sendall(response[start:start+3])
                            time.sleep(.001)
            except (ConnectionResetError, BrokenPipeError):
                pass  # rejecting a malicious header may close before its body
            except BaseException as e:
                errors.append(e)
        worker = threading.Thread(target=peer)
        worker.start()
        result = subprocess.run([exe, str(server.getsockname()[1]), expected], capture_output=True, timeout=12)
        worker.join(10)
        assert not worker.is_alive() and not errors, errors
        assert result.returncode == 0, (result.stdout, result.stderr)
        if diagnostic:
            assert diagnostic.encode() in result.stdout, result.stdout
        for field in (b'endpoint=127.0.0.1:', b'local=', b'stage=', b'os_error=', b'elapsed_ms=', b'sent=', b'received='):
            assert field in result.stdout, result.stdout


def header(length, **changes):
    fields = dict(magic=b'DPTM', version=1, operation=1, length=length, status=0, reserved=0)
    fields.update(changes)
    return struct.pack('!4sHHIHH', *fields.values())


case(header(4) + bytes(4), "ok")
for value in (header(0xffffffff), header(4, magic=b'EVIL'), header(4, version=2),
              header(4, operation=2), header(4, reserved=1), header(4, status=5),
              header(4, status=1), header(3), header(4)+bytes(2), header(4)+struct.pack('!I',129)):
    case(value)
case(b'', slow=True)
case(header(4, magic=b'HTTP'), diagnostic='reply-magic-not-DPTM')
case(header(4, version=2), diagnostic='reply-directory-version')
case(header(4)+bytes(2), diagnostic='peer-closed-before-complete-reply')
case(b'', slow=True, diagnostic='stage=receive-header result=FAILED reason=deadline')
for status, name in ((1,'BAD_REQUEST'),(2,'UNAUTHORIZED'),(3,'CONFLICT'),(4,'CAPACITY')):
    case(header(0,status=status),'reject',diagnostic=name)
# A reserved-but-not-listening local port produces a real OS refusal code.
with socket.socket() as reservation:
    reservation.bind(('127.0.0.1',0))
    result=subprocess.run([exe,str(reservation.getsockname()[1]),'bad'],capture_output=True,timeout=12)
    assert result.returncode==0 and b'reason=connect' in result.stdout, result.stdout
    assert b'os_error=0 ' not in result.stdout, result.stdout
print('Shared C master client: fragmented valid list, hostile headers/lengths, truncated/count mismatch and deadline OK')
