"""Two SDL/Win32 windows with real keyboard input and lockstep checksums."""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--dmflags", choices=("0", "1", "2", "3"), default="0")
    parser.add_argument("--local-end", action="store_true", help="Confirm/cancel F7 disconnect instead of closing host")
    parser.add_argument("--fast", action="store_true", help="Verify fast monster state does not leak into SP")
    parser.add_argument("--audio", action="store_true", help="Exercise initialized SFX teardown (honors SDL_AUDIODRIVER)")
    parser.add_argument("--nightmare", action="store_true", help="Use nightmare instead of medium skill")
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
    processes, handles = [], {}

    @callback_type
    def visit(hwnd, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in [p.pid for p in processes]:
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title, len(title))
            if title.value.startswith("DPT.DOOM - "):
                handles[pid.value] = hwnd
        return True

    with tempfile.TemporaryDirectory(prefix="dptdoom-net-window-") as temporary:
        root = Path(temporary)
        dirs = [root / "host", root / "guest"]
        for path in dirs: path.mkdir()
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            number = reservation.getsockname()[1]
        logs = [(path / "game.log").open("w") for path in dirs]
        try:
            base = [str(exe), "-iwad", str(iwad), "-warp", "1", "-net-timeout", "5000", "-nogui",
                    "-dmflags", args.dmflags, "-chat-debug"]
            base += ["-nomusic"] if args.audio else ["-nosound"]
            if args.fast: base += ["-fast"]
            if args.nightmare: base += ["-skill", "5"]
            processes.append(subprocess.Popen(base + ["-net-host", str(number)], cwd=dirs[0], stdout=logs[0], stderr=logs[0]))
            deadline = time.monotonic() + 15
            while "listening on" not in (dirs[0] / "game.log").read_text():
                assert processes[0].poll() is None
                assert time.monotonic() < deadline
                time.sleep(0.05)
            processes.append(subprocess.Popen(base + ["-net-join", f"127.0.0.1:{number}"], cwd=dirs[1], stdout=logs[1], stderr=logs[1]))
            while len(handles) < 2:
                user32.EnumWindows(visit, 0)
                assert all(p.poll() is None for p in processes)
                assert time.monotonic() < deadline
                time.sleep(0.05)
            for handle in handles.values(): user32.ShowWindow(handle, 0)

            def key(player, vk, pressed):
                scan = user32.MapVirtualKeyW(vk, 0)
                flags = 1 | scan << 16 | (0 if pressed else 0xC0000000)
                assert user32.PostMessageW(handles[processes[player].pid], 0x0100 if pressed else 0x0101, vk, flags)

            time.sleep(2)
            key(0, 0x26, True); key(1, 0x27, True)  # player1 walks, player2 turns
            key(1, 0x11, True)  # fire
            time.sleep(2)
            key(0, 0x26, False); key(1, 0x27, False); key(1, 0x11, False)
            def tap(player, vk):
                key(player, vk, True); time.sleep(0.06)
                key(player, vk, False); time.sleep(0.06)

            # T used to terminate both peers on the first chat destination
            # byte. Exercise real input/HUD, not only scripted ticcmd bytes.
            for player in (0, 1):
                tap(player, ord('T'))
                for char in "CANCEL": tap(player, ord(char))
                tap(player, 0x1B)  # Escape cancels
                tap(player, ord('T'))
                for char in "HELLO PEEX": tap(player, ord(char))
                tap(player, 0x08)  # backspace
                tap(player, ord('R')); tap(player, 0x0D)
            deadline = time.monotonic() + 5
            while not all(f"Chat received from player {2-i}: HELLO PEER" in (path / "game.log").read_text()
                          for i, path in enumerate(dirs)):
                assert all(p.poll() is None for p in processes), [(path / "game.log").read_text() for path in dirs]
                assert time.monotonic() < deadline, [(path / "game.log").read_text() for path in dirs]
                time.sleep(0.05)
            # Smaller viewport exercises input widget erase after open/cancel.
            tap(0, 0xBD)  # OEM_MINUS
            tap(0, ord('T')); tap(0, 0x1B)
            time.sleep(3)  # longer than BACKUPTICS, catches legacy consistency handling
            assert all(p.poll() is None for p in processes), [(path / "game.log").read_text() for path in dirs]
            # Disconnection while chat is open and the game is paused must not
            # carry pause/chat into the title or the next single-player game.
            tap(1, 0x13)  # Pause
            tap(1, ord('T'))
            tap(1, ord('X'))
            if args.local_end:
                tap(0, 0x76)  # F7 End Game
                tap(0, ord('N'))
                time.sleep(0.3)
                assert all(p.poll() is None for p in processes)
                assert "returned to menu" not in (dirs[0] / "game.log").read_text()
                tap(0, 0x76); tap(0, ord('Y'))
                survivors = (0, 1)
            else:
                assert user32.PostMessageW(handles[processes[0].pid], 0x0010, 0, 0)
                assert processes[0].wait(timeout=10) == 0
                survivors = (1,)
            deadline = time.monotonic() + 10
            while not all("returned to menu" in (dirs[i] / "game.log").read_text() for i in survivors):
                assert all(processes[i].poll() is None for i in survivors), [(path / "game.log").read_text() for path in dirs]
                assert time.monotonic() < deadline
                time.sleep(0.05)
            # Former guest must reset slot 1 -> slot 0 and safely start SP.
            tap(1, 0x0D)  # dismiss reason -> main menu
            tap(1, 0x0D)  # New Game
            tap(1, 0x0D)  # Doom II skill
            deadline = time.monotonic() + 10
            while "Single-player game started after disconnect" not in (dirs[1] / "game.log").read_text():
                assert processes[1].poll() is None, (dirs[1] / "game.log").read_text()
                assert time.monotonic() < deadline
                time.sleep(0.05)
            time.sleep(0.5)
            tap(1, 0x26)
            tap(1, 0x71)  # F2, save a real SP game after teardown
            tap(1, 0x0D)
            for char in "AFTER NET": tap(1, ord(char))
            tap(1, 0x0D)
            save = dirs[1] / ".savegame/doomsav0.dsg"
            deadline = time.monotonic() + 5
            while not save.is_file():
                assert processes[1].poll() is None
                assert time.monotonic() < deadline, (dirs[1] / "game.log").read_text()
                time.sleep(0.05)
            saved = save.read_bytes()
            assert saved[43:47] == b"\x01\0\0\0", saved[43:47]  # no ghost player 2
            assert int.from_bytes(saved[47:50], "big") > 0  # SP is not stuck paused
            tap(1, 0x72); tap(1, 0x0D)  # F3 reload after SP reset
            time.sleep(0.5)
            for i in survivors:
                assert user32.PostMessageW(handles[processes[i].pid], 0x0010, 0, 0)
                assert processes[i].wait(timeout=10) == 0
            output = [(path / "game.log").read_text() for path in dirs]
            assert all("Network desync" not in log and "consistency failure" not in log for log in output), output
            assert all("ERROR: AddressSanitizer:" not in log for log in output), output
            assert "Network session ended:" in output[1], output[1]
            print("Window coop: chat/gameplay, menu recovery, guest SP reset/save/load OK", "F7 cancel+confirm" if args.local_end else "peer close")
        finally:
            for process in processes:
                if process.poll() is None: process.kill(); process.wait()
            for log in logs: log.close()


if __name__ == "__main__":
    main()
