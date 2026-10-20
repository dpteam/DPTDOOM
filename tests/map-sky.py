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


def overlay(iwad, path, special=0, tag=0):
    """Return (sector_count, overlay_path).

    special=0 builds the sky-ceiling baseline without any transfer.
    """
    entries = read_entries(iwad.read_bytes())
    index = next(i for i, (name, _) in enumerate(entries) if name == b"MAP01")
    level = entries[index:index + 11]
    lumps = dict(level)

    sidedefs = bytearray(lumps[SIDEDEFS])
    linedefs = bytearray(lumps[LINEDEFS])
    sectors = bytearray(lumps[SECTORS])

    # Force sky ceilings so the transferred texture is on screen immediately.
    for off in range(0, len(sectors), 26):
        sectors[off + 12:off + 20] = b"F_SKY1\0\0"

    if special:
        # Reuse an existing wall texture name, guaranteed to be present.
        texture = b"STARTAN3"
        for off in range(0, len(sidedefs), 30):
            middle = bytes(sidedefs[off + 20:off + 28]).rstrip(b"\0")
            if middle:
                texture = middle.ljust(8, b"\0")
                break

        # Prefer a one-sided line: its top texture is never drawn as a wall.
        chosen = None
        for i in range(len(linedefs) // 14):
            off = i * 14
            side0 = struct.unpack_from("<h", linedefs, off + 10)[0]
            side1 = struct.unpack_from("<h", linedefs, off + 12)[0]
            if side0 >= 0 and side1 < 0:
                chosen = (off, side0)
                break
        assert chosen is not None, "no one-sided line in MAP01"
        line_off, side = chosen
        struct.pack_into("<h", linedefs, line_off + 6, special)
        struct.pack_into("<h", linedefs, line_off + 8, tag)
        sidedefs[side * 30 + 4:side * 30 + 12] = texture
        if tag:
            # Unique tag on the first sector, so 272 hits exactly one sector.
            struct.pack_into("<h", sectors, 24, tag)

    lumps[SECTORS] = bytes(sectors)
    lumps[SIDEDEFS] = bytes(sidedefs)
    lumps[LINEDEFS] = bytes(linedefs)
    write_wad(path, [(name, lumps[name]) for name, _ in level])
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

        # Unimplemented Boom/MBF extended specials must be reported, not
        # silently inert. 142 is the first Boom extended type; vanilla Doom
        # uses 1..141.
        overlay(iwad, root / "unsupported.wad", 142)
        _, text = run("unsupported", ["-file", str(root / "unsupported.wad")])
        assert "Map compatibility:" in text and " 142" in text, text
        assert "Map sky: count=0" in text, text
        # The baseline overlay has no 142+ specials and must stay quiet, even
        # though stock Doom II MAP01 does use vanilla types 102 and 103.
        assert "Map compatibility:" not in run("base-quiet", ["-file", str(root / "sky-base.wad")])[1]
        assert "Map compatibility:" not in run("vanilla-quiet", [])[1]

        # Note: a transfer line whose sidedef has neither upper nor lower
        # texture is ignored defensively. Vanilla MAP01 has no such two-sided
        # line, and an empty sidedef texture name is itself an invalid map, so
        # this path is documented rather than fabricated here.

        print("Map sky: 271 all sectors, 272 tagged sector, repeatable pixels OK")


if __name__ == "__main__":
    main()
