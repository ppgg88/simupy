#!/usr/bin/env python3
"""Writes a release version into the files that carry one.

    stamp_version.py <version> [--date YYYY-MM-DD]

The tag is the authority for a release, but two files have to agree with it
before the packages are built: the Flatpak manifest, which passes the version
to CMake, and the AppStream data, which is what a software centre shows. Both
substitutions are checked, because a version that silently fails to apply
produces a package that looks right and lies about what it contains.
"""

import argparse
import datetime
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "packaging/flatpak/io.github.ppgg88.SimuPy.yml"
METAINFO = ROOT / "packaging/io.github.ppgg88.SimuPy.metainfo.xml"

def substitute(path, pattern, replacement):
    text = path.read_text(encoding="utf-8")
    patched, count = re.subn(pattern, replacement, text, count=1)
    if count != 1:
        sys.exit(f"{path}: nothing matched {pattern!r} — refusing to stamp")
    path.write_text(patched, encoding="utf-8")
    print(f"{path.relative_to(ROOT)}: stamped")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("version")
    parser.add_argument("--date", default=datetime.date.today().isoformat())
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9A-Za-z.+~-]+", args.version):
        sys.exit(f"implausible version: {args.version!r}")

    substitute(MANIFEST,
               r"-DSIMUPY_VERSION=\S+",
               f"-DSIMUPY_VERSION={args.version}")

    substitute(METAINFO,
               r'<release version="[^"]*" date="[^"]*"',
               f'<release version="{args.version}" date="{args.date}"')

if __name__ == "__main__":
    main()
