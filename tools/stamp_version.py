#!/usr/bin/env python3
"""Writes a release version into the files that carry one.

    stamp_version.py <version> [--date YYYY-MM-DD]

The tag is the authority for a release, but four files have to agree with it
before the packages are built:

* the Flatpak manifest, which passes the version to CMake;
* the AppStream data, which is what a software centre shows;
* ``CMakeLists.txt``, whose ``project(VERSION ...)`` is what every other build
  reports — the Windows and Debian packages among them, since they never see
  the Flatpak manifest;
* ``docs/conf.py``, which puts it on every page of the documentation.

Every substitution is checked, because a version that silently fails to apply
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
CMAKELISTS = ROOT / "CMakeLists.txt"
DOCS_CONF = ROOT / "docs/conf.py"

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

    # project(VERSION ...) takes only digits and dots, so "1.2.0-rc1" is
    # trimmed there. Checked before the first write, never half way.
    numeric = re.match(r"[0-9]+(\.[0-9]+)*", args.version)
    if not numeric:
        sys.exit(f"{args.version!r} has no numeric part for project(VERSION)")

    substitute(MANIFEST,
               r"-DSIMUPY_VERSION=\S+",
               f"-DSIMUPY_VERSION={args.version}")

    substitute(METAINFO,
               r'<release version="[^"]*" date="[^"]*"',
               f'<release version="{args.version}" date="{args.date}"')

    substitute(CMAKELISTS,
               r"project\(SimuPy VERSION [0-9.]+",
               f"project(SimuPy VERSION {numeric.group(0)}")

    substitute(DOCS_CONF,
               r'release = "[^"]*"',
               f'release = "{args.version}"')

if __name__ == "__main__":
    main()
