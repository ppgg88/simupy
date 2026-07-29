#!/usr/bin/env python3
"""Turns a text file into a C++ translation unit defining a string constant.

    embed_text.py <input> <output.cpp> <namespace> <symbol>

One string literal per line, because MSVC caps a single literal at 16380
bytes. The concatenation of adjacent literals has its own ceiling of 65535, so
the total is checked here and reported plainly rather than left to surface as
a compiler error nobody can read.
"""

import sys

MSVC_TOTAL_LIMIT = 65535
HEADROOM = 4096

ESCAPES = {
    ord("\\"): "\\\\",
    ord('"'): '\\"',
    ord("\n"): "\\n",
    ord("\r"): "",
    ord("\t"): "\\t",
}


def escape(line):
    """Escapes one line's UTF-8 bytes into a pure-ASCII C++ literal.

    Non-ASCII goes out as three-digit octal rather than raw bytes: MSVC reads
    a UTF-8 source without a BOM in the system code page, which would mangle
    every accented character in a docstring. Octal, not hex, because a hex
    escape swallows as many digits as follow it.
    """
    out = []
    for byte in line.encode("utf-8"):
        if byte in ESCAPES:
            out.append(ESCAPES[byte])
        elif 0x20 <= byte < 0x7F:
            out.append(chr(byte))
        else:
            out.append(f"\\{byte:03o}")
    return "".join(out)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    source, target, namespace, symbol = sys.argv[1:]

    with open(source, encoding="utf-8") as handle:
        text = handle.read()

    encoded = text.encode("utf-8")
    if len(encoded) > MSVC_TOTAL_LIMIT - HEADROOM:
        sys.exit(
            f"{source} is {len(encoded)} bytes, too close to the {MSVC_TOTAL_LIMIT}"
            " byte ceiling on concatenated string literals.\n"
            "Split it across two embedded constants, or switch this generator"
            " to emit a char array."
        )

    lines = text.split("\n")
    body = "\n".join(f'    "{escape(line)}\\n"' for line in lines)

    with open(target, "w", encoding="utf-8") as handle:
        handle.write(
            f"// Generated from {source}. Do not edit.\n\n"
            f"namespace {namespace} {{\n\n"
            f"extern const char* const {symbol};\n"
            f"const char* const {symbol} =\n{body};\n\n"
            f"}}  // namespace {namespace}\n"
        )


if __name__ == "__main__":
    main()
