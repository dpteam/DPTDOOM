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
    parser.add_argument("--dmflags", choices=("0", "1", "2", "3"), default="0")
    parser.add_argument("--chat", action="store_true", help="Send distinct legacy chat messages from both peers")
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    guest_exe = args.guest_exe.resolve(strict=True) if args.guest_exe else exe
    # Cancelled text must not prefix the next message. Include backspace and
    # high-bit untrusted input before a fresh destination resets the buffer.
    message = b"\x05CANCEL\x1b\x05HELLO PEEX\x7fR\x0d\x05\xff\xfe\x05SAFE\x0d"
    with tempfile.TemporaryDirectory(prefix="dptdoom-net-") as directory:
        root = Path(directory)
        host, guest = root / "host", root / "guest"
        host.mkdir(); guest.mkdir()
        number = port()
        for index, path in enumerate((host, guest)):
            (path / "input.bin").write_bytes(b"".join(
                struct.pack(">bbhBBBB", 25 if tic < 35 else 0, (10 if index else -10) if tic < 50 else 0,
                            256 if index else -256, 1 if tic % 20 < 10 else 0,
                            (message[tic - 10] if args.chat and 10 <= tic < 10 + len(message) else 0), 0, 0)
                for tic in range(210)))
        base = [str(exe), "-iwad", str(iwad), "-warp", "1", "-net-timeout", "5000",
                "-dmflags", args.dmflags,
                "-headless-tics", "210", "-headless-trace", "state.txt", "-headless-input", "input.bin"]
        if args.chat:
            base += ["-chat-debug"]
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
                if args.chat:
                    for player, log in enumerate(logs):
                        assert f"Chat received from player {2-player}: HELLO PEER" in log, log
                        assert f"Chat received from player {2-player}: SAFE" in log, log
                        assert "CANCELHELLO" not in log, log
                a, b = [(path / "state.txt").read_text() for path in (host, guest)]
                assert a == b, (a, b)
                assert len([line for line in a.splitlines() if not line.startswith("#")]) == 210
                print("Direct coop: two players / movement+fire+monsters / 210 matching per-tic states OK, dmflags=" + args.dmflags)
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.kill(); process.wait()


if __name__ == "__main__":
    main()
