"""Bounded handshake/packet failures against a real headless host (local IWAD)."""
import argparse
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def read_exact(sock, count):
    data = b""
    while len(data) < count:
        part = sock.recv(count - len(data))
        assert part, "Unexpected EOF"
        data += part
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--recover", action="store_true", help="Post-handshake failures must return to menu, not exit with error")
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="dptdoom-net-errors-") as temporary:
        root = Path(temporary)
        # Argument validation must fail before listening, with no hanging peer.
        for options in (["-net-host"], ["-net-host", "0"], ["-net-host", "65536"],
                        ["-net-host", "1234", "-net-join", "127.0.0.1:1234"],
                        ["-net-join", "localhost:1234"], ["-net-join", "127.0.0.1"],
                        ["-net-host", "1234", "-net-timeout", "abc"],
                        ["-net-host", "1234", "-dmflags", "4"], ["-dmflags", "2"],
                        ["-dmflags"], ["-net-host", "1234", "-dmflags", "-1"],
                        ["-net-host", "1234", "-record", "test"]):
            result = subprocess.run([str(exe), "-iwad", str(iwad)] + options, cwd=root,
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode in (255, -1, 0xFFFFFFFF), (options, result.returncode, result.stderr)
            assert "AddressSanitizer" not in result.stderr, result.stderr
        print("Network argument validation: OK")
        for scenario, expected in (("version", "protocol mismatch"), ("engine", "engine version mismatch"),
                                   ("resources", "resource content/order mismatch"), ("settings", "game settings mismatch"),
                                   ("dmflags", "dmflags mismatch"),
                                   ("disconnect", "Network:"), ("truncated", "Network:"),
                                   ("tic-truncated", "truncated tic packet"),
                                   ("silent", "Network timeout"), ("timeout", "Network timeout"), ("order", "out-of-order"),
                                   ("desync", "Network desync"), ("command", "invalid player command")):
            if args.recover and scenario in ("version", "engine", "resources", "settings", "dmflags", "truncated", "silent"):
                continue  # startup errors deliberately still fail fast
            work = root / scenario
            work.mkdir()
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                number = reservation.getsockname()[1]
            path = work / "log.txt"
            with path.open("w") as log:
                process = subprocess.Popen([str(exe), "-iwad", str(iwad), "-net-host", str(number),
                                            "-net-timeout", "1500", "-headless-tics", "3", "-dmflags", "3"]
                                           + ([] if args.recover else ["-net-fail-fast"]),
                                           cwd=work, stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 10
                    while "listening on" not in path.read_text():
                        assert process.poll() is None, path.read_text()
                        assert time.monotonic() < deadline
                        time.sleep(0.02)
                    with socket.create_connection(("127.0.0.1", number), timeout=3) as peer:
                        hello = bytearray(read_exact(peer, 100))
                        struct.pack_into(">I", hello, 88, 1)  # opposite player
                        field = {"version": 8, "engine": 12, "resources": 32, "settings": 52, "dmflags": 96}.get(scenario)
                        if field is not None:
                            hello[field] ^= 1
                        if scenario == "truncated":
                            peer.sendall(hello[:13])
                        elif scenario == "silent":
                            time.sleep(2)
                        else:
                            # Fragment the real TCP stream deliberately.
                            for chunk in (hello[:7], hello[7:31], hello[31:]):
                                peer.sendall(chunk)
                            if scenario == "timeout":
                                read_exact(peer, 24)
                                time.sleep(2)
                            if scenario == "tic-truncated":
                                tic = read_exact(peer, 24)
                                peer.sendall(tic[:7])
                            if scenario in ("order", "desync", "command"):
                                tic = bytearray(read_exact(peer, 24))
                                if scenario == "order":
                                    struct.pack_into(">I", tic, 4, 9)
                                elif scenario == "desync":
                                    struct.pack_into(">I", tic, 8, 1)
                                else:
                                    tic[12] = 127
                                peer.sendall(tic)
                        if scenario not in ("disconnect", "truncated", "tic-truncated"):
                            code = process.wait(timeout=10)
                    code = process.wait(timeout=10)
                    output = path.read_text()
                    assert code in ((0,) if args.recover else (255, -1, 0xFFFFFFFF)) and expected in output, (scenario, code, output)
                    if args.recover:
                        if scenario in ("disconnect", "timeout"):
                            assert "continuing alone" in output and "returned to menu" not in output, output
                        else:
                            assert "returned to menu" in output and "continuing alone" not in output, output
                    assert "AddressSanitizer" not in output, output
                    print("Network error:", scenario, "OK")
                finally:
                    if process.poll() is None:
                        process.kill(); process.wait()


if __name__ == "__main__":
    main()
