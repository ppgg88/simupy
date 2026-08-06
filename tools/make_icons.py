#!/usr/bin/env python3
"""Renders the application icon into the raster forms each platform needs.

    make_icons.py [--check]

The SVG in resources/icons is the source. Two things are generated beside it
and committed, because the machines that need them cannot make them:

* a Windows ``.ico``, which the executable carries as a Win32 resource and the
  NSIS installer shows — Windows has no notion of an icon theme, so nothing
  finds the SVG at runtime;
* a 256-pixel PNG, embedded in the binary as the fallback for
  ``QIcon::fromTheme``, which returns nothing outside a freedesktop desktop.

Needs Inkscape to rasterise and Pillow to assemble the ``.ico``. Neither is a
build dependency: run this when the SVG changes and commit the result, stamp
included. ``--check`` only compares the stamp against the SVG, so CI catches an
SVG edited without re-running this without needing a renderer -- and without
depending on two Inkscape versions rasterising identically, which they do not.
"""

import argparse
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / "resources/icons"
APP_ID = "io.github.ppgg88.SimuPy"

SOURCE = ICONS / f"{APP_ID}.svg"
ICO = ICONS / f"{APP_ID}.ico"
PNG = ICONS / f"{APP_ID}-256.png"
STAMP = ICONS / "generated.sha256"

# What Windows asks for, from the taskbar to the largest Explorer view.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def digest():
    return hashlib.sha256(SOURCE.read_bytes()).hexdigest()


def render(size, into):
    out = into / f"{size}.png"
    subprocess.run(
        ["inkscape", "--export-type=png", f"--export-filename={out}",
         "-w", str(size), "-h", str(size), str(SOURCE)],
        check=True, capture_output=True)
    return out


def blobs(frames, into, fmt):
    """The image payloads Pillow puts in an .ico, one per size, in size order.

    Reads them back out of a throwaway .ico rather than encoding a DIB by hand.
    """
    path = into / f"{fmt}.ico"
    frames[-1].save(path, format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1],
                    **({"bitmap_format": "bmp"} if fmt == "bmp" else {}))

    data = path.read_bytes()
    count = struct.unpack_from("<H", data, 4)[0]
    out = []
    for i in range(count):
        size, offset = struct.unpack_from("<II", data, 6 + 16 * i + 8)
        out.append(data[offset:offset + size])
    return out


def build(into):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is needed to assemble the .ico: pip install pillow")

    # One Inkscape render per size, not one render scaled down: hinting at
    # 16 pixels is the whole reason the small sizes are in there.
    frames = [Image.open(render(size, into)) for size in ICO_SIZES]

    # Windows reads PNG-compressed entries, but only since Vista and only
    # reliably at 256; every other size goes in as an uncompressed DIB, which
    # is what resource compilers and installer stubs have always understood.
    payloads = [png if size == 256 else bmp
                for size, bmp, png in zip(ICO_SIZES,
                                          blobs(frames, into, "bmp"),
                                          blobs(frames, into, "png"))]

    ico = into / ICO.name
    with ico.open("wb") as out:
        out.write(struct.pack("<HHH", 0, 1, len(payloads)))
        offset = 6 + 16 * len(payloads)
        for size, payload in zip(ICO_SIZES, payloads):
            # 0 means 256: the field is a single byte.
            out.write(struct.pack("<BBBBHHII", size % 256, size % 256,
                                  0, 0, 1, 32, len(payload), offset))
            offset += len(payload)
        for payload in payloads:
            out.write(payload)

    png = into / PNG.name
    shutil.copyfile(render(256, into), png)
    return ico, png


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed files are out of date")
    args = parser.parse_args()

    if not SOURCE.is_file():
        sys.exit(f"no such file: {SOURCE}")

    if args.check:
        missing = [t.name for t in (ICO, PNG, STAMP) if not t.is_file()]
        if missing:
            sys.exit("missing, run tools/make_icons.py: " + ", ".join(missing))
        if STAMP.read_text().split()[0] != digest():
            sys.exit(f"{SOURCE.name} changed since the icons were rendered: "
                     "re-run tools/make_icons.py and commit the result")
        print("icons are up to date")
        return

    if shutil.which("inkscape") is None:
        sys.exit("Inkscape is needed to rasterise the SVG")

    with tempfile.TemporaryDirectory() as scratch:
        ico, png = build(pathlib.Path(scratch))
        for made, target in ((ico, ICO), (png, PNG)):
            shutil.copyfile(made, target)
            print(f"{target.relative_to(ROOT)}: written")

    STAMP.write_text(f"{digest()}  {SOURCE.name}\n")
    print(f"{STAMP.relative_to(ROOT)}: written")


if __name__ == "__main__":
    main()
