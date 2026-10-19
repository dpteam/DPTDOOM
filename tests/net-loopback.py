"""Two real headless clients, same resources, bounded deterministic coop ticks."""
import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--guest-exe", type=Path, help="Optional second compiler/backend build")
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    guest_exe = args.guest_exe.resolve(strict=True) if args.guest_exe else exe
    with tempfile.TemporaryDirectory(prefix="dptdoom-net-") as directory:
        root = Path(directory)
        host, guest = root / "host", root / "guest"
        host.mkdir(); guest.mkdir()
        number = port()
        for index, path in enumerate((host, guest)):
            (path / "input.bin").write_bytes(b"".join(
                struct.pack(">bbhBBBB", 25 if tic < 35 else 0, (10 if index else -10) if tic < 50 else 0,
                            256 if index else -256, 1 if tic % 20 < 10 else 0, 0, 0, 0)
                for tic in range(210)))
        base = [str(exe), "-iwad", str(iwad), "-warp", "1", "-net-timeout", "5000",
                "-headless-tics", "210", "-headless-trace", "state.txt", "-headless-input", "input.bin"]
        processes = []
        with (host / "output.txt").open("w") as hlog, (guest / "output.txt").open("w") as glog:
            try:
                processes.append(subprocess.Popen(base + ["-net-host", str(number)], cwd=host, stdout=hlog, stderr=hlog))
                deadline = time.monotonic() + 10
                while "listening on" not in (host / "output.txt").read_text():
                    assert processes[0].poll() is None, (host / "output.txt").read_text()
                    assert time.monotonic() < deadline
                    time.sleep(0.05)
                processes.append(subprocess.Popen([str(guest_exe)] + base[1:] + ["-net-join", f"127.0.0.1:{number}"], cwd=guest, stdout=glog, stderr=glog))
                codes = [process.wait(timeout=30) for process in processes]
                assert codes == [0, 0], [(code, (path / "output.txt").read_text()) for code, path in zip(codes, (host, guest))]
                logs = [(path / "output.txt").read_text() for path in (host, guest)]
                assert "player 1 of 2" in logs[0] and "player 2 of 2" in logs[1], logs
                assert all("AddressSanitizer" not in log for log in logs), logs
                a, b = [(path / "state.txt").read_text() for path in (host, guest)]
                assert a == b, (a, b)
                assert len([line for line in a.splitlines() if not line.startswith("#")]) == 210
                print("Direct coop: two players / movement+fire+monsters / 210 matching per-tic states OK")
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.kill(); process.wait()


if __name__ == "__main__":
    main()
