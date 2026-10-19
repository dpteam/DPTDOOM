"""Real engine + dummy SDL video/audio, using a local IWAD and synthetic music."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    wad = iwad.read_bytes()
    magic, count, directory = struct.unpack_from("<4sii", wad)
    assert magic == b"IWAD"
    lumps = {}
    for i in range(count):
        start, length, name = struct.unpack_from("<ii8s", wad, directory + i * 16)
        lumps[name.rstrip(b"\0").upper()] = wad[start:start + length]
    demo = lumps[b"DEMO1"]
    assert demo[0] in (106, 107, 108, 109) and demo[9:13] == b"\x01\0\0\0"
    versions = {106: "1.666", 107: "1.7", 108: "1.8", 109: "1.9"}

    with tempfile.TemporaryDirectory(prefix="dptdoom-sdl-engine-") as temporary:
        work = Path(temporary)
        (work / "smoke.lmp").write_bytes(demo[:13 + 35 * 4] + b"\x80")
        with wave.open(str(work / "tone.wav"), "wb") as output:
            output.setparams((1, 2, 44100, 0, "NONE", "not compressed"))
            output.writeframes(b"".join(struct.pack("<h", 6000 if (i // 100) % 2 else -6000)
                                        for i in range(44100)))
        music = work / "music"
        music.mkdir()
        (music / "doom2-music.cfg").write_text("".join(
            f"{hashlib.sha1(data).hexdigest()} = ../tone.wav\n"
            for data in lumps.values() if data.startswith((b"MUS\x1a", b"MThd"))))
        environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                           SDL_AUDIODRIVER="dummy")
        command = [str(exe), "-iwad", str(iwad), "-playdemo", "smoke.lmp", "-nogui",
                   "-gameversion", versions[demo[0]]]
        for flag in ([], ["-nosound"], ["-nosfx"], ["-nomusic"]):
            # Force GENMIDI even if this config was produced by another backend.
            (work / "default.cfg").write_text("snd_musicdevice 8\n")
            result = subprocess.run(command + flag, cwd=work, env=environment,
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, (flag, result.returncode, result.stdout, result.stderr)
            assert "Error loading" not in result.stderr and "Error playing" not in result.stderr, result.stderr
            assert (work / "dptdoom-doom.cfg").is_file()
            print("SDL engine demo and shutdown:", flag or ["audio enabled"])

        failed = dict(environment, SDL_AUDIODRIVER="does-not-exist")
        result = subprocess.run(command, cwd=work, env=failed, capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert "Unable to set up sound" in result.stderr
        failed = dict(environment, SDL_VIDEODRIVER="does-not-exist")
        result = subprocess.run(command, cwd=work, env=failed, capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and "SDL_Init:" in result.stderr
        print("SDL engine: audio failure degrades gracefully; video failure exits cleanly")


if __name__ == "__main__":
    main()
