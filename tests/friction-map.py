"""Boom variable friction (linedef 223) in the real engine.

Three properties, in order of importance:

  1. Flagging a sector with the friction bit but giving it no linedef 223 must
     change nothing. Boom only honours the stored value on flagged sectors, and
     every sector starts at vanilla friction, so this must be a no-op - if it is
     not, the feature silently alters maps that merely use the flag.
  2. A real 223 line must change how the player moves. Without this the feature
     could be wired up and inert.
  3. The result must be reproducible run to run, because the friction values are
     derived from the map at level load and the simulation depends on them.

The observable is the player's position after a fixed run of walking, not the
state trace: the trace's archive hash includes sector->special, so merely setting
the friction flag changes it without changing the simulation. Comparing traces
would therefore "prove" a difference that is only a hash of the flag. The
arithmetic itself is covered by the data-free `friction` test; this one proves
the wiring.

Note what this test does and does not pin down. It proves the feature as a whole
changes movement, not each of its two halves separately: friction scales the
momentum in P_XYMovement and movefactor scales how hard the player pushes off in
P_Thrust, and disabling only one of them still leaves the positions different.
The direction of the effect (ice slides further, sludge less) is pinned by the
data-free test, which checks both branches of the formula.
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

TICS = 70


def movement_input(tics=TICS):
    """Walk forward for the whole run: friction is only observable in motion."""
    return b"".join(
        struct.pack(">bbhBBBB", 25, 0, 0, 0, 0, 0, 0)
        for _ in range(tics))


def run(root, name, extra):
    """Returns (x, y, leveltime) from the session report."""
    work = root / name
    work.mkdir()
    (work / "input.bin").write_bytes(movement_input())
    command = [exe, "-iwad", str(iwad), "-warp", "1", "-skill", "3",
               "-nomonsters", "-headless-input", "input.bin",
               "-headless-tics", str(TICS), "-headless-session-report"] + extra
    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=180)
    text = result.stdout + result.stderr
    assert result.returncode == 0, (result.returncode, text)
    match = re.search(r"Session report:.*x=(-?\d+) y=(-?\d+)", text)
    assert match, text
    return int(match.group(1)), int(match.group(2))


with tempfile.TemporaryDirectory(prefix="dptdoom-friction-") as temporary:
    root = Path(temporary)

    # Both maps are overlays, so the sky-forced ceilings are identical between
    # them and only the friction data differs.
    sky.overlay(iwad, root / "base.wad")
    base = run(root, "base", ["-file", str(root / "base.wad")])

    # Flagged sectors, no 223 line: friction stays at the vanilla value, so the
    # player must end up exactly where they did without the flag.
    sky.overlay(iwad, root / "flagged.wad", sector_special=0x100)
    flagged = run(root, "flagged", ["-file", str(root / "flagged.wad")])
    assert flagged == base, \
        ("the friction flag without a linedef 223 must not change movement",
         flagged, base)

    # Real 223 line, tag 0 so it reaches the sectors the player actually walks
    # on without depending on where MAP01's start happens to be. All sectors are
    # flagged, which is what a mapper does when using 223.
    sky.overlay_multi(iwad, root / "friction.wad", [223], tag=0, sector_special=0x100)
    with_friction = run(root, "friction", ["-file", str(root / "friction.wad")])
    assert with_friction != base, \
        ("a linedef 223 must change how the player moves, otherwise friction is inert",
         with_friction, base)

    # Reproducible: the values are derived from the map at load, so two runs of
    # the same map must agree exactly.
    repeat = run(root, "friction-repeat", ["-file", str(root / "friction.wad")])
    assert repeat == with_friction, ("friction run is not reproducible", repeat, with_friction)

    # The push bit is supported too now, so a sector flagged with it must stay
    # quiet. The point-source push/pull line (226) is what still gets reported,
    # and push-map covers that.
    sky.overlay(iwad, root / "push.wad", sector_special=0x200)
    work = root / "push"
    work.mkdir()
    result = subprocess.run([exe, "-iwad", str(iwad), "-warp", "1",
                             "-headless-tics", "5", "-file", str(root / "push.wad")],
                            cwd=work, capture_output=True, text=True, timeout=120)
    text = result.stdout + result.stderr
    assert "Map compatibility:" not in text, text

    print("Boom friction: flag alone is a no-op, 223 changes movement, reproducible, push bit quiet OK")
