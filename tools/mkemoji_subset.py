#!/usr/bin/env python3
"""Build the Tanmatsu app's emoji SUBSET blob from the L2's full emoji table.

The L2 ships `firmware/src/ui/emoji_data.bin`: 1,416 single-code-point cells, 13x13, RGB565 + 4-bit
alpha, 432 B per cell, 623,056 B total. The Tanmatsu app has 3,200 KiB per app and a 2 MB build
check, and its binary is already ~1.55 MB, so the whole table does not fit. This script writes the
SAME format with fewer glyphs, and `firmware/src/ui/emoji_table.h` (the L2's own parser, included by
the app, never forked) reads the result unchanged:

    16-byte header  "OMEM", u16 version, u16 cellW, u16 cellH, u16 count, u32 dataOffset
    count entries   u32 codePoint, u32 dataOffset, sorted by code point
    each cell       w*h*2 B RGB565 little-endian, then h*ceil(w/2) B of 4-bit alpha,
                    high nibble = left pixel; every cell starts 4-byte aligned

WHICH glyphs (EST-457):
  * every code point the L2's own emoji picker offers (`firmware/src/ui/emoji_picker.cpp` — its five
    curated groups of "most used", parsed out of that file so the two cannot drift),
  * all of Smileys & Emotion (emoticons, the supplemental faces, the cat faces, skull/ghost, the
    hearts, the sweat/dash/anger set, speech and thought balloons, eyes),
  * People & Body hand gestures (waving, OK, thumbs, peace, raised hands, call-me, prayer, ...),
  * the hearts and the common symbols people actually type (check, crosses, warning, question,
    exclamation, arrows, stars, fire, sparkles, weather, clocks, refresh, bell, warning lights).

HARD CAP 120 KB. The script refuses to write a blob over it, and prints the glyph count and bytes.

Usage:
    python3 tools/mkemoji_subset.py            # write main/emoji/emoji_subset.bin
    python3 tools/mkemoji_subset.py --check    # rebuild in memory, compare with the shipped file
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
L2_SRC = os.path.join(ROOT, '..', 'firmware', 'src', 'ui')
SRC_BLOB = os.path.join(L2_SRC, 'emoji_data.bin')
PICKER = os.path.join(L2_SRC, 'emoji_picker.cpp')
OUT = os.path.join(ROOT, 'main', 'emoji', 'emoji_subset.bin')

CAP_BYTES = 120 * 1024

MAGIC = 0x4D454D4F          # "OMEM" little-endian
VERSION = 1


def read_blob(path):
    with open(path, 'rb') as f:
        return f.read()


def parse(blob):
    """The L2's layout, read exactly as firmware/src/ui/emoji_table.h reads it."""
    if len(blob) < 16:
        raise SystemExit('%s: too short' % SRC_BLOB)
    magic, version, w, h, count, data_off = struct.unpack('<IHHHHI', blob[:16])
    if magic != MAGIC or version != VERSION:
        raise SystemExit('%s: not an OMEM v1 blob' % SRC_BLOB)
    entries = []
    for i in range(count):
        cp, off = struct.unpack('<II', blob[16 + i * 8:16 + i * 8 + 8])
        entries.append((cp, off))
    cell = w * h * 2 + h * ((w + 1) // 2)
    cell = (cell + 3) & ~3
    return dict(w=w, h=h, count=count, data_off=data_off, cell=cell, entries=entries)


def picker_cps():
    """Every code point the L2's curated picker offers: the one list a user sees first."""
    cps = set()
    with open(PICKER, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line.startswith('0x'):
                continue
            for tok in line.split(','):
                tok = tok.strip()
                if re.fullmatch(r'0x[0-9A-Fa-f]{4,6}', tok):
                    cps.add(int(tok, 16))
    return cps


# (first, last) inclusive code-point blocks, chosen for everyday chat use.
BLOCKS = [
    # --- Smileys & Emotion ---------------------------------------------------------------------
    (0x1F600, 0x1F64F),   # emoticons: grinning .. angry
    (0x1F910, 0x1F92F),   # supplemental: nerd, love eyes, mind blown, yawn, ...
    (0x1F970, 0x1F978),   # smiling hearts, hot, partying, woozy, pleading, ...
    (0x1F97A, 0x1F97A),   # pleading face
    (0x1F9D0, 0x1F9D0),   # monocle
    (0x1F63A, 0x1F640),   # cat faces + weary cat
    (0x1F480, 0x1F480),   # skull
    (0x1F47B, 0x1F47D),   # ghost, alien, alien monster
    (0x1F47F, 0x1F47F),   # angry devil
    (0x1F48B, 0x1F48C),   # kiss mark, love letter
    (0x1F493, 0x1F49F),   # hearts both blocks
    (0x1F4A0, 0x1F4AF),   # diamonds, bomb, anger, sweat drops, 100
    (0x1F440, 0x1F441),   # eyes
    (0x1F4AC, 0x1F4AD),   # speech / thought balloon
    (0x1F5A4, 0x1F5A4),   # black heart
    (0x1F5E3, 0x1F5E3),   # speaking head
    (0x1F90D, 0x1F90E),   # white / brown heart
    (0x1F9E1, 0x1F9E1),   # orange heart
    (0x2665, 0x2665),     # heart suit
    (0x2763, 0x2764),     # heart exclamation, heavy heart
    # --- People & Body: hand gestures ----------------------------------------------------------
    (0x1F44A, 0x1F450),   # punch, wave, OK, thumbs, clap, open hands, crown
    (0x1F4AA, 0x1F4AA),   # flexed biceps
    (0x1F590, 0x1F590),   # hand with fingers splayed
    (0x1F595, 0x1F596),   # middle finger, vulcan
    (0x1F645, 0x1F64F),   # no/good, bowing, raising hands, palms, prayer ...
    (0x1F90C, 0x1F90C),   # pinched fingers
    (0x1F90F, 0x1F90F),   # pinching hand
    (0x1F918, 0x1F91F),   # sign of the horns .. love-you
    (0x1F932, 0x1F933),   # palms up, selfie
    (0x261D, 0x261D),     # index pointing up
    (0x270A, 0x270D),     # fist, waving hand, OK hand, writing hand
    # --- Common symbols ------------------------------------------------------------------------
    (0x203C, 0x203C), (0x2049, 0x2049),                  # double exclamation / question
    (0x2194, 0x2199),                                    # arrows left-right .. down-left
    (0x231A, 0x231B),                                    # watch, hourglass
    (0x23F0, 0x23F0), (0x23F3, 0x23F3),                  # alarm clock, hourglass flowing
    (0x2600, 0x2604),                                    # sun, cloud, umbrella, snowman, comet
    (0x260E, 0x260E), (0x2615, 0x2615),                  # telephone, hot beverage
    (0x26A0, 0x26A1),                                    # warning, high voltage
    (0x26BD, 0x26BE), (0x26C4, 0x26C5), (0x26D4, 0x26D4),  # soccer, snowman, clouds, no entry
    (0x26F5, 0x26F5), (0x26FA, 0x26FA), (0x26FD, 0x26FD),  # boat, tent, fuel pump
    (0x2705, 0x2705), (0x2728, 0x2728),                  # check mark button, sparkles
    (0x274C, 0x274C), (0x274E, 0x274E),                  # cross mark, cross mark button
    (0x2753, 0x2755), (0x2757, 0x2757),                  # question, exclamation marks
    (0x2795, 0x2797),                                    # plus, minus, divide
    (0x27A1, 0x27A1), (0x27B0, 0x27B0),                  # right arrow, curly loop
    (0x2934, 0x2935),                                    # arrows
    (0x2B05, 0x2B07), (0x2B50, 0x2B50), (0x2B55, 0x2B55),  # arrows, star, red circle
    (0x1F195, 0x1F195), (0x1F197, 0x1F197),              # NEW, OK
    (0x1F31F, 0x1F31F), (0x1F320, 0x1F320),              # glowing star, shooting star
    (0x1F381, 0x1F381), (0x1F389, 0x1F38A),              # gift, party popper, confetti
    (0x1F3C1, 0x1F3C1), (0x1F3F3, 0x1F3F4),              # flag, white/black flag
    (0x1F4A1, 0x1F4A1), (0x1F4BB, 0x1F4BB),              # bulb, laptop
    (0x1F4E1, 0x1F4E1), (0x1F4E4, 0x1F4E8),              # antenna, envelopes
    (0x1F4F1, 0x1F4F1), (0x1F4F6, 0x1F4F7),              # phone, signal bars, camera
    (0x1F501, 0x1F501), (0x1F504, 0x1F504),              # repeat, refresh
    (0x1F512, 0x1F513),                                  # locks
    (0x1F50B, 0x1F50B), (0x1F511, 0x1F514),              # battery, keys, lock, bell
    (0x1F525, 0x1F525),                                  # fire
    (0x1F6A5, 0x1F6A9),                                  # traffic light .. flag in hole
]


def select(entries):
    """Which code points go into the subset, sorted, with the L2 cell bytes for each."""
    have = {cp for cp, _ in entries}
    wanted = {cp for cp in picker_cps() if cp in have}          # the picker's curated set, guaranteed
    for a, b in BLOCKS:
        wanted.update(cp for cp in have if a <= cp <= b)
    missing = sorted(cp for cp in picker_cps() if cp not in have)
    if missing:
        raise SystemExit('picker code points not in the L2 blob: %s' % ', '.join('U+%04X' % c for c in missing))
    return sorted(wanted)


def build(src, info, cps):
    by_cp = dict(info['entries'])
    w, h, cell = info['w'], info['h'], info['cell']
    used = w * h * 2 + h * ((w + 1) // 2)
    out = bytearray(16 + 8 * len(cps))
    offsets = []
    for cp in cps:
        offsets.append(len(out))
        out += src[by_cp[cp]:by_cp[cp] + used]
        out += b'\x00' * (cell - used)      # 4-byte alignment, as the L2's cells have
    # The header is written last: dataOffset is where the cells start.
    header = struct.pack('<IHHHHI', MAGIC, VERSION, w, h, len(cps), 16 + 8 * len(cps))
    out[:16] = header
    for i, (cp, off) in enumerate(zip(cps, offsets)):
        struct.pack_into('<II', out, 16 + i * 8, cp, off)
    return bytes(out)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--check', action='store_true', help='rebuild in memory and compare with the shipped blob')
    args = ap.parse_args(argv)

    src = read_blob(SRC_BLOB)
    info = parse(src)
    cps = select(info['entries'])
    blob = build(src, info, cps)

    print('%s: %d bytes (%d glyphs of %d, %dx%d cells)' %
          (os.path.relpath(OUT, ROOT), len(blob), len(cps), info['count'], info['w'], info['h']))
    # The L2 parser's own bounds rule, re-checked here so a bad build never reaches the tree.
    index_end = 16 + 8 * len(cps)
    if index_end > len(blob) or 16 + 8 * len(cps) + len(cps) * info['cell'] > len(blob):
        raise SystemExit('built blob fails emoji_table.h bounds')
    if len(blob) > CAP_BYTES:
        raise SystemExit('REFUSED: %d bytes is over the %d-byte cap' % (len(blob), CAP_BYTES))
    print('cap: %d B, headroom %d B' % (CAP_BYTES, CAP_BYTES - len(blob)))

    if args.check:
        have = read_blob(OUT) if os.path.exists(OUT) else b''
        if have != blob:
            raise SystemExit('%s is STALE (%d B on disk, %d B from source)' % (OUT, len(have), len(blob)))
        print('check: %s matches a fresh build' % os.path.relpath(OUT, ROOT))
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as f:
        f.write(blob)
    print('wrote %s' % os.path.relpath(OUT, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
