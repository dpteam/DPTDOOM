"""Windows integration test: F2 save in attract demo, F3 load and take over."""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
    user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]

    with tempfile.TemporaryDirectory(prefix="dptdoom-demo-menu-") as temporary:
        work = Path(temporary)
        log_path = work / "game.log"
        with log_path.open("w") as log:
            process = subprocess.Popen([str(exe), "-iwad", str(iwad), "-nosound", "-nogui"],
                                       cwd=work, stdout=log, stderr=log)
            try:
                windows = []

                @callback_type
                def visit(hwnd, _):
                    owner = wintypes.DWORD()
                    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                    if owner.value == process.pid:
                        title = ctypes.create_unicode_buffer(256)
                        user32.GetWindowTextW(hwnd, title, len(title))
                        if title.value.startswith("DPT.DOOM - "):
                            windows.append(hwnd)
                    return True

                def wait(condition, timeout=10):
                    deadline = time.monotonic() + timeout
                    while time.monotonic() < deadline:
                        assert process.poll() is None, (process.returncode, log_path.read_text(errors="replace"))
                        if condition():
                            return
                        time.sleep(0.05)
                    raise AssertionError(log_path.read_text(errors="replace"))

                def window_ready():
                    user32.EnumWindows(visit, 0)
                    return bool(windows)

                wait(window_ready)
                hwnd = windows[0]
                # Keep the automated keyboard test separate from the owner's
                # playable window. Messages target this HWND, not global input.
                user32.ShowWindow(hwnd, 0)

                def key(vk):
                    scan = user32.MapVirtualKeyW(vk, 0)
                    assert user32.PostMessageW(hwnd, 0x0100, vk, 1 | (scan << 16))
                    time.sleep(0.08)
                    assert user32.PostMessageW(hwnd, 0x0101, vk, 1 | (scan << 16) | 0xC0000000)
                    time.sleep(0.08)

                # Wait for actual playback rather than assuming machine speed.
                wait(lambda: "Playing demo demo1 (version" in log_path.read_text(), timeout=20)
                time.sleep(1)
                key(0x71)  # F2
                key(0x0D)  # select slot 0
                for character in "DEMO":
                    key(ord(character))
                key(0x0D)
                save = work / ".savegame/doomsav0.dsg"
                wait(lambda: save.is_file())
                data = save.read_bytes()
                assert data[:24].startswith(b"DEMO\0") and data[-1] == 0x1D
                wait(lambda: "Demo snapshot saved; playback continues." in log_path.read_text())
                time.sleep(0.3)
                assert process.poll() is None
                key(0x72)  # F3
                key(0x0D)
                wait(lambda: "loading snapshot as single-player" in log_path.read_text())
                # Live input after takeover, then save again through normal flow.
                key(0x26)  # Up
                key(0x71)
                key(0x0D)
                key(0x0D)
                wait(lambda: save.read_bytes() != data)
                assert user32.PostMessageW(hwnd, 0x0010, 0, 0)
                assert process.wait(timeout=10) == 0, log_path.read_text()
                print("Attract demo: F2 snapshot, continued playback, F3 takeover and live save OK")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
