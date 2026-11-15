#!/usr/bin/env python3
"""Pack the sixteen demo typeface atlases in native/ui/demo_fonts.h to 1 bit per pixel.

The atlases are pure two-level masks (every value 0 or 255), so one bit per pixel loses nothing:
the packed header renders pixel-identically (ptfont-atlas-check holds the render hashes) and is
eight times smaller.

Input: the byte-per-pixel header as font-authoring/generate_atlases.py writes it (arrays
demoGrid2/demoGrid3/demoLabel12/demoLabel18 of uint8_t 0/255). Output: the complete 1-bit header —
each array replaced by "<name>Bits" (bit i of the original array at byte i>>3, bit (i&7), LSB
first), and the size total, layout checks and readers after the arrays by their 1-bit versions —
which compiles as it is and equals the shipped native/ui/demo_fonts.h byte for byte.

    python3 tools/fonts/pack_demo_atlases.py native/ui/demo_fonts.h   # rewrites it in place

Only the generator's exact output is accepted: its fixed text around the four arrays, and the
arrays in canonical form (32 values a line, each exactly "0" or "255"). Any other spelling, value,
comment, line break or order is refused before anything is written, as is an already packed header.
"""
import re
import sys

NAMES = ["demoGrid2", "demoGrid3", "demoLabel12", "demoLabel18"]

# The 1-bit readers written after the arrays (BIT_TRAILER) index exactly this layout: 16 faces;
# grid glyphs of 95 characters × 110 (scale 2) and × 255 (scale 3) pixels; label atlases the size
# of font_ui.h's. Any other shape would be read past its end, so it is refused.
EXPECTED_SHAPE = {
    "demoGrid2": (16, 95 * 110),
    "demoGrid3": (16, 95 * 255),
    "demoLabel12": (16, 17108),   # sizeof(uiFont12Pixels)
    "demoLabel18": (16, 36981),   # sizeof(uiFont18Pixels)
}

# The generator's byte header around its four arrays, exactly. The input must be exactly this, with
# the arrays in their canonical form between (rendered from the values read): anything else — a
# comment, a line break, a moved marker, another value spelling — is not the generator's output,
# and the packer refuses it with the file untouched instead of guessing at its structure.
BYTE_PREFIX = '// SPDX-License-Identifier: GPL-3.0-or-later\n// Original project pixel alphabets, Kid Bunka / project contributors.\n// Generated from original pixel-masters.json and clean-masters.json.\n// Reproduce with font-authoring/generate_atlases.py (runs clean-generate.py).\n// Existing font_ui supplies metrics only. No font/ROM raster has been copied.\n// Static alpha atlases: no allocations, timers, drawing or audio-thread work.\n#pragma once\n#include <cstdint>\n#include "font_ui.h"\nnamespace pt::ui::detail {\n'
BYTE_SUFFIX = '\ninline constexpr unsigned demoFontAtlasBytes = sizeof(demoGrid2)+sizeof(demoGrid3)+sizeof(demoLabel12)+sizeof(demoLabel18);\nstatic_assert(demoFontAtlasBytes < 1536u*1024u, "Keep all sixteen typefaces below 1.5 MiB");\nstatic_assert(sizeof(demoLabel12[0])==sizeof(uiFont12Pixels) && sizeof(demoLabel18[0])==sizeof(uiFont18Pixels), "Label atlas layout changed");\ninline const uint8_t* demo_grid_pixels(int style,int font_scale,uint32_t cp) {\n    if(style<1 || style>16 || cp<32 || cp>126)return nullptr;\n    if(font_scale==2)return demoGrid2[style-1]+(cp-32)*110u;\n    if(font_scale==3)return demoGrid3[style-1]+(cp-32)*255u;\n    return nullptr;\n}\ninline const uint8_t* demo_label_pixels(int style,int size) {\n    if(style<1 || style>16)return nullptr;\n    return size>=15?demoLabel18[style-1]:demoLabel12[style-1];\n}\n} // namespace pt::ui::detail\n'

# Everything after the arrays that reads them, as the 1-bit header needs it: the size total, the
# layout checks and the bit readers. It replaces the byte header's own (which names the byte
# arrays), so the packed header compiles as it is — byte for byte the shipped native/ui/demo_fonts.h.
BYTE_TRAILER_START = "inline constexpr unsigned demoFontAtlasBytes"
TRAILER_END = "} // namespace pt::ui::detail"
BIT_TRAILER = 'inline constexpr unsigned demoFontAtlasBytes = sizeof(demoGrid2Bits)+sizeof(demoGrid3Bits)+sizeof(demoLabel12Bits)+sizeof(demoLabel18Bits);\nstatic_assert(demoFontAtlasBytes < 1536u*1024u, "Keep all typefaces below 1.5 MiB");\nstatic_assert(demoLabel12Pixels==sizeof(uiFont12Pixels) && demoLabel18Pixels==sizeof(uiFont18Pixels), "Label atlas layout changed");\n\n// The atlases are two-level masks packed one bit per pixel (tools/fonts/pack_demo_atlases.py):\n// pixel i of a face is bit (i & 7) of byte i >> 3. A glyph is a face plus the index of its first\n// pixel, read through alpha() — 0 or 255, exactly the values the unpacked atlases held.\nstruct DemoAlpha {\n    const uint8_t* bits = nullptr;   // the face; null: no custom typeface for this text\n    uint32_t start = 0;              // first pixel of the glyph (grid) or of the atlas (labels)\n    explicit operator bool() const { return bits != nullptr; }\n    uint8_t alpha(uint32_t index) const {\n        const uint32_t i = start + index;\n        return ((bits[i >> 3] >> (i & 7)) & 1u) ? 255 : 0;\n    }\n};\ninline DemoAlpha demo_grid_pixels(int style,int font_scale,uint32_t cp) {\n    if(style<1 || style>16 || cp<32 || cp>126)return {};\n    if(font_scale==2)return {demoGrid2Bits[style-1],(cp-32)*110u};\n    if(font_scale==3)return {demoGrid3Bits[style-1],(cp-32)*255u};\n    return {};\n}\ninline DemoAlpha demo_label_pixels(int style,int size) {\n    if(style<1 || style>16)return {};\n    // As label_font: 14-px atlas up to 14 and for the doubled title size (26 and up), 21 px between.\n    return {size>=15 && size<26?demoLabel18Bits[style-1]:demoLabel12Bits[style-1],0u};\n}\n'
BYTE_COMMENT = "// Static alpha atlases: no allocations, timers, drawing or audio-thread work."
BIT_COMMENT = ("// Static 1-bit alpha atlases (packed by tools/fonts/pack_demo_atlases.py): no allocations, timers,\n"
               "// drawing or audio-thread work.")


def parse_array(text, name):
    head = re.search(r"inline constexpr uint8_t %s\[(\d+)\]\[(\d+)\]=\{" % name, text)
    if not head:
        sys.exit("pack_demo_atlases: %s not found (already packed?)" % name)
    faces, count = int(head.group(1)), int(head.group(2))
    if (faces, count) != EXPECTED_SHAPE[name]:
        sys.exit("pack_demo_atlases: %s is [%d][%d], the readers need [%d][%d]; nothing written"
                 % ((name, faces, count) + EXPECTED_SHAPE[name]))
    end = text.index("\n};", head.end())
    body = text[head.end():end]
    rows = []
    # The generator's exact format, nothing looser: inner blocks "{...}" of comma-separated tokens,
    # each exactly "0" or "255" (no sign, no leading zero — "0255" is octal 173 in C++ — no suffix).
    stray = re.sub(r"\{[^{}]*\}", "", body)
    if stray.replace(",", "").strip():
        sys.exit("pack_demo_atlases: %s has text outside its face blocks: %r" % (name, stray.strip()[:40]))
    # Exactly one comma between two face blocks, one optional after the last: the C++ the compiler
    # accepts. A missing or doubled separator is refused, not silently repaired.
    if not re.fullmatch(r"\s*\{[^{}]*\}(\s*,\s*\{[^{}]*\})*\s*,?\s*", body):
        sys.exit("pack_demo_atlases: %s needs exactly one comma between its face blocks" % name)
    for block in re.findall(r"\{([^{}]*)\}", body):
        tokens = [t.strip() for t in block.split(",")]
        if tokens and tokens[-1] == "":
            tokens.pop()   # the trailing comma of the last row
        bad = [t for t in tokens if t not in ("0", "255")]
        if bad:
            sys.exit("pack_demo_atlases: %s has a value other than 0/255 (%r); 1-bit packing would "
                     "lose it or misread it" % (name, bad[0]))
        values = [int(t) for t in tokens]
        if len(values) != count:
            sys.exit("pack_demo_atlases: %s face has %d values, expected %d" % (name, len(values), count))
        rows.append(values)
    if len(rows) != faces:
        sys.exit("pack_demo_atlases: %s has %d faces, expected %d" % (name, len(rows), faces))
    return faces, count, rows, head.start(), end + len("\n};")


def render_bytes(name, faces, count, rows):
    """The array exactly as the generator writes it: 32 values a line, each line ending in a comma."""
    lines = ["inline constexpr uint8_t %s[%d][%d]={" % (name, faces, count)]
    for values in rows:
        lines.append("{")
        for k in range(0, len(values), 32):
            lines.append(",".join(str(v) for v in values[k:k + 32]) + ",")
        lines.append("},")
    lines.append("};")
    return "\n".join(lines)


def pack(values):
    out = bytearray((len(values) + 7) // 8)
    for i, v in enumerate(values):
        if v:
            out[i >> 3] |= 1 << (i & 7)
    return out


def emit(name, faces, count, rows):
    nbytes = (count + 7) // 8
    lines = ["inline constexpr uint32_t %sPixels = %d;   // pixels per face, as unpacked" % (name, count),
             "inline constexpr uint8_t %sBits[%d][%d]={" % (name, faces, nbytes)]
    for values in rows:
        packed = pack(values)
        lines.append("{")
        for k in range(0, len(packed), 32):
            lines.append(",".join(str(b) for b in packed[k:k + 32]) + ",")
        lines.append("},")
    lines.append("};")
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    text = open(path, encoding="utf-8", newline="").read()
    pieces = []
    for name in NAMES:
        pieces.append((name,) + parse_array(text, name))
    # The parts around the arrays must be the generator's, or the result would not compile.
    # Exactly the generator's output, character for character, or nothing is written.
    expected = BYTE_PREFIX + "\n".join(render_bytes(n, f, c, r) for n, f, c, r, _, _ in pieces) + BYTE_SUFFIX
    if text != expected:
        at = next((k for k, (a, b) in enumerate(zip(text, expected)) if a != b), min(len(text), len(expected)))
        sys.exit("pack_demo_atlases: not the generator's byte header (first difference at line %d); "
                 "nothing written" % (text.count("\n", 0, at) + 1))
    for name, faces, count, rows, start, stop in sorted(pieces, key=lambda p: -p[4]):
        text = text[:start] + emit(name, faces, count, rows) + text[stop:]
    begin, end = text.index(BYTE_TRAILER_START), text.index(TRAILER_END)
    text = text[:begin] + BIT_TRAILER + text[end:]
    text = text.replace(BYTE_COMMENT, BIT_COMMENT, 1)
    open(path, "w", encoding="utf-8", newline="").write(text)
    print("packed %s: %s" % (path, ", ".join("%s %dx%d" % (p[0], p[1], p[2]) for p in pieces)))


if __name__ == "__main__":
    main()
