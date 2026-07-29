#!/usr/bin/env python3
"""Checks that a built package actually contains a runnable application.

    check_package.py <build-dir>

CPack packages the install tree, so anything staged only in the build
directory is silently absent from the archive — which surfaces as a package
that unpacks cleanly and then refuses to start. This inspects the archive
itself rather than trusting the install rules.
"""

import fnmatch
import pathlib
import sys
import tarfile
import zipfile

# Every entry has to be matched by at least one path in the archive.
COMMON = [
    "*/bin/simupy-cli*",
    "*/share/simupy/examples/*.spy",
    "*/share/simupy/libraries/*.spylib",
    "*/share/simupy/firmware/*",
]

GUI = ["*/bin/simupy", "*/bin/simupy.exe"]

# Qt is bundled on Windows and depended on elsewhere, so it is only required
# here. The platform plugin matters as much as the libraries: without it Qt
# aborts at startup with "no Qt platform plugin could be initialized".
WINDOWS = [
    "*/bin/Qt6Core.dll",
    "*/bin/Qt6Widgets.dll",
    "*/bin/Qt6Charts.dll",
    "*/bin/platforms/qwindows.dll",
    "*/bin/python3*.dll",
]


def entries(archive):
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as handle:
            return handle.namelist()
    with tarfile.open(archive) as handle:
        return handle.getnames()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build = pathlib.Path(sys.argv[1])

    archives = sorted(build.glob("simupy-*.zip")) + sorted(build.glob("simupy-*.tar.gz"))
    if not archives:
        sys.exit(f"no package found in {build}")

    failures = []
    for archive in archives:
        names = entries(archive)
        required = list(COMMON)
        # One of the two names, depending on the platform; a headless build has
        # neither, and that is a configuration rather than a fault.
        if any(fnmatch.fnmatch(n, p) for n in names for p in GUI):
            if archive.suffix == ".zip" and any(n.endswith(".exe") for n in names):
                required += WINDOWS

        missing = [p for p in required
                   if not any(fnmatch.fnmatch(n, p) for n in names)]
        status = "ok" if not missing else "MISSING " + ", ".join(missing)
        print(f"{archive.name}: {len(names)} entries, {status}")

        # Printed only on failure, and only here: knowing a path is absent is
        # useless without knowing what took its place.
        if missing:
            print("  what the archive actually holds:")
            for name in sorted(names):
                print(f"    {name}")

        failures += [(archive.name, p) for p in missing]

    if failures:
        sys.exit(f"\n{len(failures)} required path(s) absent from the packages")


if __name__ == "__main__":
    main()
