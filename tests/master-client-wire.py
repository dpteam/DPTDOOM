"""Hostile directory responses sent to the real shared C client."""
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time

exe = str(Path(sys.argv[1]).resolve(strict=True))


def case(response, expected="bad", slow=False):
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
print('Shared C master client: fragmented valid list, hostile headers/lengths, truncated/count mismatch and deadline OK')
