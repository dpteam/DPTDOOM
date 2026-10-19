"""Real WebView2/Win32 launcher integration. stdlib only; isolated data, local CDP."""
import argparse
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import urllib.request


def wait(check, seconds=20):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = check()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("Timed out: " + str(check))


class CDP:
    """Minimal synchronous localhost WebSocket client; no third-party test runtime."""
    def __init__(self, url):
        from urllib.parse import urlsplit
        u = urlsplit(url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=15)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET {u.path} HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            header += self.exact(1)
        assert b" 101 " in header, header
        expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        assert expected in header
        self.number = 0

    def exact(self, n):
        out = b""
        while len(out) < n:
            part = self.sock.recv(n - len(out))
            if not part:
                raise ConnectionError("CDP closed")
            out += part
        return out

    def call(self, method, params=None):
        self.number += 1
        payload = json.dumps({"id": self.number, "method": method, "params": params or {}}).encode()
        n, mask = len(payload), os.urandom(4)
        header = bytes([0x81, 0x80 | n]) if n < 126 else bytes([0x81, 0xfe]) + struct.pack("!H", n) if n < 65536 else bytes([0x81, 0xff]) + struct.pack("!Q", n)
        self.sock.sendall(header + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(payload)))
        fragments = b""
        while True:
            a, b = self.exact(2)
            n = b & 127
            if n == 126:
                n = struct.unpack("!H", self.exact(2))[0]
            elif n == 127:
                n = struct.unpack("!Q", self.exact(8))[0]
            assert n < 20_000_000 and not (b & 128)
            body = self.exact(n)
            if a & 15 == 8:
                raise ConnectionError("CDP closed frame")
            fragments += body
            if not a & 128:
                continue
            message = json.loads(fragments)
            fragments = b""
            if message.get("id") == self.number:
                assert "error" not in message, message
                return message.get("result", {})

    def js(self, code):
        result = self.call("Runtime.evaluate", {"expression": code, "returnByValue": True})
        assert "exceptionDetails" not in result, result
        return result.get("result", {}).get("value")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    args = parser.parse_args()
    exe, engine, iwad = (p.resolve(strict=True) for p in (args.exe, args.engine, args.iwad))
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    processes, clients, game_pids = [], [], []
    master, master_log = None, None

    def windows(pid):
        handles = []
        @callback
        def visit(hwnd, _):
            owner = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid:
                classname = ctypes.create_unicode_buffer(256)
                user.GetClassNameW(hwnd, classname, len(classname))
                if classname.value != "ConsoleWindowClass":
                    handles.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        return handles

    def free_port():
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    def connect(port):
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json", timeout=1) as r:
                return next((p for p in json.load(r) if p.get("type") == "page" and p.get("title", "").startswith("DPT.DOOM")), None)
        except (OSError, ValueError):
            return None

    with tempfile.TemporaryDirectory(prefix="dptdoom-launcher-") as temporary:
        root = Path(temporary)
        try:
            def start(directory, portable=False):
                port = free_port()
                env = os.environ.copy()
                env["WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS"] = f"--remote-debugging-port={port} --remote-debugging-address=127.0.0.1"
                env["SDL_AUDIODRIVER"] = "dummy"
                directory.mkdir(exist_ok=True)
                if portable:
                    # Marker lookup must be relative to EXE, even with another CWD.
                    local = directory / "dptdoom-launcher.exe"
                    shutil.copy2(exe, local)
                    shutil.copy2(exe.parent / "WebView2Loader.dll", directory)
                    (directory / "portable.txt").touch()
                    p = subprocess.Popen([str(local)], cwd=root, env=env)
                    storage = directory / "user"
                else:
                    p = subprocess.Popen([str(exe), "--data-dir", str(directory)], cwd=root, env=env)
                    storage = directory
                processes.append(p)
                target = wait(lambda: connect(port))
                c = CDP(target["webSocketDebuggerUrl"])
                clients.append(c)
                wait(lambda: c.js("document.getElementById('status')?.textContent.includes('Выбери ресурсы')"))
                for hwnd in windows(p.pid):
                    user.ShowWindow(hwnd, 0)
                return p, c, storage

            def set_fields(c, mode=0, slot=1, port=2342):
                c.js(f"$('executable').value={json.dumps(str(engine))};$('iwad').value={json.dumps(str(iwad))};$('slot').value='{slot}';$('port').value='{port}';mode={mode};modeUI();send('preview');")
                wait(lambda: '"-iwad"' in c.js("$('preview').textContent"))

            def launch(c, wait_window=True):
                c.js("$('launch').click()")
                status = wait(lambda: (s if "PID " in s else None) if (s := c.js("$('status').textContent")) else None)
                pid = int(status.split("PID ")[1].split()[0])
                game_pids.append(pid)
                if wait_window:
                    wait(lambda: windows(pid))
                for hwnd in windows(pid):
                    user.ShowWindow(hwnd, 0)
                return pid

            def close_game(pid):
                for hwnd in windows(pid):
                    user.PostMessageW(hwnd, 0x10, 0, 0)

            p, c, storage = start(root / "portable with spaces", True)
            assert c.js("$('storage').textContent").startswith("PORTABLE")
            assert (storage / "webview").is_dir()
            assert c.js("document.documentElement.dataset.theme") == "light"
            assert c.js("getComputedStyle(document.documentElement).getPropertyValue('--accent').trim()") == "#0088ff"
            geometry = c.js("JSON.stringify([...document.querySelectorAll('.panel')].map(e=>[e.offsetLeft,e.offsetTop,e.offsetWidth,e.offsetHeight]))")
            c.js("$('theme-toggle').click()")
            assert c.js("document.documentElement.dataset.theme") == "dark"
            assert c.js("$('theme-toggle').getAttribute('aria-pressed')") == "true"
            assert c.js("getComputedStyle(document.documentElement).getPropertyValue('--accent').trim()") == "#000088"
            assert c.js("JSON.stringify([...document.querySelectorAll('.panel')].map(e=>[e.offsetLeft,e.offsetTop,e.offsetWidth,e.offsetHeight]))") == geometry
            c.call('Page.reload')
            wait(lambda: c.js("document.getElementById('status')?.textContent.includes('Выбери ресурсы')"))
            assert c.js("document.documentElement.dataset.theme") == "dark"
            c.js("$('theme-toggle').click()")
            assert c.js("localStorage.getItem('dptdoom.theme')") == "light"
            set_fields(c)
            # Preview, HTML escaping, profile persistence and malformed IPC.
            c.js("mods=['<img src=x onerror=alert(1)>.wad'];modsUI();send('save')")
            wait(lambda: c.js("$('status').textContent==='Профиль сохранён.'"))
            assert c.js("document.querySelectorAll('#mods img').length") == 0
            c.js("mods=[];modsUI();$('load').click()")
            wait(lambda: c.js("mods.length===1"))
            c.js("mods=[];modsUI();send('save')")
            wait(lambda: c.js("$('status').textContent==='Профиль сохранён.'"))
            c.js("chrome.webview.postMessage('bogus')")
            wait(lambda: c.js("$('status').classList.contains('error')"))
            c.js("$('map').value='999';send('launch')")
            time.sleep(.2)
            assert c.js("$('status').textContent.includes('Проверь числа')")
            c.js("$('map').value='1';send('preview')")
            pid = launch(c)
            close_game(pid)
            wait(lambda: c.js("!$('launch').disabled"))
            assert (storage / "game/profile1/default.cfg").is_file()
            original = (storage / "game/profile1/default.cfg").read_bytes()
            # A busy profile lock must prevent a second engine overwriting saves.
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
            kernel.CreateFileW.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            lock = kernel.CreateFileW(str(storage / 'game/profile1/.running'), 0xC0000000, 0, None, 4, 0, None)
            assert lock != wintypes.HANDLE(-1).value
            try:
                c.js("$('launch').click()")
                wait(lambda: c.js("$('status').textContent.includes('уже используется')"))
            finally:
                kernel.CloseHandle(lock)
            pid = launch(c)
            close_game(pid)
            wait(lambda: c.js("!$('launch').disabled"))
            assert (storage / "game/profile1/default.cfg").read_bytes() == original
            # Host/join through real page buttons; no engine CLI bypass.
            port = free_port()
            master_port = free_port()
            master_log = (root / "master.stdout.log").open("wb")
            master = subprocess.Popen([str(exe.parent / "dptdoom-master.exe"), "--port", str(master_port),
                                       "--log", str(root / "master.events.log")], cwd=root, stdout=master_log, stderr=master_log)
            p2, c2, storage2 = start(root / "custom data")
            set_fields(c, 1, 2, port)
            c.js("$('extra').checked=true;$('pass').checked=true;send('preview')")
            c.js(f"$('serversNav').click();$('masterEndpoint').value='localhost:{master_port}';$('publishHost').checked=true;$('sessionName').value='<b>Browser test</b>';$('masterSave').click()")
            wait(lambda: c.js("$('status').textContent==='Настройки master сохранены.'"))
            pid1 = launch(c, False)
            wait(lambda: any('waiting coop host published' in f.read_text(errors='replace') for f in storage.glob('sessions/*/game.log')))
            set_fields(c2, 0, 1, 2342)
            c2.js(f"$('serversNav').click();$('masterEndpoint').value='localhost:{master_port}';$('refreshServers').click()")
            wait(lambda: c2.js("document.querySelectorAll('.server-card').length===1"))
            assert c2.js("document.querySelector('.server-card strong').textContent") == '<b>Browser test</b>'
            assert c2.js("document.querySelectorAll('.server-card b').length") == 0
            c2.js("document.querySelector('.server-card button').click()")
            wait(lambda: c2.js("mode===2&&$('port').value==='" + str(port) + "'"))
            assert c2.js("$('extra').checked&&$('pass').checked")
            assert c2.js("$('status').textContent.includes('не проверена')")
            pid2 = launch(c2)
            time.sleep(2)
            close_game(pid2)
            wait(lambda: c2.js("!$('launch').disabled"))
            close_game(pid1)
            wait(lambda: c.js("!$('launch').disabled"))
            c2.js("$('refreshServers').click()")
            wait(lambda: c2.js("$('status').textContent.includes('Список получен: 0')"))
            assert c2.js("document.querySelectorAll('.server-card').length") == 0
            assert '[Master]' in (storage / 'launcher.ini').read_text(encoding='utf-16')
            master.terminate();master.wait(10);master_log.close()
            c2.js("$('refreshServers').click()")
            wait(lambda: c2.js("$('status').textContent.includes('Master недоступен')"))
            c2.js("$('masterEndpoint').value='https://bad.invalid:5';$('refreshServers').click()")
            wait(lambda: c2.js("$('status').classList.contains('error')"))
            logs = [f.read_text(errors="replace") for d in (storage, storage2) for f in d.glob("sessions/*/game.log")]
            assert sum("connected" in text for text in logs) >= 2, logs
            assert not any("desync" in text.lower() for text in logs), logs
            # Blocking navigation must keep the trusted page/bridge in place.
            c.js("location.href='https://example.invalid/blocked'")
            time.sleep(.3)
            assert c.js("location.href") == "https://dptdoom.local.invalid/"
            assert c.js("document.title").startswith("DPT.DOOM")
            print("Launcher Windows: bridge/themes/portable, actual master publication + DNS browser selection/escaping/removal/outage, SP/COOP and blocked navigation OK")
        finally:
            if master is not None and master.poll() is None:
                master.terminate();master.wait(10)
            if master_log is not None:
                master_log.close()
            for pid in game_pids:
                for hwnd in windows(pid):
                    user.PostMessageW(hwnd, 0x10, 0, 0)
            for c in clients:
                c.sock.close()
            for p in processes:
                for hwnd in windows(p.pid):
                    user.PostMessageW(hwnd, 0x10, 0, 0)
            for p in processes:
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
            # WebView subprocesses release the isolated cache asynchronously;
            # wait for that rather than assuming a fixed sleep is sufficient.
            deadline = time.monotonic() + 20
            while root.exists():
                try:
                    shutil.rmtree(root)
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(.25)


if __name__ == "__main__":
    main()
