"""Package an allowlisted CMake install + exact-commit source, never build trees/IWADs."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--kind", choices=("windows-sdl", "headless"), required=True)
    parser.add_argument("--format", choices=("zip", "tar.gz"), default="zip")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.name):
        raise SystemExit("Package name must be a simple filename component")
    root = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root, text=True).strip():
        raise SystemExit("Refusing bundle: tracked source differs from HEAD; commit changes first")
    installed = args.install.resolve(strict=True)
    expected_binary = "dptdoom.exe" if args.kind == "windows-sdl" else "dptdoom-headless" + (".exe" if os.name == "nt" else "")
    binaries = {expected_binary, "dptdoom-master" + (".exe" if os.name == "nt" else "")}
    if args.kind == "windows-sdl":
        binaries |= {"SDL2.dll", "SDL2_mixer.dll", "dptdoom-launcher.exe", "WebView2Loader.dll"}
    files = {Path("bin") / name for name in binaries} | {
        Path("share/dptdoom/LICENSE"), Path("share/dptdoom/SDL-LICENSES.txt")}
    if args.kind == "windows-sdl":
        files |= {Path("share/dptdoom/WebView2-LICENSE.txt"), Path("share/dptdoom/WebView2-NOTICE.txt")}
    for file in files:
        if not (installed / file).is_file():
            raise SystemExit(f"Missing install payload: {file}")
    forbidden = {".wad", ".pk3", ".dsg", ".cfg", ".lmp", ".ppm", ".log", ".pdb"}
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output.resolve() / f"{args.name}-{commit[:12]}.{args.format}"
    if archive.exists():
        raise SystemExit(f"Refusing to overwrite an existing package: {archive}")
    with tempfile.TemporaryDirectory(prefix="dptdoom-package-") as temporary:
        bundle = Path(temporary) / "DPTDOOM"
        bundle.mkdir()
        for file in sorted(files):
            destination = bundle / file
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(installed / file, destination)
        # Source must accompany GPL binaries; HEAD archive excludes ignored
        # local refs/build data and is the same revision recorded in metadata.
        source = bundle / "SOURCE.zip"
        subprocess.run(["git", "archive", "--format=zip", f"--output={source}", commit], cwd=root, check=True)
        with zipfile.ZipFile(source) as zipped:
            for name in zipped.namelist():
                if Path(name).suffix.lower() in forbidden or name.startswith(("refs/", "build/", ".git/")):
                    raise SystemExit(f"Forbidden file in source archive: {name}")
        metadata = {"project": "DPT.DOOM", "commit": commit, "kind": args.kind,
                    "repository": "https://github.com/dpteam/DPTDOOM", "status": "experimental CI build, not a release"}
        (bundle / "BUILD.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        text = """DPT.DOOM - experimental test build, NOT A RELEASE

The game data is NOT included. Supply your own legally obtained Doom IWAD.
Extract the whole archive first. The executable and SDL DLLs are in bin/.

Windows SDL: cd bin, then: dptdoom.exe -iwad C:\\path\\to\\DOOM2.WAD
Or open bin/dptdoom-launcher.exe (local HTML UI; needs WebView2 Evergreen Runtime).
No runtime installer is run automatically. WebView2Loader.dll is included.
Portable: create an empty portable.txt NEXT TO the launcher; data goes to bin/user/.
Otherwise launcher data stays in LocalAppData/DPTDOOM. Logs: sessions/; saves/config: game/profileN/.
Headless: dptdoom-headless[.exe] -iwad /path/to/doom2.wad -headless-frames 70
Use --version to check startup without game data.

Native directory prototype: bin/dptdoom-master[.exe] --help
Default: loopback TCP 2343, directory protocol v1, experimental LAN only.
This is NOT a dedicated game server. Launcher listing/automatic publication
are not integrated yet. No TLS, game-port reachability proof or NAT traversal.

Direct coop is experimental, exactly 2 players, trusted LAN only (TCP).
Both peers need the same build, IWAD/PWAD contents/order and skill/map flags.
Host: dptdoom.exe -iwad DOOM2.WAD -warp 1 -net-host 2342 -net-bind 0.0.0.0
Join: dptdoom.exe -iwad DOOM2.WAD -warp 1 -net-join HOST_IPV4:2342
Match rules: add the same -dmflags N on both commands (0=vanilla default,
1=hide multiplayer-only coop weapons, 2=players pass through, 3=both).
This build uses direct protocol v2: do not mix different artifacts/builds.
Without -net-bind the host listens ONLY on localhost. No NAT traversal,
public server listing, authentication, encryption or Internet hardening.

SOURCE.zip contains the corresponding project source at the BUILD.json commit.
See share/dptdoom/LICENSE, SDL-LICENSES.txt and source docs for notices,
dependencies and build instructions. No warranty. Do not redistribute IWADs.
Unix headless files are platform-specific; chmod +x bin/dptdoom-headless
if your extraction tool did not preserve executable permissions.
"""
        if args.kind == "headless":
            text += "\nHEADLESS DIAGNOSTIC BUNDLE: no game window or audio output.\n"
        (bundle / "START-HERE.txt").write_text(text, encoding="utf-8")
        checksums = []
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(bundle).as_posix()}")
        (bundle / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="ascii")
        if args.format == "zip":
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
                for path in sorted(bundle.rglob("*")):
                    if path.is_file():
                        zipped.write(path, path.relative_to(bundle.parent).as_posix())
        else:
            with tarfile.open(archive, "w:gz") as tar:
                tar.add(bundle, arcname="DPTDOOM")
    print(archive)


if __name__ == "__main__":
    main()
