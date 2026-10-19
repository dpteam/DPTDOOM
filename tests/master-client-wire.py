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
    finished = threading.Event()
    label = f"expected={expected} slow={slow} header={response[:16].hex()} diagnostic={diagnostic}"
    print("master-client-wire case:", label, flush=True)
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
                        part = sock.recv(16-len(request))
                        assert part, "native client closed before full request"
                        request += part
                    assert request[:4] == b"DPTM"
                    if slow:
                        # Keep the peer open until the native client has actually
                        # exited on its own deadline. A fixed 3s sleep competes
                        # with its 2.5s timer under loaded/suspended CI runners.
                        assert finished.wait(12), "native deadline did not finish"
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
        try:
            result = subprocess.run([exe, str(server.getsockname()[1]), expected], capture_output=True, timeout=12)
        finally:
            finished.set()
            worker.join(12)
        assert not worker.is_alive() and not errors, (label, errors)
        assert result.returncode == 0, (label, result.stdout, result.stderr)
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
# A bound, non-listening TCP socket is NOT a portable ECONNREFUSED fixture:
# macOS may leave the SYN pending. Both outcomes are valid, but neither may
# send a request or be reported as a successful connection.
def check_connect_failure(result):
    assert result.returncode == 0, (result.stdout, result.stderr)
    output = result.stdout
    assert b'stage=connect result=FAILED' in output and b'sent=0/16 received=0/16' in output, output
    if b'reason=connect ' in output:
        assert b'os_error=0 ' not in output, output
    else:
        assert b'reason=deadline ' in output and b'os_error=0 ' in output, output

print('master-client-wire case: bound non-listener (OS refusal OR connect deadline)', flush=True)
with socket.socket() as reservation:
    reservation.bind(('127.0.0.1',0))
    result=subprocess.run([exe,str(reservation.getsockname()[1]),'bad'],capture_output=True,timeout=12)
    check_connect_failure(result)
# Separately exercise an actually closed port; no bound socket remains to
# absorb SYNs on BSD. There is a small ephemeral-port reuse window, so retain
# bounded OS refusal/deadline semantics rather than guessing the OS outcome.
print('master-client-wire case: closed port connection failure', flush=True)
with socket.socket() as reservation:
    reservation.bind(('127.0.0.1',0))
    closed_port = reservation.getsockname()[1]
result=subprocess.run([exe,str(closed_port),'bad'],capture_output=True,timeout=12)
check_connect_failure(result)
print('Shared C master client: fragmented valid list, hostile headers/lengths, truncated/count mismatch and deadline OK')
