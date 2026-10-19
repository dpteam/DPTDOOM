"""Exercise a Win32 or SDL Windows build with a local IWAD; no game data is shipped.

python tests/windows-smoke.py --exe build/msvc/Release/dptdoom.exe --iwad path/to/doom1.wad
"""

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import re
import subprocess
import struct
import tempfile
import time


def close_window(pid):
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    titles = []

    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title, len(title))
            if title.value.startswith("DPT.DOOM - "):
                titles.append(title.value)
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        return True

    user32.EnumWindows(visit, 0)
    return titles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--gameversion", help="For example 1.8 for the shareware v1.8 IWAD")
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    assert subprocess.check_output([str(exe), "--version"], text=True).strip() == "DPT.DOOM 0.1.0-dev"

    with tempfile.TemporaryDirectory(prefix="dptdoom-smoke-") as directory:
        work = Path(directory)
        command = [str(exe), "-iwad", str(iwad), "-nosound", "-nogui"]
        if args.gameversion:
            command += ["-gameversion", args.gameversion]
        result = subprocess.run(command + ["-timedemo", "demo1"], cwd=work,
                                capture_output=True, text=True, timeout=60)
        # Vanilla reports a completed timedemo through I_Error, not exit(0).
        match = re.search(r"timed (\d+) gametics in (\d+) realtics \(([^)]+) fps\)", result.stderr)
        assert match and result.returncode in (255, -1, 0xFFFFFFFF), (result.returncode, result.stdout, result.stderr)
        print("Timedemo:", match.group(0))

        # Extract two seconds from the built-in demo (classic 13-byte header,
        # four bytes per tic for one player) so this remains a quick test.
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
        assert demo[0] in (106, 107, 108, 109) and demo[9:13] == b"\x01\0\0\0", "Expected a classic single-player demo"
        (work / "smoke.lmp").write_bytes(demo[:13 + 70 * 4] + b"\x80")

        # Plain demo playback must end through I_Quit instead of continuing
        # after the video buffer has been freed by exit callbacks.
        result = subprocess.run(command + ["-playdemo", "smoke.lmp"], cwd=work,
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert (work / "default.cfg").is_file()
        assert (work / "dptdoom-doom.cfg").is_file()
        assert not (work / ".default.cfg").exists()
        print("Demo playback: clean exit and correct config paths")

        with (work / "window.log").open("w") as log:
            process = subprocess.Popen(command, cwd=work, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 15
                titles = []
                while not titles and time.monotonic() < deadline:
                    assert process.poll() is None, "Game exited before window close test"
                    titles = close_window(process.pid)
                    if not titles:
                        time.sleep(0.1)
                assert titles, "No branded game window found"
                assert process.wait(timeout=10) == 0, "WM_CLOSE did not exit cleanly"
                print("Window title and WM_CLOSE:", titles[0])
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
