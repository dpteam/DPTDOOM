"""Non-interactive engine smoke tests; supply a classic local IWAD."""

import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--gameversion")
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    assert subprocess.check_output([str(exe), "--version"], text=True).strip() == "DPT.DOOM 0.1.0-dev"

    with tempfile.TemporaryDirectory(prefix="dptdoom-headless-") as directory:
        work = Path(directory)
        command = [str(exe), "-iwad", str(iwad)]
        if args.gameversion:
            command += ["-gameversion", args.gameversion]

        def run(extra, success=True):
            result = subprocess.run(command + extra, cwd=work, capture_output=True,
                                    text=True, timeout=60)
            if success:
                assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            return result

        # Compare two full demos at uncapped speed, including their last RGB
        # frame. Speed/FPS itself is deliberately not used as a test assertion.
        signatures = []
        for _ in range(2):
            result = run(["-timedemo", "demo1"], success=False)
            timed = re.search(r"timed (\d+) gametics in .* fps", result.stderr)
            frame = re.search(r"Headless: frames=(\d+) rgb-fnv1a=([0-9a-f]{8})", result.stdout)
            assert timed and frame and result.returncode in (255, -1, 0xFFFFFFFF), (result.stdout, result.stderr)
            signatures.append((timed[1], frame[1], frame[2]))
        assert signatures[0] == signatures[1], signatures
        print("Repeated demo (gametics, frames, RGB FNV-1a):", signatures[0])

        # Verify normal demo completion, not just I_Error's timedemo path.
        wad = iwad.read_bytes()
        magic, count, offset = struct.unpack_from("<4sii", wad)
        assert magic == b"IWAD"
        for i in range(count):
            start, length, name = struct.unpack_from("<ii8s", wad, offset + i * 16)
            if name.rstrip(b"\0").upper() == b"DEMO1":
                demo = wad[start:start + length]
                break
        else:
            raise AssertionError("IWAD has no DEMO1")
        assert demo[0] in (106, 107, 108, 109) and demo[9:13] == b"\x01\0\0\0"
        (work / "smoke.lmp").write_bytes(demo[:13 + 70 * 4] + b"\x80")
        run(["-playdemo", "smoke.lmp"])
        assert (work / "default.cfg").is_file()
        assert (work / "dptdoom-doom.cfg").is_file()
        assert not (work / ".default.cfg").exists()
        print("Short demo: clean exit and correct config paths")

        # Persistence must actually read the config, not merely create files.
        config = work / "default.cfg"
        text = config.read_text()
        text, replacements = re.subn(r"(?m)^mouse_sensitivity\s+[^\n]+", "mouse_sensitivity 17", text)
        assert replacements == 1
        config.write_text(text)

        result = run(["-headless-frames", "5", "-headless-dump", "frame.ppm"])
        assert "Headless: frames=5 " in result.stdout, result.stdout
        assert re.search(r"(?m)^mouse_sensitivity\s+17\s*$", config.read_text())
        ppm = (work / "frame.ppm").read_bytes()
        header = b"P6\n640 400\n255\n"
        assert ppm.startswith(header) and len(ppm) == len(header) + 640 * 400 * 3
        assert any(ppm[len(header):]), "Frame dump must not be all black"
        print("Frame limit and PPM capture: OK")
        result = run(["-headless-frames", "1", "-headless-dump", "missing-dir/frame.ppm"], success=False)
        assert result.returncode != 0 and "cannot open frame dump" in result.stderr

        for extra in (["-headless-frames", "0"], ["-headless-frames", "-1"],
                      ["-headless-frames", "abc"], ["-headless-frames"],
                      ["-headless-dump"]):
            assert run(extra, success=False).returncode != 0, extra
        # Use a separate command for missing data (the first -iwad wins).
        result = subprocess.run([str(exe), "-iwad", "missing.wad"], cwd=work,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode != 0
        print("Invalid arguments and missing IWAD: nonzero exit, no dialogs")


if __name__ == "__main__":
    main()
