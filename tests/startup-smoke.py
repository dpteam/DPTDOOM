"""Data-free startup/error paths; never use a real IWAD or physical display."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    exe = str(Path(sys.argv[1]).resolve(strict=True))
    backend = sys.argv[2]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", DISPLAY="", DOOMWADDIR="", DOOMWADPATH="")
    with tempfile.TemporaryDirectory(prefix="dptdoom-startup-") as directory:
        result = subprocess.run([exe, "--version"], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0 and result.stdout.strip() == "DPT.DOOM 0.1.0-dev", result
        result = subprocess.run([exe, "-iwad", "does-not-exist.wad", "-nogui", "-nosound"],
                                cwd=directory, env=env, capture_output=True, text=True, timeout=10)
        assert result.returncode in (255, -1, 0xFFFFFFFF), (result.returncode, result.stdout, result.stderr)
        assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr, result.stderr
        if backend == "x11":
            assert "Cannot open X11 display" in result.stderr, result.stderr
        else:
            assert "IWAD" in result.stderr, result.stderr
        print("Data-free startup: version and bounded error exit OK", backend)


if __name__ == "__main__":
    main()
