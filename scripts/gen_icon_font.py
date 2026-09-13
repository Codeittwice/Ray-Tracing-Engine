#!/usr/bin/env python3
"""Regenerates the embedded icon font from Font Awesome 6 Free Solid.

The app draws its icons from a *subset* of Font Awesome compiled straight into the binary.
Two deliberate choices:

  * Subset, not the whole face. The full fa-solid-900.ttf is 410 KB; the ~50 glyphs this
    interface actually uses are 10 KB. Everything below has to be reviewed by a human at some
    point, and a 2 MB generated source file is not reviewable.
  * Embedded, not shipped as a file. Twelve example scenes once failed in every packaged build
    because package.ps1 never copied the meshes they referenced. An asset that cannot be left
    out of a package cannot be left out of a package.

Run this only when the icon list changes:

    python scripts/gen_icon_font.py

It needs network access (it fetches the upstream font and the codepoint table) and fonttools:

    python -m pip install fonttools

Outputs, both marked generated and both checked in:
    include/scrt/viz/Icons.hpp   - the ICON_FA_* string literals
    src/viz/IconFontData.cpp     - the subset TTF as a byte array

Licensing: the font file is SIL OFL 1.1, the icons themselves CC BY 4.0, both from
https://fontawesome.com. third_party/fontawesome/LICENSE.txt carries the notice, and the
attribution belongs in Help > About.
"""

from __future__ import annotations

import io
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

FA_VERSION = "6.5.2"
FONT_URL = f"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/{FA_VERSION}/webfonts/fa-solid-900.ttf"
HEADER_URL = "https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/main/IconsFontAwesome6.h"

ROOT = Path(__file__).resolve().parent.parent

# The icon set, grouped by where it is used. Keep this list honest: an icon that nothing draws
# is 200 bytes of binary and one more thing to explain.
ICONS: dict[str, list[str]] = {
    "Top bar and chrome": [
        "SUN", "MOON", "GEAR", "USER", "CIRCLE_INFO", "TRIANGLE_EXCLAMATION",
        "CHEVRON_RIGHT", "CHEVRON_DOWN", "XMARK", "CHECK", "TRASH",
    ],
    "Files": [
        "FILE", "FILE_IMPORT", "FILE_EXPORT", "FOLDER_OPEN", "FLOPPY_DISK",
        "ARROW_ROTATE_LEFT", "RIGHT_LEFT",
    ],
    "Scene tree": [
        "CUBE", "LAYER_GROUP", "SOLAR_PANEL", "MOUNTAIN_SUN", "BOWL_FOOD", "FIRE",
        "LIGHTBULB", "PALETTE", "SQUARE", "EYE", "EYE_SLASH",
    ],
    "Transform tools": [
        "ARROWS_UP_DOWN_LEFT_RIGHT",   # translate
        "ROTATE",                       # rotate
        "UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER",  # scale
        "LOCK", "LOCK_OPEN", "CROSSHAIRS", "SLIDERS", "EXPAND", "COMPRESS",
        "ARROWS_LEFT_RIGHT",
    ],
    "Trace and analysis": [
        "PLAY", "STOP", "CHART_SIMPLE", "CHART_AREA", "TEMPERATURE_HALF",
        "MAGNIFYING_GLASS", "DIAGRAM_PROJECT",
    ],
    "Assistant": [
        "WAND_MAGIC_SPARKLES",
    ],
}


def fetch(url: str) -> bytes:
    print(f"  fetching {url}")
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def codepoints(header: str) -> dict[str, int]:
    """Maps ICON_FA_NAME -> codepoint, from the IconFontCppHeaders table."""
    out: dict[str, int] = {}
    for name, cp in re.findall(r"^#define ICON_FA_(\S+) \".*\"\s*// U\+([0-9a-fA-F]+)", header, re.M):
        out[name] = int(cp, 16)
    return out


def utf8_escape(cp: int) -> str:
    """A codepoint as a C string literal of escaped UTF-8 bytes, as ImGui wants it."""
    return "".join(f"\\x{b:02x}" for b in chr(cp).encode("utf-8"))


def main() -> int:
    print("Font Awesome icon subset")

    ttf = fetch(FONT_URL)
    table = codepoints(fetch(HEADER_URL).decode("utf-8"))

    wanted: list[tuple[str, str, int]] = []   # (group, name, codepoint)
    for group, names in ICONS.items():
        for name in names:
            if name not in table:
                print(f"  ERROR: no such icon ICON_FA_{name} in Font Awesome {FA_VERSION}")
                return 1
            wanted.append((group, name, table[name]))

    cps = sorted({cp for _, _, cp in wanted})
    print(f"  {len(wanted)} icons, {len(cps)} distinct codepoints")

    # The merged range must not reach down into ASCII. ImGui merges by codepoint, so an icon
    # whose codepoint is a printable character (Font Awesome maps a few that way - "+" is
    # U+002B) would replace that character everywhere in the interface.
    if min(cps) < 0x2000:
        print(f"  ERROR: codepoint U+{min(cps):04x} collides with ordinary text")
        return 1

    src = ROOT / "build" / "_fa-solid-900.ttf"
    src.parent.mkdir(parents=True, exist_ok=True)
    src.write_bytes(ttf)
    dst = ROOT / "build" / "_fa-subset.ttf"

    subprocess.run(
        [sys.executable, "-m", "fontTools.subset", str(src),
         "--unicodes=" + ",".join(f"{cp:04x}" for cp in cps),
         f"--output-file={dst}", "--no-hinting", "--desubroutinize",
         "--name-IDs=", "--drop-tables+=DSIG"],
        check=True,
    )
    data = dst.read_bytes()
    print(f"  {len(ttf)} bytes -> {len(data)} bytes")

    # ---- Icons.hpp ----
    h = io.StringIO()
    h.write("// GENERATED by scripts/gen_icon_font.py - do not edit by hand.\n")
    h.write(f"// Font Awesome {FA_VERSION} Free Solid, subset to the glyphs this interface draws.\n")
    h.write("// Font: SIL OFL 1.1. Icons: CC BY 4.0. See third_party/fontawesome/LICENSE.txt.\n")
    h.write("#pragma once\n\n")
    h.write('#include "imgui.h"\n\n')
    h.write("namespace scrt::viz {\n\n")
    h.write("/// The subset TTF, compiled in. Not a file on disk: an embedded asset cannot be\n")
    h.write("/// left out of a package the way the example meshes once were.\n")
    h.write("extern const unsigned char kIconFontTTF[];\n")
    h.write("/// Length of kIconFontTTF in bytes.\n")
    h.write("extern const unsigned int kIconFontTTFLen;\n\n")
    h.write("/// Inclusive codepoint range of the subset, for ImFontConfig::GlyphRanges.\n")
    h.write(f"constexpr ImWchar kIconMin = 0x{min(cps):04x};\n")
    h.write(f"constexpr ImWchar kIconMax = 0x{max(cps):04x};\n\n")
    h.write("} // namespace scrt::viz\n\n")
    h.write("// Icon literals. Each is a UTF-8 string, so it concatenates with ordinary text:\n")
    h.write('//     ImGui::Button(ICON_FA_PLAY "  Trace")\n')
    last_group = None
    for group, name, cp in wanted:
        if group != last_group:
            h.write(f"\n// {group}\n")
            last_group = group
        h.write(f'#define ICON_FA_{name} "{utf8_escape(cp)}"  // U+{cp:04x}\n')
    (ROOT / "include" / "scrt" / "viz" / "Icons.hpp").write_text(h.getvalue(), encoding="utf-8")

    # ---- IconFontData.cpp ----
    c = io.StringIO()
    c.write("// GENERATED by scripts/gen_icon_font.py - do not edit by hand.\n")
    c.write(f"// Font Awesome {FA_VERSION} Free Solid, subset to {len(cps)} glyphs ({len(data)} bytes).\n")
    c.write("// Font: SIL OFL 1.1. Icons: CC BY 4.0. See third_party/fontawesome/LICENSE.txt.\n")
    c.write('#include "scrt/viz/Icons.hpp"\n\n')
    c.write("namespace scrt::viz {\n\n")
    c.write(f"const unsigned int kIconFontTTFLen = {len(data)};\n\n")
    c.write("const unsigned char kIconFontTTF[] = {\n")
    for i in range(0, len(data), 20):
        c.write("    " + "".join(f"0x{b:02x}," for b in data[i:i + 20]) + "\n")
    c.write("};\n\n} // namespace scrt::viz\n")
    (ROOT / "src" / "viz" / "IconFontData.cpp").write_text(c.getvalue(), encoding="utf-8")

    src.unlink(missing_ok=True)
    dst.unlink(missing_ok=True)
    print("  wrote include/scrt/viz/Icons.hpp and src/viz/IconFontData.cpp")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
