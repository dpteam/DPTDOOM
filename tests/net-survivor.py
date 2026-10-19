"""A peer leaves after 70 tics; host OR guest retains the world and runs to 140."""
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
    with tempfile.TemporaryDirectory(prefix="dptdoom-survivor-") as directory:
        root = Path(directory)
        for survivor in (0, 1):
            dirs = [root / f"{survivor}-host", root / f"{survivor}-guest"]
            for path in dirs:
                path.mkdir()
                (path / "input.bin").write_bytes(b"".join(
                    struct.pack(">bbhBBBB", 25 if tic < 15 else 0, 0, 0, 1 if tic < 25 else 0, 0, 0, 0)
                    for tic in range(140)))
            with socket.socket() as reserve:
                reserve.bind(("127.0.0.1", 0)); port = reserve.getsockname()[1]
            logs = [(path / "game.log").open("w") for path in dirs]
            processes = []
            base = [str(exe), "-iwad", str(iwad), "-warp", "1", "-nomonsters", "-dmflags", "3",
                    "-headless-input", "input.bin", "-headless-trace", "trace.txt",
                    "-headless-session-report", "-net-timeout", "3000"]
            try:
                for player in (0, 1):
                    options = ["-net-host", str(port)] if player == 0 else ["-net-join", f"127.0.0.1:{port}"]
                    options += ["-headless-tics", "140" if player == survivor else "70"]
                    processes.append(subprocess.Popen(base + options, cwd=dirs[player], stdout=logs[player], stderr=logs[player]))
                    if player == 0:
                        deadline = time.monotonic() + 10
                        while "listening on" not in (dirs[0] / "game.log").read_text():
                            assert processes[0].poll() is None
                            assert time.monotonic() < deadline
                            time.sleep(0.02)
                codes = [process.wait(timeout=25) for process in processes]
                outputs = [(path / "game.log").read_text() for path in dirs]
                assert codes == [0, 0], (codes, outputs)
                log = outputs[survivor]
                assert "continuing alone as player " + str(survivor+1) in log, log
                assert "returned to menu" not in log and "Network desync" not in log, log
                assert all("AddressSanitizer" not in output for output in outputs), outputs
                expected_mask = "10" if survivor == 0 else "01"
                report = re.search(r"Session report: state=(\d+) player=(\d+) leveltime=(\d+) flags=(\d+) mask=(\d+) paused=(\d+) x=(-?\d+) y=(-?\d+) health=(-?\d+) ammo=(\d+)", log)
                assert report and report.groups()[:6] == ("0", str(survivor+1), "140", "3", expected_mask, "0"), log
                assert int(report[9]) == 100 and int(report[10]) < 50, report.groups()
                boundary = re.search(r"Survivor boundary: x=(-?\d+) y=(-?\d+) health=(-?\d+) ammo=(\d+)", log)
                assert boundary and boundary.groups()[2:] == report.groups()[8:], (boundary.groups() if boundary else None, report.groups())
                # Normal friction/collision settling can still move a resting
                # actor a fraction of a map unit; forbid respawn/teleport reset,
                # not legitimate simulation movement after the departure.
                assert abs(int(boundary[1]) - int(report[7])) < 8 * 65536
                assert abs(int(boundary[2]) - int(report[8])) < 8 * 65536
                rows = [(path / "trace.txt").read_text().splitlines()[1:] for path in dirs]
                assert len(rows[survivor]) == 140 and len(rows[1-survivor]) == 70
                assert rows[0][:70] == rows[1][:70], "Diverged before departure"
                assert [int(row.split()[2]) for row in rows[survivor]] == list(range(1, 141))
                departed = re.search(r"Session report:.* health=(-?\d+) ammo=(\d+)", outputs[1-survivor])
                assert departed and int(departed[1]) == 100
                print("Survivor retains world/rules/inventory and advances 70 -> 140 tics:", survivor+1)
            finally:
                for process in processes:
                    if process.poll() is None: process.kill(); process.wait()
                for log in logs: log.close()


if __name__ == "__main__":
    main()
