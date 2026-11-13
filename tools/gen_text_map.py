#!/usr/bin/env python3
"""Writes src/text_map.h: the characters Arklite's text can hold beyond ASCII (text.c).

Oni's text is bytes, and its text system reads a byte >= 0x80 (but 0xff) and the byte after it as
one 16-bit character: the high byte picks one of a font's 256 glyph arrays, the low byte the glyph
(retail 0x42dbd6, OniFoxed TSiString_GetNextCharacter). Arklite keeps both bytes >= 0x80, so no
byte of a character ever reads as ASCII (a space, '/', '\\' Oni's style code, a NUL): code k is
the bytes 0x80 + k / 128, 0x80 + k % 128, leads 0x80..0xfe. That's 127 * 128 = 16256 characters,
in this order:

  - Latin-1 and Latin Extended-A (Spanish and most European languages), Greek, Cyrillic (Russian,
    Ukrainian...), punctuation, currency, CJK punctuation, hiragana, katakana, bopomofo, fullwidth
    forms
  - the symbols GB2312, Shift-JIS (cp932) and Big5 have beyond those (no private use area)
  - every Han character of GB2312 (Simplified Chinese) and of cp932 (JIS X 0208 + IBM/NEC extras:
    Japanese), 10101 together
  - Big5's (Traditional Chinese) in its order, level 1 (the common ones) first, until it's full

The order is part of the network protocol (names and chat travel as these bytes): change it and
every player needs the new build, as with any change to src/ (the source hash).

Run: python3 tools/gen_text_map.py  (no arguments; writes src/text_map.h)
"""
import os
import sys

MAX = 127 * 128

RANGES = [
    (0x00A0, 0x017F),   # Latin-1 Supplement, Latin Extended-A (Spanish and most European languages)
    (0x0370, 0x03FF),   # Greek
    (0x0400, 0x052F),   # Cyrillic, Cyrillic Supplement
    (0x2010, 0x203F),   # dashes, quotes, ellipsis...
    (0x20A0, 0x20BF),   # currency
    (0x2100, 0x2122),   # letterlike (No, TM)
    (0x2190, 0x2199),   # arrows
    (0x3000, 0x30FF),   # CJK Symbols and Punctuation, Hiragana, Katakana
    (0x3100, 0x312F),   # Bopomofo
    (0xFF00, 0xFFEF),   # Halfwidth and Fullwidth Forms
]


def is_han(c):
    return 0x4E00 <= c <= 0x9FFF or 0x3400 <= c <= 0x4DBF or 0xF900 <= c <= 0xFAFF


def is_private(c):
    return 0xE000 <= c <= 0xF8FF


def codec_chars(codec, leads=range(0x81, 0xFF), trails=range(0x40, 0xFF)):
    """The code page's two-byte characters in its own order"""
    out = []
    for a in leads:
        for b in trails:
            try:
                s = bytes([a, b]).decode(codec)
            except UnicodeDecodeError:
                continue
            if len(s) == 1:
                out.append(ord(s))
    return out


def main():
    order, seen = [], set()

    def add(c):
        if c not in seen and not is_private(c) and len(order) < MAX:
            seen.add(c)
            order.append(c)

    for lo, hi in RANGES:
        for c in range(lo, hi + 1):
            add(c)
    gb, jis = codec_chars("gb2312"), codec_chars("cp932")
    big5_1 = codec_chars("big5", range(0xA1, 0xC7))    # symbols and level 1 (A440..C67E)
    big5_2 = codec_chars("big5", range(0xC9, 0xFA))    # level 2 (C940..F9D5)
    for c in gb + jis + big5_1 + big5_2:
        if not is_han(c):
            add(c)
    symbols = len(order)
    for c in gb + jis:
        if is_han(c):
            add(c)
    gb_jis = len(order) - symbols
    for c in big5_1 + big5_2:
        if is_han(c):
            add(c)
    big5_han = [c for c in big5_1 + big5_2 if is_han(c)]
    missing = [c for c in big5_han if c not in seen]

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "text_map.h")
    with open(path, "w", newline="\n") as f:
        f.write("/* Made by tools/gen_text_map.py: don't edit. Arklite's characters beyond ASCII (text.c):\n")
        f.write(" * code k is the bytes 0x80 + k / 128, 0x80 + k %% 128. %d symbols and letters, %d Han\n" % (symbols, gb_jis))
        f.write(" * of GB2312 and cp932, %d more of Big5 (%d of its Han characters left out). */\n" %
                (len(order) - symbols - gb_jis, len(missing)))
        f.write("#define TEXT_MAP_COUNT %d\n" % len(order))
        f.write("static const u16 g_text_map[TEXT_MAP_COUNT] = {\n")
        for i in range(0, len(order), 16):
            f.write("\t" + ", ".join("0x%04x" % c for c in order[i:i + 16]) + ",\n")
        f.write("};\n")
    print("src/text_map.h: %d characters (%d symbols, %d GB2312/cp932 Han, %d Big5 Han; %d Big5 Han left out)" %
          (len(order), symbols, gb_jis, len(order) - symbols - gb_jis, len(missing)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
