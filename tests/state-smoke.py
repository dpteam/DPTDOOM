"""Versioned per-tic diagnostics, demo baselines and save/load regression tests."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--baselines", type=Path, default=Path(__file__).with_name("state-baselines.json"))
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    wad = iwad.read_bytes()
    magic, count, directory = struct.unpack_from("<4sii", wad)
    assert magic == b"IWAD"
    lumps = {}
    entries = []
    for i in range(count):
        start, length, name = struct.unpack_from("<ii8s", wad, directory + i * 16)
        lumps[name.rstrip(b"\0").upper()] = wad[start:start + length]
        entries.append((name.rstrip(b"\0").upper(), wad[start:start + length]))
    versions = {106: "1.666", 107: "1.7", 108: "1.8", 109: "1.9"}
    records = {}

    with tempfile.TemporaryDirectory(prefix="dptdoom-state-") as temporary:
        work = Path(temporary)
        command = [str(exe), "-iwad", str(iwad)]

        def run(options, success=True):
            result = subprocess.run(command + options, cwd=work, capture_output=True, text=True, timeout=60)
            if success:
                assert result.returncode == 0, (options, result.returncode, result.stdout, result.stderr)
            else:
                # A deterministic error, never an access violation, hang, or silent success.
                assert result.returncode in (255, -1, 0xFFFFFFFF), (options, result.returncode, result.stderr)
            return result

        for name in (b"DEMO1", b"DEMO2", b"DEMO3"):
            if name not in lumps:
                continue
            demo = lumps[name]
            assert demo[0] in versions
            version = versions[demo[0]]
            options = ["-gameversion", version, "-timedemo", name.decode().lower(),
                       "-headless-trace", "trace.txt"]
            signatures = []
            for _ in range(2):
                result = run(options, success=False)  # timedemo intentionally reports via I_Error
                match = re.search(r"timed (\d+) gametics", result.stderr)
                assert match, result.stderr
                rows = [line.split() for line in (work / "trace.txt").read_text().splitlines()
                        if line and not line.startswith("#")]
                assert rows and all(len(row) == 5 for row in rows)
                assert [int(row[0]) for row in rows] == list(range(1, len(rows) + 1))
                canonical = "".join(" ".join(row) + "\n" for row in rows).encode("ascii")
                signatures.append({"gametics": int(match[1]), "rows": len(rows),
                                   "trace_sha256": hashlib.sha256(canonical).hexdigest(),
                                   "last_archive": rows[-1][3], "last_sim": rows[-1][4]})
            assert signatures[0] == signatures[1], (name, signatures)
            records[name.decode()] = signatures[0]
            print(name.decode(), json.dumps(signatures[0], sort_keys=True))

        demo = lumps[b"DEMO1"]
        version = versions[demo[0]]
        common = ["-gameversion", version]
        result = run(common + ["-timedemo", "demo1", "-headless-tics", "140",
                              "-headless-save-tic", "140", "-headless-trace", "checkpoint.txt"])
        saved = re.search(r"saved slot=0 archive-fnv1a-v1=([0-9a-f]{8}) leveltime=(\d+)", result.stdout)
        assert saved and int(saved[2]) == 140, result.stdout
        files = list(work.glob(".savegame/*.dsg"))
        assert len(files) == 1 and files[0].name == "doomsav0.dsg", files
        save = files[0]
        original = save.read_bytes()
        assert original[-1] == 0x1D and original[:24].startswith(b"DPT.DOOM TEST\0")

        # Verify before the first resumed tic (not after running simulation).
        load = common + ["-loadgame", "0", "-headless-tics", "35", "-headless-trace", "loaded.txt"]
        loaded_traces = []
        for _ in range(2):
            result = run(load)
            loaded = re.search(r"loaded archive-fnv1a-v1=([0-9a-f]{8}) leveltime=(\d+)", result.stdout)
            assert loaded and loaded.groups() == saved.groups(), (saved.groups(), result.stdout)
            loaded_traces.append((work / "loaded.txt").read_bytes())
        assert loaded_traces[0] == loaded_traces[1]
        print("Save/load: exact archive projection restored; resumed traces repeat")

        # Exercise successful replacement of an existing slot.
        warp = ["-warp", "1"] if b"MAP01" in lumps else ["-warp", "1", "1"]
        run(common + warp + ["-headless-tics", "5", "-headless-save-tic", "5"])
        replacement = save.read_bytes()
        assert replacement != original and replacement[-1] == 0x1D
        assert not (save.parent / "temp.dsg").exists()

        # Force commit failure by occupying the destination with a nonempty
        # directory. The sentinel must survive; the new temp save is retained.
        save.unlink()
        save.mkdir()
        sentinel = save / "keep.txt"
        sentinel.write_text("original destination")
        result = run(common + warp + ["-headless-tics", "5", "-headless-save-tic", "5"], success=False)
        assert "original slot was preserved" in result.stderr and sentinel.read_text() == "original destination"
        assert (save.parent / "temp.dsg").is_file()
        sentinel.unlink()
        save.rmdir()
        save.write_bytes(original)
        print("Save commit: replacement succeeds, failed commit preserves destination")

        for length in (0, 10, 30, 45, len(original) // 2, len(original) - 1):
            save.write_bytes(original[:length])
            result = run(common + ["-loadgame", "0", "-headless-tics", "1"], success=False)
            assert "Unexpected end of file" in result.stderr, result.stderr
        bad_version = bytearray(original)
        bad_version[24:40] = b"X" * 16  # deliberately no NUL
        save.write_bytes(bad_version)
        assert "Incompatible savegame version" in run(common + ["-loadgame", "0"], success=False).stderr
        bad_header = bytearray(original)
        bad_header[42] = 255  # map
        save.write_bytes(bad_header)
        assert "Invalid skill, episode or map" in run(common + ["-loadgame", "0"], success=False).stderr

        # Vanilla layout: 50-byte header, alignment to 52, one 280-byte player,
        # then sectors (14 bytes), lines (6) and each present sidedef (10).
        map_name = f"MAP{demo[3]:02d}".encode() if b"MAP01" in lumps else f"E{demo[2]}M{demo[3]}".encode()
        map_index = next(i for i, (name, _) in enumerate(entries) if name == map_name)
        map_lumps = dict(entries[map_index + 1:map_index + 11])
        linedefs = map_lumps[b"LINEDEFS"]
        sides_count = sum(struct.unpack_from("<H", linedefs, i + field)[0] != 0xFFFF
                          for i in range(0, len(linedefs), 14) for field in (10, 12))
        thinker = 52 + 280 + len(map_lumps[b"SECTORS"]) // 26 * 14 + len(linedefs) // 14 * 6 + sides_count * 10
        assert original[thinker] == 1
        mobj = (thinker + 4) & ~3
        for field, message in ((88, "Invalid mobj type"), (100, "Invalid mobj state"),
                               (132, "Invalid player index")):
            corrupt = bytearray(original)
            struct.pack_into("<i", corrupt, mobj + field, 0x7FFFFFFF)
            save.write_bytes(corrupt)
            assert message in run(common + ["-loadgame", "0", "-headless-tics", "1"], success=False).stderr
        save.write_bytes(original[:-1] + b"\0")
        assert "Bad savegame" in run(common + ["-loadgame", "0"], success=False).stderr
        save.unlink()
        assert "Cannot open savegame" in run(common + ["-loadgame", "0"], success=False).stderr
        print("Malformed/missing saves: bounded nonzero exit with clear diagnostics")

        for options in (["-headless-tics", "0"], ["-headless-save-tic", "-1"],
                        ["-headless-tics", "abc"], ["-headless-tics"],
                        ["-headless-trace"], ["-headless-trace", "missing-dir/trace.txt"]):
            run(options, success=False)

    digest = hashlib.sha256(wad).hexdigest()
    expected = json.loads(args.baselines.read_text())["iwads"].get(digest)
    if expected:
        assert records == expected["demos"], ("Baseline mismatch", records, expected)
        print("Pinned state baseline: PASS", expected["label"])
    else:
        print("No pinned baseline for IWAD SHA-256:", digest)
        print("Repeatability/save tests passed; do not infer compatibility with another IWAD.")


if __name__ == "__main__":
    main()
