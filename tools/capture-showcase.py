"""Capture README images from WebView2 / engine buffers, never the Windows desktop.

Requires Pillow and locally built executables + a legally owned Doom II IWAD.
No IWAD/resource extraction, saves, real launcher profiles or external websites.
GPL-2.0-or-later; see LICENSE.
"""
import argparse
import base64
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.request
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHOTS = (("game-demo1.png", "demo1", 350),
         ("game-demo2.png", "demo2", 500),
         ("game-demo3.png", "demo3", 700))


def game(args, output):
    with tempfile.TemporaryDirectory(prefix="dptdoom-showcase-game-") as temporary:
        work = Path(temporary)
        hashes = {}
        for name, demo, ticks in SHOTS:
            ppm = work / "frame.ppm"
            command = [str(args.engine), "-iwad", str(args.iwad), "-timedemo", demo,
                       "-headless-tics", str(ticks), "-headless-session-report", "-headless-dump", str(ppm)]
            result = subprocess.run(command, cwd=work, capture_output=True, timeout=45)
            if result.returncode or b"Session report: state=0" not in result.stdout:
                raise RuntimeError((result.returncode, result.stdout, result.stderr))
            with Image.open(ppm) as frame:
                if frame.size != (640, 400) or frame.mode != "RGB":
                    raise RuntimeError("Unexpected engine framebuffer dimensions/format")
                frame.save(output / name, optimize=True)
            hashes[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
        return hashes


def launcher(args, output):
    # Reuse the stdlib-only CDP test client; no Selenium/Node/browser install.
    spec = importlib.util.spec_from_file_location("launcher_capture_client", ROOT / "tests/launcher-windows.py")
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="dptdoom-showcase-ui-") as temporary:
        work = Path(temporary)
        env = os.environ.copy()
        env["WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS"] = f"--remote-debugging-port={port} --remote-debugging-address=127.0.0.1"
        process = subprocess.Popen([str(args.launcher), "--data-dir", str(work)], cwd=work, env=env)
        cdp = None
        try:
            def target():
                if process.poll() is not None:
                    raise RuntimeError("Launcher exited before capture")
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json", timeout=1) as response:
                        return next((row for row in json.load(response) if row.get("type") == "page"
                                     and row.get("title", "").startswith("DPT.DOOM")), None)
                except OSError:
                    return None
            tab = client.wait(target)
            cdp = client.CDP(tab["webSocketDebuggerUrl"])
            client.wait(lambda: cdp.js("document.getElementById('status')?.textContent.includes('Выбери ресурсы')"))
            # Documentation-only preview paths, not personal filesystem paths.
            # Never submit save/launch: these illustrative paths need not exist.
            cdp.js("$('executable').value='C:\\\\Games\\\\DPTDOOM\\\\dptdoom.exe';"
                   "$('iwad').value='C:\\\\Games\\\\DOOM2.WAD';send('preview')")
            client.wait(lambda: "C:" in cdp.js("$('preview').textContent"))
            cdp.js("$('storage').hidden=true")  # hide the isolated temp path, not game UI
            cdp.call("Emulation.setDeviceMetricsOverride", {"width": 1180, "height": 1240,
                                                          "deviceScaleFactor": 1, "mobile": False})
            for theme in ("light", "dark"):
                cdp.js(f"document.documentElement.dataset.theme='{theme}';themeUI()")
                time.sleep(.3)  # CSS colour transition has duration .2s
                capture = cdp.call("Page.captureScreenshot", {"format": "png", "captureBeyondViewport": True})
                path = output / f"launcher-{theme}.png"
                path.write_bytes(base64.b64decode(capture["data"]))
                with Image.open(path) as image:
                    if image.size != (1180, 1240):
                        raise RuntimeError("Unexpected WebView viewport capture")
                    image.convert("RGB").save(path, optimize=True)
        finally:
            if cdp:
                cdp.sock.close()
            @callback
            def close(hwnd, _):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == process.pid:
                    user.PostMessageW(hwnd, 0x10, 0, 0)
                return True
            user.EnumWindows(close, 0)
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(5)
            deadline = time.monotonic() + 20
            while work.exists():
                try:
                    shutil.rmtree(work)
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(.25)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True, help="headless executable")
    parser.add_argument("--iwad", type=Path, required=True, help="legally owned Doom II IWAD with DEMO1/2/3")
    parser.add_argument("--launcher", type=Path, help="Windows launcher EXE (optional)")
    parser.add_argument("--output", type=Path, default=ROOT / "build/showcase", help="default ignored build/showcase")
    args = parser.parse_args()
    args.engine = args.engine.resolve(strict=True)
    args.iwad = args.iwad.resolve(strict=True)
    if args.launcher:
        args.launcher = args.launcher.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    hashes = game(args, args.output.resolve())
    if args.launcher:
        launcher(args, args.output.resolve())
    print(json.dumps({"game_png_sha256": hashes, "output": str(args.output.resolve())}, indent=2))


if __name__ == "__main__":
    main()
