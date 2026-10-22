"""Boom scrollers (linedefs 250-253) in the real engine.

The observable for the flat scroll is the engine's own scroller report, not the
framebuffer: a ceiling scroll is invisible on a sky ceiling, because sky planes
are drawn by the sky code rather than as flats, so their offsets never reach the
screen. A pixel comparison would call that correct behaviour "inert". The
framebuffer is still checked for the floor case, where the effect is visible.

What is being proven:

  1. A floor scroll advances the floor's offsets and leaves the ceiling's alone.
  2. A ceiling scroll does the opposite - the two are independent.
  3. A carrying floor (252/253) drags a standing player, which is the only part of
     this family that is simulation rather than texture coordinates.
  4. No scroller means no drift: a map without 250-253 keeps zero offsets, so the
     feature cannot quietly affect every map.
  5. The two stateful families are still reported as unsupported.

The arithmetic is covered by the data-free `scroll` test.
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

TICS = 60


def standing_input(tics=TICS):
    """No movement: anything that moves must come from the scroller."""
    return b"".join(
        struct.pack(">bbhBBBB", 0, 0, 0, 0, 0, 0, 0)
        for _ in range(tics))


def run(root, name, extra):
    """Returns (pixels, position, scroller_offsets)."""
    work = root / name
    work.mkdir()
    (work / "input.bin").write_bytes(standing_input())
    command = [exe, "-iwad", str(iwad), "-warp", "1", "-skill", "3",
               "-nomonsters", "-headless-input", "input.bin",
               "-headless-tics", str(TICS), "-headless-session-report"] + extra
    result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=180)
    text = result.stdout + result.stderr
    assert result.returncode == 0, (result.returncode, text)

    pixels = re.search(r"rgb-fnv1a=([0-9a-f]+)", text)
    position = re.search(r"Session report:.*x=(-?\d+) y=(-?\d+)", text)
    scroller = re.search(r"Scroller report: floor_off=(-?\d+),(-?\d+) "
                         r"ceiling_off=(-?\d+),(-?\d+) carry=(-?\d+),(-?\d+)", text)
    assert pixels and position and scroller, text
    return (pixels.group(1), (int(position.group(1)), int(position.group(2))),
            tuple(int(scroller.group(i)) for i in range(1, 7)))


with tempfile.TemporaryDirectory(prefix="dptdoom-scroll-") as temporary:
    root = Path(temporary)

    sky.overlay(iwad, root / "base.wad")
    base_px, base_pos, base_off = run(root, "base", ["-file", str(root / "base.wad")])

    # No scroller, no drift. Without this a bug that scrolled everything would
    # still pass the positive checks below.
    assert base_off == (0, 0, 0, 0, 0, 0), ("a map without scrollers must not drift", base_off)

    # Floor scroll: floor offsets move, ceiling offsets stay put, and the picture
    # changes because the flat is actually drawn with the offset.
    sky.overlay_multi(iwad, root / "floor.wad", [251], tag=0)
    floor_px, floor_pos, floor_off = run(root, "floor", ["-file", str(root / "floor.wad")])
    assert floor_off[0] != 0 or floor_off[1] != 0, \
        ("a 251 floor scroll must advance the floor offsets", floor_off)
    assert floor_off[2] == 0 and floor_off[3] == 0, \
        ("a floor scroll must not touch the ceiling offsets", floor_off)
    assert floor_px != base_px, "a floor scroll must change the rendered image"
    # Purely visual: no carrying means the player stays put.
    assert floor_pos == base_pos, \
        ("a floor scroll must not move the player; only carrying does", floor_pos, base_pos)

    # Reproducible: the speed comes from map geometry.
    repeat_px, _, repeat_off = run(root, "floor-repeat", ["-file", str(root / "floor.wad")])
    assert repeat_off == floor_off and repeat_px == floor_px, \
        ("floor scroll is not reproducible", repeat_off, floor_off)

    # Ceiling scroll: the mirror image of the floor case.
    sky.overlay_multi(iwad, root / "ceiling.wad", [250], tag=0)
    ceiling_px, _, ceiling_off = run(root, "ceiling", ["-file", str(root / "ceiling.wad")])
    assert ceiling_off[2] != 0 or ceiling_off[3] != 0, \
        ("a 250 ceiling scroll must advance the ceiling offsets", ceiling_off)
    assert ceiling_off[0] == 0 and ceiling_off[1] == 0, \
        ("a ceiling scroll must not touch the floor offsets", ceiling_off)

    # Carrying floor: this one moves the player, and it is the only part of the
    # scroller family that is simulation rather than texture coordinates.
    sky.overlay_multi(iwad, root / "carry.wad", [253], tag=0)
    carry_px, carry_pos, carry_off = run(root, "carry", ["-file", str(root / "carry.wad")])
    assert carry_off[4] != 0 or carry_off[5] != 0, \
        ("a 253 carrying floor must have a carry velocity", carry_off)
    assert carry_off[0] != 0 or carry_off[1] != 0, \
        ("253 also scrolls the flat, not only carrying", carry_off)
    # The two must point opposite ways in x: the flat's texture coordinate moves
    # by -dx while things ride at +dx, which is Boom's convention and the reason
    # the picture and the objects agree visually. A test that only checked
    # "non-zero" would pass with the sign flipped.
    assert (carry_off[0] < 0) != (carry_off[4] < 0), \
        ("the carry velocity must oppose the flat offset in x", carry_off)
    assert carry_pos != base_pos, \
        ("a 253 carrying floor must drag a standing player", carry_pos, base_pos)

    # The two stateful families are still reported, so a map using them does not
    # look fully supported.
    for special in (215, 247):
        name = "stateful%d" % special
        sky.overlay_multi(iwad, root / (name + ".wad"), [special], tag=0)
        work = root / name
        work.mkdir()
        result = subprocess.run([exe, "-iwad", str(iwad), "-warp", "1",
                                 "-headless-tics", "5", "-file", str(root / (name + ".wad"))],
                                cwd=work, capture_output=True, text=True, timeout=120)
        text = result.stdout + result.stderr
        assert "Map compatibility:" in text and (" %d" % special) in text, text

    print("Boom scrollers: floor/ceiling offsets independent, no drift without a "
          "scroller, carrying moves the player, stateful families still reported OK")
