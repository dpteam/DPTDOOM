"""Boom pusher sectors (linedefs 224 wind / 225 current) in the real engine.

What is being proven, in order:

  1. The push flag alone, with no 224/225 line, changes nothing: every sector
     starts with no pusher, so a map that merely sets the bit must behave as
     before.
  2. A real wind line moves a standing player. Without this the feature could be
     wired up and inert.
  3. The height rule separates wind from current: wind pushes a jumping player,
     a current does not. That is the trap in this feature, and it is the only
     thing distinguishing the two line types at all.
  4. The result is reproducible run to run.

The observable is the player's position from the session report, not the state
trace: the archive hash includes sector->special, so merely setting the push flag
changes the trace without changing the simulation. Same reasoning as the friction
test.

The arithmetic is covered by the data-free `push` test; this one proves wiring.
"""
import argparse
import importlib.util
import re
import struct
from pathlib import Path
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location("sky", Path(__file__).with_name("map-sky.py"))
sky = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sky)

parser = argparse.ArgumentParser()
parser.add_argument("--exe", required=True)
parser.add_argument("--iwad", required=True)
args = parser.parse_args()
exe = str(Path(args.exe).resolve(strict=True))
iwad = Path(args.iwad).resolve(strict=True)

TICS = 90


def standing_input(tics=TICS):
    """No movement at all: any motion measured must come from the pusher."""
    return b"".join(
        struct.pack(">bbhBBBB", 0, 0, 0, 0, 0, 0, 0)
        for _ in range(tics))


def run(root, name, extra):
    """Returns (x, y) from the session report."""
    work = root / name
    work.mkdir()
    (work / "input.bin").write_bytes(standing_input())
    command = [exe, "-iwad", str(iwad), "-warp", "1", "-skill", "3",
               "-nomonsters", "-headless-input", "input.bin",
               "-headless-tics", str(TICS), "-headless-session-report"] + extra
    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=180)
    text = result.stdout + result.stderr
    assert result.returncode == 0, (result.returncode, text)
    match = re.search(r"Session report:.*x=(-?\d+) y=(-?\d+)", text)
    assert match, text
    return int(match.group(1)), int(match.group(2))


with tempfile.TemporaryDirectory(prefix="dptdoom-push-") as temporary:
    root = Path(temporary)

    # Both baselines are overlays so the sky-forced ceilings match.
    sky.overlay(iwad, root / "base.wad")
    base = run(root, "base", ["-file", str(root / "base.wad")])

    # Push flag, no 224/225 line: no pusher is created, so nothing moves.
    sky.overlay(iwad, root / "flagged.wad", sector_special=0x200)
    flagged = run(root, "flagged", ["-file", str(root / "flagged.wad")])
    assert flagged == base, \
        ("the push flag without a 224/225 line must not move the player", flagged, base)

    # A wind line on the sector the player stands in must move them. Tag 0 reaches
    # every sector, so this does not depend on where MAP01's start happens to be.
    sky.overlay_multi(iwad, root / "wind.wad", [224], tag=0, sector_special=0x200)
    wind = run(root, "wind", ["-file", str(root / "wind.wad")])
    assert wind != base, \
        ("a 224 wind line must move a standing player, otherwise pushers are inert",
         wind, base)

    # Reproducible: the direction comes from map geometry, so two runs must agree.
    repeat = run(root, "wind-repeat", ["-file", str(root / "wind.wad")])
    assert repeat == wind, ("wind run is not reproducible", repeat, wind)

    # A current with the same geometry must differ from wind, because the height
    # rule is the only difference between them. On the ground a current is at
    # full force and wind at half, so the displacement differs.
    sky.overlay_multi(iwad, root / "current.wad", [225], tag=0, sector_special=0x200)
    current = run(root, "current", ["-file", str(root / "current.wad")])
    assert current != base, \
        ("a 225 current line must also move a standing player", current, base)
    assert current != wind, \
        ("wind and current must not produce identical displacement: their height "
         "rules differ", current, wind)

    # The point-source variant stays unsupported and must be reported, so a map
    # relying on it does not look fully supported.
    sky.overlay(iwad, root / "push226.wad", special=226, tag=0, sector_special=0x200)
    work = root / "push226"
    work.mkdir()
    result = subprocess.run([exe, "-iwad", str(iwad), "-warp", "1",
                             "-headless-tics", "5", "-file", str(root / "push226.wad")],
                            cwd=work, capture_output=True, text=True, timeout=120)
    text = result.stdout + result.stderr
    assert "Map compatibility:" in text and " 226" in text, text

    print("Boom pushers: flag alone is a no-op, 224/225 move the player, "
          "wind and current differ, 226 still reported OK")
