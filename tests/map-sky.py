"""Boom/MBF sky transfer (linedef specials 271/272) with a local IWAD.

Builds temporary MAP01 overlays in the test's own directory. Every sector's
ceiling flat is set to F_SKY1 so the sky is guaranteed to be on screen at the
player start; the transfer then changes which *texture* that sky uses. A
one-sided line gets the special and its front sidedef's top texture is pointed
at an existing wall texture; for a one-sided line the top texture is never
drawn as a wall, so a framebuffer difference comes from the sky alone.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

SECTORS = b"SECTORS"
SIDEDEFS = b"SIDEDEFS"
LINEDEFS = b"LINEDEFS"


def read_entries(data):
    magic, count, directory = struct.unpack_from("<4sii", data)
    assert magic == b"IWAD"
    entries = []
    for i in range(count):
        start, length, name = struct.unpack_from("<ii8s", data, directory + i * 16)
        entries.append((name.rstrip(b"\0"), data[start:start + length]))
    return entries


# Parsing the 15 MB IWAD for every overlay was a large share of this test's
# runtime (it builds dozens of maps). The file never changes during a run.
_ENTRY_CACHE = {}


def cached_entries(iwad):
    key = str(iwad)
    if key not in _ENTRY_CACHE:
        _ENTRY_CACHE[key] = read_entries(iwad.read_bytes())
    return _ENTRY_CACHE[key]


def map01_lumps(iwad):
    """Return (level_entry_names, {name: bytearray}) for MAP01, already sky-forced."""
    entries = cached_entries(iwad)
    index = next(i for i, (name, _) in enumerate(entries) if name == b"MAP01")
    level = entries[index:index + 11]
    lumps = {name: bytearray(data) for name, data in level}

    # Force sky ceilings so a transferred texture is on screen immediately.
    sectors = lumps[SECTORS]
    for off in range(0, len(sectors), 26):
        sectors[off + 12:off + 20] = b"F_SKY1\0\0"
    return [name for name, _ in level], lumps


def one_sided_lines(linedefs):
    """Yield (offset, front_sidedef) for every one-sided line."""
    for i in range(len(linedefs) // 14):
        off = i * 14
        side0 = struct.unpack_from("<h", linedefs, off + 10)[0]
        side1 = struct.unpack_from("<h", linedefs, off + 12)[0]
        if side0 >= 0 and side1 < 0:
            yield off, side0


def write_level(path, names, lumps):
    write_wad(path, [(name, bytes(lumps[name])) for name in names])


def write_wad(path, entries):
    body = bytearray(b"PWAD" + struct.pack("<ii", len(entries), 0))
    records = []
    for name, data in entries:
        records.append((len(body), len(data), name.ljust(8, b"\0")))
        body.extend(data)
    struct.pack_into("<i", body, 8, len(body))
    for record in records:
        body.extend(struct.pack("<ii8s", *record))
    path.write_bytes(body)


def overlay(iwad, path, special=0, tag=0, sector_special=0):
    """Return (sector_count, overlay_path).

    special=0 builds the sky-ceiling baseline without any transfer.
    sector_special sets the BOOM/generalized sector special on every sector.
    """
    return overlay_multi(iwad, path, [special] if special else [],
                         tag=tag, sector_special=sector_special)


def overlay_multi(iwad, path, specials, tag=0, sector_special=0):
    """Put one special on each of several one-sided lines.

    Consolidating many specials into one map turns dozens of engine launches
    into one. The engine reports unsupported specials as a single list, so a
    caller can assert on the set instead of running the engine per special.
    """
    names, lumps = map01_lumps(iwad)
    sidedefs = lumps[SIDEDEFS]
    linedefs = lumps[LINEDEFS]
    sectors = lumps[SECTORS]

    if sector_special:
        for off in range(0, len(sectors), 26):
            struct.pack_into("<h", sectors, off + 22, sector_special)

    if specials:
        # Reuse an existing wall texture name, guaranteed to be present.
        texture = b"STARTAN3"
        for off in range(0, len(sidedefs), 30):
            middle = bytes(sidedefs[off + 20:off + 28]).rstrip(b"\0")
            if middle:
                texture = middle.ljust(8, b"\0")
                break

        lines = list(one_sided_lines(linedefs))
        assert len(lines) >= len(specials), "not enough one-sided lines in MAP01"
        for (line_off, side), special in zip(lines, specials):
            struct.pack_into("<h", linedefs, line_off + 6, special)
            struct.pack_into("<h", linedefs, line_off + 8, tag)
            # Top texture of a one-sided line is never drawn as a wall, so a
            # framebuffer difference comes from the sky alone.
            sidedefs[side * 30 + 4:side * 30 + 12] = texture

        if tag:
            # Unique tag on the first sector, so 272 hits exactly one sector.
            struct.pack_into("<h", sectors, 24, tag)

    write_level(path, names, lumps)
    return len(sectors) // 26


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    args = parser.parse_args()
    exe, iwad = args.exe.resolve(strict=True), args.iwad.resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="dptdoom-sky-") as temporary:
        root = Path(temporary)
        base = [str(exe), "-iwad", str(iwad), "-warp", "1", "-nomonsters",
                "-headless-tics", "5", "-headless-session-report"]

        def run(name, extra, ok=True, dump=None):
            work = root / name
            work.mkdir()
            command = base + extra
            if dump:
                command += ["-headless-dump", dump]
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=30)
            text = result.stdout + result.stderr
            assert "AddressSanitizer" not in text and "runtime error:" not in text, text
            assert (result.returncode == 0) == ok, (result.returncode, text)
            return work, text

        # Untouched map: no transfer, and the vanilla sky is in use.
        _, text = run("vanilla", [])
        assert "Map sky: count=0" in text, text

        # Baseline overlay: sky ceilings, but no transfer.
        sectors = overlay(iwad, root / "sky-base.wad")
        baseline, text = run("base", ["-file", str(root / "sky-base.wad"), "-headless-dump", "frame.ppm"])
        assert "Map sky: count=0" in text, text
        baseline_pixels = (root / "base" / "frame.ppm").read_bytes()

        # 271: transfer to every sector.
        overlay(iwad, root / "sky271.wad", 271)
        _, text = run("sky271", ["-file", str(root / "sky271.wad"), "-headless-dump", "frame.ppm"])
        assert "Map sky: count=%d" % sectors in text, text
        sky_pixels = (root / "sky271" / "frame.ppm").read_bytes()
        assert sky_pixels != baseline_pixels, "sky transfer did not change the framebuffer"

        # Same map twice must render identically.
        _, text = run("sky271-repeat", ["-file", str(root / "sky271.wad"), "-headless-dump", "frame.ppm"])
        assert (root / "sky271-repeat" / "frame.ppm").read_bytes() == sky_pixels

        # 272: only the sector with the matching tag.
        overlay(iwad, root / "sky272.wad", 272, 999)
        _, text = run("sky272", ["-file", str(root / "sky272.wad")])
        assert "Map sky: count=1 first=0:" in text, text

        # Unimplemented Boom/MBF specials must be reported, not silently inert.
        # 142+ is the main Boom extended range; 78 and 85 are Boom types placed
        # inside the vanilla 1..141 range (vanilla never uses those numbers).
        for name, special in (("unsupported-142", 142), ("unsupported-78", 78),
                              ("unsupported-85", 85)):
            overlay(iwad, root / (name + ".wad"), special)
            _, text = run(name, ["-file", str(root / (name + ".wad"))])
            assert "Map compatibility:" in text and (" %d" % special) in text, text
            assert "Map sky: count=0" in text, text
        # Classification checks are consolidated into one map per outcome. Each
        # special used to get its own engine launch, which made this test the
        # slowest in the suite (~42 launches, each reloading the IWAD). The
        # engine reports unsupported specials as a single list, so the same
        # assertions hold against one run. Exhaustive per-value decoding lives
        # in the data-free gen*/secspecial unit tests; what this test proves is
        # the engine's report wiring, which one representative map covers.
        supported = (
            # Boom generalized doors: all eight trigger classes.
            [0x3c00 + t for t in range(8)]
            # Floors: every target, both directions, including the texture-based
            # one and the relative 24/32 heights.
            + [0x6000, 0x6002, 0x6004, 0x6006, 0x6007,
               0x6080, 0x6100, 0x6180, 0x6200, 0x6300, 0x6380, 0x6280,
               0x6040, 0x60c0, 0x6140, 0x61c0, 0x6240, 0x6340, 0x7140,
               0x6400, 0x6800, 0x6c00, 0x6c20,
               # Ceilings: the same, plus next-neighbour, shortest upper texture
               # and crush while lowering.
               0x4000, 0x4080, 0x40c0, 0x4100, 0x4180, 0x4200, 0x4240, 0x4280,
               0x4300, 0x4380, 0x4040, 0x4042, 0x4046, 0x4140, 0x4280, 0x5200,
               0x4400, 0x4800, 0x4c00, 0x4c20,
               # Crushers, stairs, lifts (all three supported targets), locked
               # doors, including push (P1/PR) which acts on the sector behind
               # the line.
               0x2f80, 0x2f82, 0x2fd8, 0x2f86, 0x2f87,
               0x3140, 0x3142, 0x3144, 0x3146,
               0x3400, 0x3402, 0x3404, 0x3406, 0x3500, 0x3600,
               0x3800, 0x3840, 0x3880, 0x39c0, 0x3806]
        )
        overlay_multi(iwad, root / "supported.wad", supported)
        _, text = run("supported", ["-file", str(root / "supported.wad")])
        assert "Map compatibility:" not in text, text

        # Unsupported: 142+ is the main Boom extended range; 78 and 85 are Boom
        # types placed inside the vanilla 1..141 range (vanilla never uses those
        # numbers). The rest are the two remaining combinations: crush in the
        # direction where it has no meaning, and the perpetual lift.
        unsupported = [142, 78, 85,
                       0x7000,   # floor: crush while lowering
                       0x4040 | 0x1000,  # ceiling: crush while raising
                       0x3700]   # lift: perpetual lowest<->highest
        overlay_multi(iwad, root / "unsupported.wad", unsupported)
        _, text = run("unsupported", ["-file", str(root / "unsupported.wad")])
        assert "Map compatibility:" in text, text
        assert "linedef special(s)" in text, text
        for special in unsupported:
            assert (" %d" % special) in text, (special, text)
        assert "Map sky: count=0" in text, text

        # Sector specials are global to the map, so they cannot be batched onto
        # one map. One supported value (base + damage + secret together) and two
        # rejection paths (friction needs a thinker we lack; bits above 0x03ff
        # are undefined by BOOM) cover the wiring.
        overlay(iwad, root / "sec-supported.wad", sector_special=0xE1)
        _, text = run("sec-supported", ["-file", str(root / "sec-supported.wad")])
        assert "Map compatibility:" not in text, text

        # Friction is supported now, so a sector flagged with it must stay quiet.
        overlay(iwad, root / "sec-friction.wad", sector_special=0x100)
        _, text = run("sec-friction", ["-file", str(root / "sec-friction.wad")])
        assert "Map compatibility:" not in text, text

        for name, value in (("sec-push", 0x200), ("sec-undefined", 0x400)):
            overlay(iwad, root / (name + ".wad"), sector_special=value)
            _, text = run(name, ["-file", str(root / (name + ".wad"))])
            assert "Map compatibility:" in text and " sector special(s)" in text, text
            assert (" %d" % value) in text, text

        # The baseline overlay has no Boom specials and must stay quiet, even
        # though stock Doom II MAP01 does use vanilla types 102 and 103.
        assert "Map compatibility:" not in run("base-quiet", ["-file", str(root / "sky-base.wad")])[1]
        assert "Map compatibility:" not in run("vanilla-quiet", [])[1]

        # Damage must actually be applied, and the vanilla table must keep its
        # own numbers. The vanilla sector special 5 deals 10 damage while its
        # damage bit field is 0, so a single shared table would be wrong either
        # way round. The damage tick fires at leveltime 0, 32, 64, ...; 40 tics
        # covers two of them, which is enough to tell the tiers apart while
        # keeping the ASan run inside its timeout.
        def health(name, value, tics=40):
            path = root / (name + ".wad")
            overlay(iwad, path, sector_special=value)
            work = root / name
            work.mkdir()
            result = subprocess.run(
                [str(exe), "-iwad", str(iwad), "-warp", "1", "-nomonsters",
                 "-headless-tics", str(tics), "-headless-session-report",
                 "-file", str(path)],
                cwd=work, capture_output=True, text=True, timeout=30)
            text = result.stdout + result.stderr
            assert result.returncode == 0, text
            assert "AddressSanitizer" not in text and "runtime error:" not in text, text
            report = [l for l in result.stdout.splitlines()
                      if l.startswith("Session report")]
            assert report, text
            return int(report[0].split("health=")[1].split()[0])

        assert health("hp-none", 0) == 100
        # Vanilla tiers: 5 -> 10, 7 -> 5, 16 -> 20 damage per damage tick.
        assert health("hp-vanilla-5", 5) == 100 - 2 * 10
        assert health("hp-vanilla-7", 7) == 100 - 2 * 5
        assert health("hp-vanilla-16", 16) == 100 - 2 * 20
        # Generalized tiers must reproduce the same numbers through the bit
        # fields, which is the point of the shared model. gen-dmg3 also covers
        # the light-spawner interaction: base 1 is a flickering light, so if the
        # light thinker cleared sector->special the damage would vanish.
        assert health("hp-gen-dmg1", 0x01 | (1 << 5)) == 100 - 2 * 5
        assert health("hp-gen-dmg2", 0x01 | (2 << 5)) == 100 - 2 * 10
        assert health("hp-gen-dmg3", 0x01 | (3 << 5)) == 100 - 2 * 20
        # A generalized secret sector must not hurt by itself.
        assert health("hp-gen-secret", 0x80 | 5) == 100

        # Note: a transfer line whose sidedef has neither upper nor lower
        # texture is ignored defensively. Vanilla MAP01 has no such two-sided
        # line, and an empty sidedef texture name is itself an invalid map, so
        # this path is documented rather than fabricated here.

        print("Map sky: 271 all sectors, 272 tagged sector, repeatable pixels OK")


if __name__ == "__main__":
    main()
