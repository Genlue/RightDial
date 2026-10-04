#!/usr/bin/env python3
"""Generate src/lucide_icons.cpp from a folder of Lucide SVG files.

Usage:
    python tools/gen_lucide.py <dir-with-lucide-svgs>

The folder is the unzipped lucide.dev SVG download (files like
"activity.svg" plus optional "<name>-<12hex>.svg" duplicates; only the
plain names are embedded). The output is a compact, sorted data table of
the *inner* SVG markup of every icon. The per-theme <svg> wrapper
(stroke color/width) is applied at render time in render.cpp, so the
icons re-tint with the dark/light palette like the other builtin glyphs.

The generated file is committed to the repo; the build does not need
Python. Re-run this script only when updating the icon set.
"""

import re
import sys
import glob
import os

HEADER = """// generated file - DO NOT EDIT (regenerate with: python tools/gen_lucide.py <svg-dir>)
//
// Embeds the Lucide icon set (https://lucide.dev), {count} icons,
// downloaded {date}. Each entry stores only the inner markup of a 24x24
// viewBox="0 0 24 24" fill="none" stroke-width="2" round-cap/join SVG;
// render.cpp wraps it with the theme-appropriate stroke color before
// rasterizing (nanosvg). Icon bodies keep "currentColor" verbatim where
// the original files use it; the wrapper substitutes the theme ink.
//
// Lucide icons - ISC License
// Copyright (c) for portions of Lucide are held by Bricke du Blois, 2023.
// All other copyright (c) for Lucide are held by Lucide Contributors.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "lucide_icons.h"

"""


def minify_body(body: str) -> str:
    body = re.sub(r">\s+<", "><", body)
    body = re.sub(r"\s+", " ", body).strip()
    body = re.sub(r"\s*=\s*", "=", body)
    return body


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    src = sys.argv[1]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       os.pardir, "src", "lucide_icons.cpp")
    out = os.path.normpath(out)

    files = [f for f in glob.glob(os.path.join(src, "*.svg"))
             if not re.search(r"-[0-9a-f]{12}\.svg$", os.path.basename(f))]
    if not files:
        print(f"[!] no SVG files found in {src}")
        return 1

    icons = {}
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        text = open(f, encoding="utf-8").read()
        m = re.match(r"\s*<svg([^>]*)>(.*)</svg>\s*$", text, re.S)
        if not m:
            print(f"[!] unparseable: {f}")
            return 1
        root = re.sub(r"\s+", " ", m.group(1)).strip()
        expected = ('xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
                    'viewBox="0 0 24 24" fill="none" stroke="currentColor" '
                    'stroke-width="2" stroke-linecap="round" stroke-linejoin="round"')
        if root != expected:
            print(f"[!] unexpected root tag in {f}:\n    {root}")
            return 1
        body = minify_body(m.group(2))
        # C++ string literal: Lucide markup is pure ASCII markup, apostrophe
        # attributes are safe, only the double quotes need escaping.
        body = body.replace('"', '\\"')
        icons[name] = body

    names = sorted(icons)
    lines = []
    lines.append("static const LucideIcon kLucideIcons[] = {")
    for name in names:
        lines.append(f'    {{"{name}", "{icons[name]}"}},')
    lines.append("};")
    lines.append("")
    lines.append("const LucideIcon* LucideFindIcon(const char* name) {")
    lines.append("    int lo = 0, hi = (int)(sizeof(kLucideIcons) / sizeof(kLucideIcons[0])) - 1;")
    lines.append("    while (lo <= hi) {")
    lines.append("        int mid = (lo + hi) / 2;")
    lines.append("        int c = strcmp(name, kLucideIcons[mid].name);")
    lines.append("        if (c == 0) return &kLucideIcons[mid];")
    lines.append("        if (c < 0) hi = mid - 1; else lo = mid + 1;")
    lines.append("    }")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("int LucideIconCount() { return (int)(sizeof(kLucideIcons) / sizeof(kLucideIcons[0])); }")
    lines.append("const LucideIcon& LucideIconAt(int i) { return kLucideIcons[i]; }")
    lines.append("")

    date = "2026-06-30"  # stated in the download's catalog markdown
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(HEADER.format(count=len(names), date=date))
        fh.write("\n".join(lines))

    total = sum(len(v) for v in icons.values())
    print(f"OK: {len(names)} icons, {total} bytes of bodies -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
