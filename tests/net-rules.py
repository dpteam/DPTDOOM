"""Real engine rule checks using a temporary map overlay from a local IWAD."""
import argparse
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    wad = iwad.read_bytes()
    _, count, directory = struct.unpack_from("<4sii", wad)
    entries = []
    for i in range(count):
        start, length, name = struct.unpack_from("<ii8s", wad, directory + i * 16)
        entries.append((name.rstrip(b"\0"), wad[start:start + length]))
    map_index = next((i for i, (name, _) in enumerate(entries) if name == b"MAP01"), None)
    if map_index is None:
        print("SKIP: this map overlay test currently requires a Doom II MAP01 IWAD")
        return 77
    level = entries[map_index:map_index + 11]
    things = list(struct.iter_unpack("<hhhhh", dict(level)[b"THINGS"]))
    starts = [thing for thing in things if thing[3] in (1, 2)]
    assert len(starts) == 2
    x, y, angle, _, _ = max(things, key=lambda thing: (thing[0]-starts[0][0])**2 + (thing[1]-starts[0][1])**2)
    # Two ordinary weapons, two extra multiplayer-only weapons at a known map
    # thing location, well away from starts. No extracted resources leave temp.
    custom = starts + [(x, y, angle, 2001, 7), (x, y, angle, 2006, 7),
                       (x, y, angle, 2001, 23), (x, y, angle, 2006, 23)]
    level = [(name, b"".join(struct.pack("<hhhhh", *thing) for thing in custom) if name == b"THINGS" else data)
             for name, data in level]
    with tempfile.TemporaryDirectory(prefix="dptdoom-rules-") as temporary:
        root = Path(temporary)
        overlay = root / "rules.wad"
        body = bytearray(b"PWAD" + struct.pack("<ii", len(level), 0))
        records = []
        for name, data in level:
            records.append((len(body), len(data), name.ljust(8, b"\0")))
            body.extend(data)
        struct.pack_into("<i", body, 8, len(body))
        for record in records: body.extend(struct.pack("<ii8s", *record))
        overlay.write_bytes(body)
        for flags in range(4):
            work = [root / f"{flags}-host", root / f"{flags}-guest"]
            for path in work: path.mkdir()
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0)); port = sock.getsockname()[1]
            command = [str(exe), "-iwad", str(iwad), "-file", str(overlay), "-warp", "1", "-nomonsters",
                       "-dmflags", str(flags), "-headless-rules-probe", "-headless-tics", "2", "-net-timeout", "5000"]
            processes = []
            logs = [(path / "log.txt").open("w") for path in work]
            try:
                processes.append(subprocess.Popen(command + ["-net-host", str(port)], cwd=work[0], stdout=logs[0], stderr=logs[0]))
                deadline = time.monotonic() + 10
                while "listening on" not in (work[0] / "log.txt").read_text():
                    assert processes[0].poll() is None, (work[0] / "log.txt").read_text()
                    assert time.monotonic() < deadline
                    time.sleep(0.02)
                processes.append(subprocess.Popen(command + ["-net-join", f"127.0.0.1:{port}"], cwd=work[1], stdout=logs[1], stderr=logs[1]))
                codes = [process.wait(timeout=20) for process in processes]
                outputs = [(path / "log.txt").read_text() for path in work]
                assert codes == [0, 0], (codes, outputs)
                expected = (str(flags), "2" if flags & 1 else "4", "0" if flags & 1 else "2", "pass" if flags & 2 else "block")
                for output in outputs:
                    match = re.search(r"Rules probe: dmflags=(\d+) weapons=(\d+) extra-weapons=(\d+) player-overlap=(\w+)", output)
                    assert match and match.groups() == expected, (expected, output)
                    assert "AddressSanitizer" not in output, output
                print("Real map rules:", expected, "OK")
            finally:
                for process in processes:
                    if process.poll() is None: process.kill(); process.wait()
                for log in logs: log.close()


if __name__ == "__main__":
    raise SystemExit(main())
