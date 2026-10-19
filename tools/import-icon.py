"""Copy the development icon into tracked assets and generate Windows sizes.

Usage: python tools/import-icon.py [path/to/source.png]
Requires Pillow only when updating the icon, not for building the game.
"""

from pathlib import Path
import shutil
import sys

from PIL import Image


def main():
    root = Path(__file__).resolve().parents[1]
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "refs/DPTDOOM_ICON.png"
    destination = root / "resources"
    destination.mkdir(exist_ok=True)
    with Image.open(source) as image:
        icon = image.convert("RGBA")
        if icon.width != icon.height:
            raise ValueError("DPT.DOOM icon must be square")
        icon.save(destination / "dptdoom.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    if source.resolve() != (destination / "dptdoom.png").resolve():
        shutil.copyfile(source, destination / "dptdoom.png")
    print("Updated resources/dptdoom.png and resources/dptdoom.ico")


if __name__ == "__main__":
    main()
