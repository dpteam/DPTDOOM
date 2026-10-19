"""Packaging regression checks in an isolated tiny git repo; no real binaries/game data."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile
import json


def main():
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="dptdoom-package-test-") as temporary:
        repo = Path(temporary)
        (repo / "tools").mkdir()
        shutil.copyfile(root / "tools/package-build.py", repo / "tools/package-build.py")
        (repo / "LICENSE").write_text("test license")

        def git(*args):
            subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True)

        git("init")
        git("add", "tools/package-build.py", "LICENSE")
        git("-c", "user.name=Package Test", "-c", "user.email=test@invalid", "commit", "-m", "fixture")
        install = repo / "install"
        (install / "bin").mkdir(parents=True)
        (install / "share/dptdoom").mkdir(parents=True)
        for name in ("dptdoom.exe", "SDL2.dll", "SDL2_mixer.dll"):
            (install / "bin" / name).write_bytes(b"synthetic fixture, not an executable")
        for name in ("LICENSE", "SDL-LICENSES.txt"):
            (install / "share/dptdoom" / name).write_text("test notice")
        (install / "bin/DOOM2.WAD").write_bytes(b"must not ship")
        (install / "bin/default.cfg").write_text("private settings")
        command = [sys.executable, str(repo / "tools/package-build.py"), "--install", str(install),
                   "--output", str(repo / "packages"), "--name", "fixture", "--kind", "windows-sdl"]
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        package = next((repo / "packages").glob("*.zip"))
        original = package.read_bytes()
        with zipfile.ZipFile(package) as zipped:
            names = zipped.namelist()
            assert "DPTDOOM/bin/dptdoom.exe" in names and "DPTDOOM/SOURCE.zip" in names
            assert not any(name.lower().endswith((".wad", ".cfg")) for name in names)
            assert len(json.loads(zipped.read("DPTDOOM/BUILD.json"))["commit"]) == 40
        assert subprocess.run(command, cwd=repo, capture_output=True).returncode != 0
        assert package.read_bytes() == original
        (repo / "LICENSE").write_text("dirty source")
        assert subprocess.run(command, cwd=repo, capture_output=True).returncode != 0
        git("add", "LICENSE")
        git("-c", "user.name=Package Test", "-c", "user.email=test@invalid", "commit", "-m", "changed fixture")
        (repo / "private.wad").write_bytes(b"forbidden tracked data")
        git("add", "private.wad")
        git("-c", "user.name=Package Test", "-c", "user.email=test@invalid", "commit", "-m", "bad fixture")
        assert subprocess.run(command, cwd=repo, capture_output=True).returncode != 0
        print("Packaging: payload allowlist, source revision, no overwrite/dirty/data leakage OK")


if __name__ == "__main__":
    main()
