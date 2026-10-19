"""Exercise the native master EXE over real TCP, no IWAD/display/Python server."""
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

HEADER = struct.Struct("!4sHHIHH")


def info(port=2342, name=b"DPT test", map_name=b"MAP01", flags=0):
    return struct.pack("!64s20s20s12x8sHBBB3x", name, b"\xaa" * 20, b"\xbb" * 20,
                       map_name, port, 3, flags, 2)


def packet(op, body=b""):
    return HEADER.pack(b"DPTM", 1, op, len(body), 0, 0) + body


def exact(sock, count):
    data = b""
    while len(data) < count:
        part = sock.recv(count - len(data))
        assert part, (count, len(data))
        data += part
    return data


def reply(sock, op):
    magic, version, operation, length, status, reserved = HEADER.unpack(exact(sock, 16))
    assert (magic, version, operation, reserved) == (b"DPTM", 1, op, 0)
    assert length <= 4 + 128 * 160
    body = exact(sock, length)
    assert sock.recv(1) == b"", "one response then close required"
    return status, body


def listing(body):
    count = struct.unpack("!I", body[:4])[0]
    assert count <= 128 and len(body) == 4 + count * 160
    return [body[4 + n * 160:4 + (n + 1) * 160] for n in range(count)]


def main():
    exe = str(Path(sys.argv[1]).resolve(strict=True))
    for arg in ("--version", "--help"):
        result = subprocess.run([exe, arg], capture_output=True, timeout=10)
        assert result.returncode == 0
        if arg == "--version":
            assert b"DPT.DOOM" in result.stdout
    for args in (["--port", "0"], ["--port", "65536"], ["--port", "-1"],
                 ["--lease-seconds", "2"], ["--lease-seconds", "301"], ["--unknown"], ["--port"]):
        result = subprocess.run([exe] + args, capture_output=True, timeout=10)
        assert result.returncode != 0, args
    with tempfile.TemporaryDirectory(prefix="dptdoom-master-") as temporary:
        root = Path(temporary)
        for options, expected in ((["--bind", "example.invalid", "--log", str(root / "invalid-bind.log")], b"numeric IPv4"),
                                  (["--log", str(root / "missing/events.log")], b"cannot open event log"),
                                  (["--log-max-bytes", "4095"], b"Invalid/missing option")):
            result = subprocess.run([exe] + options, cwd=root, capture_output=True, timeout=10)
            assert result.returncode != 0 and expected in result.stdout + result.stderr
        # BSD/macOS does not promise all of 127/8 is configured as a local alias.
        # Discover test capability without privileged network configuration.
        aliases = True
        try:
            for number in range(2, 101):
                with socket.socket() as probe:
                    probe.bind((f"127.0.0.{number}", 0))
        except OSError:
            aliases = False
            print("Loopback aliases unavailable: multi-source wire cases covered by master-model; no aliases added", flush=True)

        def run_case(test):
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            with (root / (test.__name__ + ".log")).open("wb") as log:
                event_log = root / (test.__name__ + ".events.log")
                lease_seconds = "60" if test.__name__ == "maximum_list" else "3"
                process = subprocess.Popen([exe, "--port", str(port), "--lease-seconds", lease_seconds, "--log", str(event_log)],
                                           cwd=root, stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 10
                    while b"listening on" not in (root / (test.__name__ + ".log")).read_bytes():
                        assert process.poll() is None
                        assert time.monotonic() < deadline
                        time.sleep(.02)

                    def request(op, body=b"", source=None, fragmented=False):
                        with socket.socket() as sock:
                            sock.settimeout(5)
                            if source:
                                sock.bind((source, 0))
                            sock.connect(("127.0.0.1", port))
                            data = packet(op, body)
                            if fragmented:
                                for start in range(0, len(data), 7):
                                    sock.sendall(data[start:start + 7])
                                    time.sleep(.001)
                            else:
                                sock.sendall(data)
                            return reply(sock, op)
                    test(port, request)
                    assert process.poll() is None, (root / (test.__name__ + ".log")).read_text(errors="replace")
                except Exception:
                    print(f"Master loopback case FAILED: {test.__name__}, port={port}, aliases={aliases}", flush=True)
                    print((root / (test.__name__ + ".log")).read_text(errors="replace"), flush=True)
                    raise
                finally:
                    process.terminate()
                    process.wait(10)
            text = (root / (test.__name__ + ".log")).read_text(errors="replace")
            assert "AddressSanitizer" not in text and "runtime error:" not in text, text
            events = event_log.read_text()
            assert "listening on" in events
            if test.__name__ == "lifecycle":
                for event in ("register:", "heartbeat:", "remove:", "expire:", "unauthorized:"):
                    assert event in events, (event, events)

        def lifecycle(port, request):
            assert request(1) == (0, struct.pack("!I", 0))
            status, lease = request(2, info(), fragmented=True)
            assert status == 0 and len(lease) == 48
            auth = lease[:40]
            assert struct.unpack("!II", lease[40:]) == (3, 1)
            row = listing(request(1)[1])[0]
            assert row[:16] == auth[:16] and row[16:20] == socket.inet_aton("127.0.0.1")
            assert row[24:156] == info() and row[156:] == bytes(4)
            assert auth[16:] not in row, "lease secret must not be listed"
            assert request(2, info())[0] == 3
            bad = auth[:16] + bytes(24)
            assert request(3, bad)[0] == 2 and request(4, bad)[0] == 2
            if aliases:
                assert request(3, auth, source="127.0.0.2")[0] == 2
            time.sleep(2)
            assert request(3, auth) == (0, struct.pack("!I", 3))
            time.sleep(1.2)
            assert len(listing(request(1)[1])) == 1
            assert request(4, auth) == (0, bytes(4))
            assert listing(request(1)[1]) == []
            assert request(3, auth)[0] == 2
            status, new = request(2, info())
            assert status == 0 and new[:40] != auth
            time.sleep(3.2)
            assert listing(request(1)[1]) == []
            assert request(3, new[:40])[0] == 2
            events = (root / "lifecycle.events.log").read_bytes()
            for secret in (auth[16:], new[16:40]):
                assert secret not in events and secret.hex().encode() not in events
            # Same bind must fail, not silently share or steal the listener.
            duplicate = subprocess.run([exe, "--port", str(port), "--log", str(root / "bind-error.log")], cwd=root, capture_output=True, timeout=10)
            assert duplicate.returncode != 0

        def malformed(port, request):
            invalid = [info(map_name=b"MAP33"), info(name=b"bad\x1bname"), info(flags=32),
                       info(port=0), info()[:-4] + b"\x01\x00\x00\x00"]
            for body in invalid:
                assert request(2, body)[0] == 1
            for offset in (0, 4, 6, 8, 12, 14):
                data = bytearray(packet(1))
                data[offset] ^= 128
                with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
                    sock.sendall(data)
                    header = exact(sock, 16)
                    assert HEADER.unpack(header)[4] == 1
            # Truncated body and slow header must expire independently of activity.
            slow = socket.create_connection(("127.0.0.1", port), timeout=5)
            truncated = socket.create_connection(("127.0.0.1", port), timeout=5)
            try:
                slow.sendall(b"DPTM")
                truncated.sendall(packet(2, info())[:20])
                start = time.monotonic()
                assert listing(request(1)[1]) == []
                assert time.monotonic() - start < 1
                assert slow.recv(1) == b"" and truncated.recv(1) == b""
            finally:
                slow.close()
                truncated.close()
            assert request(1)[0] == 0

        def limits(port, request):
            for number in range(4):
                assert request(2, info(2400 + number))[0] == 0
            assert request(2, info(2404))[0] == 4
            rejected = False
            for _ in range(40):
                try:
                    request(1)
                except (AssertionError, OSError):
                    rejected = True
                    break
            assert rejected, "per-source request limit should close excess connections"
            time.sleep(.5)
            assert request(1)[0] == 0

        def connections(port, request):
            peers = []
            try:
                # Eight partial peers per source consume exactly 32 slots.
                for source in range(2, 6):
                    for _ in range(8):
                        sock = socket.socket()
                        sock.settimeout(5)
                        sock.bind((f"127.0.0.{source}", 0))
                        sock.connect(("127.0.0.1", port))
                        sock.sendall(b"DPTM")
                        peers.append(sock)
                    time.sleep(.05)
                    # A ninth connection from this IP must be closed, even when
                    # global slots remain available during the first iterations.
                    try:
                        request(1, source=f"127.0.0.{source}")
                    except (AssertionError, OSError):
                        pass
                    else:
                        raise AssertionError("per-IP connection cap not enforced")
                try:
                    request(1, source="127.0.0.20")
                except (AssertionError, OSError):
                    pass
                else:
                    raise AssertionError("global connection cap not enforced")
                # All slots recover after their absolute receive deadlines.
                time.sleep(3.2)
                assert request(1, source="127.0.0.20")[0] == 0
            finally:
                for sock in peers:
                    sock.close()

        def maximum_list(port, request):
            for number in range(128):
                source = f"127.0.0.{number // 4 + 2}"
                assert request(2, info(2400 + number % 4), source=source)[0] == 0
                # Capacity is not a rate-limit test. Pace below global refill
                # on fast Linux runners; Windows socket latency hid this race.
                time.sleep(.02)
            status, body = request(1)
            assert status == 0 and len(listing(body)) == 128
            assert len(body) == 20484
            assert request(2, info(), source="127.0.0.100")[0] == 4

        cases = [lifecycle, malformed, limits]
        if aliases:
            cases += [connections, maximum_list]
        for test in cases:
            run_case(test)
    print("Native master: real TCP lifecycle/fragmentation/auth/expiry, schema, slow/truncated peers, rate/capacity and bind/startup errors OK")


if __name__ == "__main__":
    main()
