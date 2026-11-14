#!/usr/bin/env python3
"""Generate the Tanmatsu app's font faces as raw VLW .bin blobs.

ONE format definition, and it is not here: the faces are produced by the L2's own
`firmware/tools/mkvlw.py` (the same generator that made `firmware/src/ui/font_*.cpp`), then the byte
array it wrote is extracted into a flat .bin so the app can embed it with ESP-IDF's EMBED_FILES and the
decoder in `main/font.cpp` reads the exact same layout:

    24-byte header  >IIIIII  glyph_count, encoder_version, y_advance, unused, ascent, descent
    24 B per glyph  >IIIIiiI code, h, w, advance, dy, dx, 0
    then all glyph bitmaps, h*w bytes each, 8-bit coverage, row-major from the glyph's top-left

Why .bin and not .cpp: the L2 blobs are Arduino sources (`PROGMEM`, `#include "ui/fonts.h"`), which do
not compile in a plain ESP-IDF component; and a 300-glyph face as C text is ~6x its size on disk.

Usage:
    python3 tools/mkfonts.py            # every face in FACES
    python3 tools/mkfonts.py --check    # verify the .bin files match a fresh render, write nothing

Sizes are the Tanmatsu type scale from `main/tm_theme.h` (title 22 / row 17 / body 13 / meta 12 /
hint 11 / kv mono 12) — the same DejaVu family as the L2, larger for the 800x480 panel (BRIEF § 4).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
OUT = os.path.join(ROOT, 'main', 'fonts')
MKVLW = os.path.join(ROOT, '..', 'firmware', 'tools', 'mkvlw.py')      # the L2's generator: one owner
SANS = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'

# Latin-1 Supplement + Latin Extended-A, the range the L2 ships (č š ž ć đ, ä ö ü ß, and U+00B7).
EXTRA = ','.join(str(c) for c in range(0xA0, 0x180))

FACES = [                      # (output name, ttf, px) — px values are tm_theme.h's kTmF*
    ('sans_title22', SANS, 22),
    ('sans_row17',   SANS, 17),
    ('sans_body13',  SANS, 13),
    ('sans_meta12',  SANS, 12),
    ('sans_hint11',  SANS, 11),
    ('mono_kv12',    MONO, 12),
]


def render(ttf, px, tmpdir):
    """Run the L2 generator, return the raw bytes it wrote (parsed back out of its C array)."""
    cpp = os.path.join(tmpdir, 'face.cpp')
    r = subprocess.run([sys.executable, MKVLW, '--font', ttf, '--size', str(px), '--name', 'Face',
                        '--out', cpp, '--extra', EXTRA, '--scale', '1'],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit('mkvlw.py failed for %s at %d px:\n%s%s' % (ttf, px, r.stdout, r.stderr))
    # mkvlw prints "<path>: N bytes (<glyphs> glyphs, ascent A descent D)" — keep N as the length check.
    m = re.search(r':\s*(\d+) bytes \((\d+) glyphs, ascent (\d+) descent (\d+)\)', r.stdout)
    if not m:
        raise SystemExit('could not read mkvlw.py\'s summary:\n' + r.stdout)
    want_len, glyphs, ascent = int(m.group(1)), int(m.group(2)), int(m.group(3))

    src = open(cpp, encoding='utf-8').read()
    body = '\n'.join(l for l in src.splitlines() if not l.lstrip().startswith('//'))   # the header
    data = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-fA-F]{2})\b', body))        # comment has hex too
    if len(data) != want_len:
        raise SystemExit('%s %dpx: parsed %d bytes, mkvlw.py reported %d — the extraction is wrong'
                         % (os.path.basename(ttf), px, len(data), want_len))
    # Cross-check the header the decoder will read.
    import struct
    count, ver, y_adv, _unused, asc, desc = struct.unpack('>IIIIII', data[:24])
    if count != glyphs or asc != ascent:
        raise SystemExit('%s %dpx: header says %d glyphs/ascent %d, mkvlw.py says %d/%d'
                         % (os.path.basename(ttf), px, count, asc, glyphs, ascent))
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='render and compare, write nothing')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    with tempfile.TemporaryDirectory() as td:
        for name, ttf, px in FACES:
            data = render(ttf, px, td)
            path = os.path.join(OUT, name + '.bin')
            old = open(path, 'rb').read() if os.path.exists(path) else None
            state = 'created' if old is None else ('identical' if old == data else 'CHANGED')
            if a.check and old != data:
                bad += 1
                state += ' (--check: would rewrite)'
            if not a.check:
                open(path, 'wb').write(data)
            print('%-14s %6d bytes  %s' % (name, len(data), state))
    if a.check and bad:
        print('%d face(s) out of date — run tools/mkfonts.py' % bad)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
