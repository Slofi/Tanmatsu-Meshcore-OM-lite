#!/usr/bin/env python3
"""ONE preview tool for the Tanmatsu app (the Tanmatsu side of firmware/tools/preview.py).

Same contract as the L2 tool, on purpose:
  * the PALETTE is not retyped here: every colour is parsed out of the L2 theme (src/ui/theme.h), so a
    change there reaches these previews and the OM Lite look cannot fork into two palettes;
  * the METRICS live in the TM table below and nowhere else (the L2 puts them in theme.h/layout.h; when
    the Tanmatsu layout header exists, this table moves into it and the numbers come from a compiled
    dumper instead, exactly as the L2 tool does it);
  * these are HOST RENDERS, not screenshots: no frame comes off the device, and the text is desktop
    DejaVu, not the firmware's generated VLW faces. Type sizes are therefore the design intent, not the
    final panel metrics;
  * every name, count, address and message shown is INVENTED (public-repo rule 9: no coordinates, no LAN
    addresses, no SSIDs, no keys, no real messages). The node/channel names are placeholders.

Look authority (BRIEF.md § 4), in order: the running L2 firmware's theme + its real screenshots, then the
approved ui-mock (its .dev-tan block), then Filip's palette. This tool deliberately does NOT copy the
mock's icon buttons: the five physical symbol keys are the page switch (BRIEF § 5).

Usage:
    python3 tools/preview_tanmatsu.py              # every preview
    python3 tools/preview_tanmatsu.py launcher     # one
    PREVIEW_OUT=/tmp/png python3 tools/preview_tanmatsu.py
"""
import functools
import os
import re
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
THEME = '/home/slofi/Projects/pocket-mesh/firmware/src/ui/theme.h'
OUT_DIR = os.environ.get('PREVIEW_OUT') or os.path.join(ROOT, 'shots')

FONT_SANS = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FONT_BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
FONT_MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'

# --------------------------------------------------------------------------------------------- palette
def palette():
    """Every rgb565() token in the L2 theme, as #rrggbb. Parsed, never retyped."""
    src = open(THEME, encoding='utf-8').read()
    out = {}
    for name, hexv in re.findall(r'constexpr uint16_t k(\w+)\s*=\s*rgb565\(0x([0-9a-fA-F]{6})\)', src):
        out[name] = '#' + hexv.lower()
    missing = [k for k in ('Bg', 'Screen', 'Panel', 'Panel2', 'Bar', 'Line', 'RowLine', 'Ink', 'Dim',
                           'Dimmer', 'Acc', 'AccInk', 'SelBg', 'SelBg2', 'Client', 'Green', 'Warn',
                           'Bad', 'KeyBg', 'KeyLine', 'FieldBg') if k not in out]
    if missing:
        raise SystemExit('theme.h no longer has: %s (preview palette would be a second palette)'
                         % ', '.join(missing))
    return out


P = palette()

# --------------------------------------------------------------------------------------------- metrics
# 800x480, 3.97", no touch. One table: the page geometry the render uses. Every number is either the
# approved mock's .dev-tan rule or an L2 constant scaled for the 800 px panel; the comment says which.
# When tanmatsu/src/ui/tm_theme.h exists this table must MOVE there, and the render must read it back.
TM = dict(
    W=800, H=480,
    status_h=26,        # mock .dev-tan .status{height:26px} (the L2's own kStatusH is 26 at 320x240)
    hint_h=24,          # mock .hints 20 px + 4 so 11 px chips sit inside it
    pad=18,             # mock .dev-tan .row{padding:8px 12px} + .detail{padding:16px 18px} -> 18
    head_h=44,          # page title band under the status bar (the L2's kHeadH 23 scaled)
    list_w=320,         # mock .dev-tan .pane .list{width:280px} + the L2's 30 px rows scaled
    row_h=64,           # L2 kRowH 30 -> 64 (2.1x, the same "expanded" step as the type)
    ring_w=2,           # the focus ring: BRIEF § 5, gold. 2 px at 800 (the mock's outline:1px at 320)
    # EST-414, scaled: the L2 draws the selection band + ring kSelPadX (7 px) in from the row's own right
    # edge and the scroll track in the LAST kScrollTrackW (4 px) of the viewport, so the text ends 3 px
    # before the track and the track can never reach the ring (ui/scroll_indicator.h:8-11,
    # ui/chat_selector.cpp:65). At 800 px: 7 -> 14, 4 -> 6.
    ring_inset=14,      # the band/ring's inset from the pane's right edge (L2 kSelPadX x2)
    scroll_w=6,         # the track width, inside that inset (L2 kScrollTrackW x1.5)
    scroll_min=24,      # L2 kScrollMinThumbH 8 -> 24, so a long list still shows something to hold
    # types (px). L2: SansBody 11 / SansTitle 14 / SansTab 9 / MonoMeta 10 at 320x240.
    # mock .dev-tan: body 13 / h2 18 / kv 12 / meta 10 / status 11, hints 9.
    f_title=22, f_row=17, f_body=13, f_meta=12, f_label=11, f_status=12, f_hint=11, f_kv=12,
    # The Chat list's cards (S513/EST-430). These mirror main/chat_page.h kTmCard* exactly; core/chat_cards.cpp
    # turns them into rects and the device draws at them, so the preview and the panel must not drift.
    card_gutter=16, card_header_h=30, card_h=68, card_gap=10, card_radius=6,
    # The start menu's three app cards (S513/EST-432). They mirror main/launcher_page.h kTmMenu* exactly:
    # the same 16 px gutter and 6 px radius as the Chat cards, one large plate per app.
    menu_gap=16, menu_card_h=232, menu_top=22, menu_radius=6,
)

SIGILS = ('triangle', 'square', 'circle', 'cloud', 'diamond')   # BRIEF § 5, Filip's names


@functools.lru_cache(maxsize=64)
def font(path, px):
    return ImageFont.truetype(path, px)


def tw(f, s):
    return f.getbbox(s)[2]


# --------------------------------------------------------------------------------------------- drawing
def sigil(d, kind, cx, cy, r, colour, width=2):
    """The five physical key symbols, drawn as vectors: no font may be missing a glyph on the glass."""
    if kind == 'triangle':
        d.polygon([(cx, cy - r), (cx + r, cy + r), (cx - r, cy + r)], outline=colour, width=width)
    elif kind == 'square':
        d.rectangle([cx - r, cy - r, cx + r, cy + r], outline=colour, width=width)
    elif kind == 'circle':
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=colour, width=width)
    elif kind == 'diamond':
        d.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], outline=colour, width=width)
    elif kind == 'cloud':                      # "cloud" = the tri-circle key (Filip's name)
        # OUTLINE like the other four (S499, Filip: "fix the cloud symbol"). It used to be solid-filled,
        # which made the fifth key read as "active" on every page while the other four read as inactive.
        # The union silhouette is drawn on a mask, eroded by `width`, and only the difference is painted,
        # so the arcs where the three circles overlap never show and the stroke weight matches the rest.
        ss = 4                                  # supersample: the sigils are ~7-9 px, aliasing is visible
        pad = width * 2
        side = (2 * r + 2 * pad + 2) * ss
        c = side / 2.0
        mask = Image.new('L', (side, side), 0)
        md = ImageDraw.Draw(mask)
        rr = r * 0.60 * ss
        for ox, oy in ((0, -r * 0.45), (-r * 0.86, r * 0.10), (r * 0.86, r * 0.10)):
            md.ellipse([c + ox * ss - rr, c + oy * ss - rr, c + ox * ss + rr, c + oy * ss + rr], fill=255)
        md.rounded_rectangle([c - r * ss, c, c + r * ss, c + r * 0.75 * ss],
                             radius=max(1, r * 0.5 * ss), fill=255)
        inner = mask.filter(ImageFilter.MinFilter(int(width * ss) * 2 + 1))   # erode -> the hole
        ring = ImageChops.subtract(mask, inner).resize((side // ss, side // ss), Image.LANCZOS)
        img = d._image if hasattr(d, '_image') else d.im
        img.paste(Image.new(img.mode, ring.size, colour),
                  (int(cx - ring.size[0] / 2), int(cy - ring.size[1] / 2)), ring)


def text(d, xy, s, f, colour, anchor='la'):
    d.text(xy, s, font=f, fill=colour, anchor=anchor)


# ------------------------------------------------------------------------------------------------- emoji
# S514/EST-457: the SAME subset blob the app embeds (tools/mkemoji_subset.py), read with the L2's layout
# ("OMEM" v1, 13x13 RGB565 + 4-bit alpha) and drawn at 2x nearest neighbour, its coverage blended over what is
# already there — the app's rule in main/font.cpp, so the preview cannot show a placement the device lacks.
EMOJI_BIN   = os.path.join(ROOT, 'main', 'emoji', 'emoji_subset.bin')
EMOJI_SCALE = 2
EMOJI_PX    = 13 * EMOJI_SCALE
EMOJI_MISSING_W = 6            # the app's half-em advance for a code point the subset does not hold
_EMOJI = None


def emoji_table():
    """{code point: (rgb bytes, alpha bytes[, w, h])} from the app's own subset blob."""
    global _EMOJI
    if _EMOJI is None:
        _EMOJI = {}
        try:
            with open(EMOJI_BIN, 'rb') as fh:
                blob = fh.read()
        except OSError:
            return _EMOJI
        _magic, _ver, w, h, count, _data_off = struct.unpack('<IHHHHI', blob[:16])
        cell = (w * h * 2 + h * ((w + 1) // 2) + 3) & ~3
        for i in range(count):
            cp, off = struct.unpack('<II', blob[16 + i * 8:24 + i * 8])
            _EMOJI[cp] = (blob[off:off + w * h * 2], blob[off + w * h * 2:off + cell], w, h)
    return _EMOJI


def emoji_cell(cp):
    return emoji_table().get(cp)


_EMOJI_MODS = (0x200D, 0xFE0E, 0xFE0F, 0x20E3)


def emoji_units(s):
    """The L2's unit walk in Python: a selector/keycap/skin tone belongs to its base, a ZWJ to the part before
    it. Yields ('t', text-run) and ('e', code-point); a lone modifier is dropped (zero width, as on the device)."""
    runs, buf = [], ''
    i, n = 0, len(s)
    while i < n:
        cp = ord(s[i])
        if cp in _EMOJI_MODS or 0x1F3FB <= cp <= 0x1F3FF:
            i += 1
            continue
        if cp < 0x2000 or cp > 0x1FAFF:
            buf += s[i]
            i += 1
            continue
        j = i + 1
        while j < n and (ord(s[j]) in _EMOJI_MODS or 0x1F3FB <= ord(s[j]) <= 0x1F3FF):
            j += 1
        if j < n and ord(s[j]) == 0x200D:
            j += 1
        if buf:
            runs.append(('t', buf))
            buf = ''
        runs.append(('e', cp))
        i = j
    if buf:
        runs.append(('t', buf))
    return runs


def mixed_width(s, f):
    w = 0
    for kind, chunk in emoji_units(s):
        if kind == 't':
            w += tw(f, chunk)
        else:
            w += EMOJI_PX if emoji_cell(chunk) else EMOJI_MISSING_W
    return w


def draw_emoji(d, x, cy, cp):
    """One 13x13 cell at 2x, over the pixels already drawn. Vertical centre = the text line's centre."""
    cell = emoji_cell(cp)
    if cell is None:
        return 0
    rgb, alpha, w, h = cell
    img = getattr(d, '_image', None) or getattr(d, 'im')
    stride = (w + 1) // 2
    top = int(round(cy - (h * EMOJI_SCALE) / 2.0))
    for sy in range(h):
        for sx in range(w):
            packed = alpha[sy * stride + sx // 2]
            nib = (packed & 0x0F) if (sx & 1) else (packed >> 4)
            if not nib:
                continue
            at = (sy * w + sx) * 2
            v = rgb[at] | (rgb[at + 1] << 8)
            r = (((v >> 11) & 0x1F) * 255 + 15) // 31
            g = (((v >> 5) & 0x3F) * 255 + 31) // 63
            b = ((v & 0x1F) * 255 + 15) // 31
            a = nib / 15.0
            for dy in range(EMOJI_SCALE):
                for dx in range(EMOJI_SCALE):
                    px, py = x + sx * EMOJI_SCALE + dx, top + sy * EMOJI_SCALE + dy
                    if not (0 <= px < img.width and 0 <= py < img.height):
                        continue
                    dr, dg, db = img.getpixel((px, py))[:3]
                    img.putpixel((px, py), (int(round(r * a + dr * (1 - a))),
                                            int(round(g * a + dg * (1 - a))),
                                            int(round(b * a + db * (1 - a)))))
    return EMOJI_PX


def text_mixed(d, xy, s, f, colour, anchor='la'):
    """A string with emoji in it. `anchor` keeps the PIL meaning for the text runs ('la'/'lm'/'ra'/'rm')."""
    x, y = xy
    total = mixed_width(s, f)
    if anchor[0] == 'r':
        x -= total
    elif anchor[0] == 'm':
        x -= total // 2
    line_cy = y if anchor[1] == 'm' else y + f.size * 0.62
    for kind, chunk in emoji_units(s):
        if kind == 't':
            d.text((x, y), chunk, font=f, fill=colour, anchor=anchor)
            x += tw(f, chunk)
        else:
            if emoji_cell(chunk):
                draw_emoji(d, x, line_cy, chunk)
                x += EMOJI_PX
            else:
                x += EMOJI_MISSING_W   # not in the subset: today's gap, exactly as the app leaves it


def wrap_mixed(s, f, maxw):
    """The same greedy wrap as wrap(), but measuring emoji at their drawn 26 px."""
    words, lines, cur = s.split(), [], ''
    for w in words:
        t = (cur + ' ' + w).strip()
        if mixed_width(t, f) <= maxw:
            cur = t
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines or ['']


def spaced(d, xy, s, f, colour, gap=1.2):
    """A mono caps label with the small tracking the L2's section labels have."""
    x, y = xy
    for ch in s:
        d.text((x, y), ch, font=f, fill=colour, anchor='la')
        x += tw(f, ch) + gap
    return x


def hline(d, x0, x1, y, colour):
    d.line([(x0, y), (x1, y)], fill=colour, width=1)


def vline(d, x, y0, y1, colour):
    d.line([(x, y0), (x, y1)], fill=colour, width=1)


def plate(d, box, fill, line=None, r=6, w=1):
    if line:
        d.rounded_rectangle(box, radius=r, fill=fill, outline=line, width=w)
    else:
        d.rounded_rectangle(box, radius=r, fill=fill)


def focus_band(d, x0, x1, y0, y1):
    """The selected row: the L2's band + 2 px gold ring, both inset `ring_inset` from the pane's right
    edge (kSelPadX). The inset is what leaves room for the scroll track: in the L2 the band, the text
    column and the track share that padding (ui/scroll_indicator.h:8-11), so the two can never touch."""
    right = x1 - TM['ring_inset']
    d.rectangle([x0, y0, right, y1], fill=P['SelBg'])
    d.rectangle([x0, y0, right, y1], outline=P['Acc'], width=TM['ring_w'])
    return right


def scroll_indicator(d, viewport, total, visible, scroll=0):
    """EST-414's rule, drawn the same way: the track is the last `scroll_w` px of the viewport (never
    over text, 3 px clear of the text column), the thumb's length is the visible share of the content
    (at least `scroll_min`), its top is the scrolled share. Nothing is drawn when the content fits
    (ui/scroll_indicator.h). `viewport` is (x0, y0, x1, y1); x1 is the pane's right edge, so the track
    lands inside the same inset the band leaves free."""
    x0, y0, x1, y1 = viewport
    if total <= visible or visible <= 0:
        return
    w = TM['scroll_w']
    tx = x1 - w
    d.rectangle([tx, y0, x1 - 1, y1], fill=P['Line'])
    h = y1 - y0
    thumb = min(h, max(TM['scroll_min'], int(h * visible / total)))
    top = int((h - thumb) * max(0, scroll) / max(1, total - visible))
    d.rectangle([tx, y0 + top, x1 - 1, y0 + top + thumb], fill=P['Acc'])


def wrap(d, s, f, maxw):
    words, lines, cur = s.split(), [], ''
    for w in words:
        t = (cur + ' ' + w).strip()
        if tw(f, t) <= maxw:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def gear(d, cx, cy, r, colour):
    """The status bar's gear, as the L2 draws it: a vector mask, 8 teeth, never a glyph."""
    import math
    pts = []
    for i in range(16):
        a = math.pi * i / 8 - math.pi / 8
        rad = r if i % 2 == 0 else r * 0.68
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    d.polygon(pts, fill=colour)
    d.ellipse([cx - r * 0.3, cy - r * 0.3, cx + r * 0.3, cy + r * 0.3], fill=P['Bar'])


def battery(d, x, y, colour, fill_colour, level=0.97, w=22, h=11):
    d.rounded_rectangle([x, y, x + w, y + h], radius=2, outline=colour, width=1)
    d.rectangle([x + w + 1, y + 3, x + w + 3, y + h - 3], fill=colour)      # the nub
    iw = int((w - 4) * level)
    if iw > 0:
        d.rectangle([x + 2, y + 2, x + 2 + iw, y + h - 2], fill=fill_colour)


def status_bar(d, clock='--:--', batt=0.97, gps=False, radio=True):
    """The L2's top bar, at 800 px: clock left; right: radio, (GPS only when present), battery, gear.

    No satellite appears on the Tanmatsu itself: it has no GPS. The slot is drawn only when an external
    module is present, which is the L2's own rule for icons that are "on only when they are on".
    """
    h = TM['status_h']
    d.rectangle([0, 0, TM['W'], h], fill=P['Bar'])
    hline(d, 0, TM['W'], h - 1, P['Line'])
    fm = font(FONT_MONO, TM['f_status'])
    cy = h // 2
    text(d, (TM['pad'], cy), clock, fm, P['Dim'], anchor='lm')

    x = TM['W'] - TM['pad']
    gear(d, x - 7, cy, 7, P['Dim'])
    x -= 26
    fmg = font(FONT_MONO, TM['f_status'])
    text(d, (x, cy), '97%', fmg, P['Ink'], anchor='rm')
    x -= tw(fmg, '97%') + 8
    battery(d, x - 24, cy - 5, P['Dim'], P['Green'], batt)
    x -= 24 + 14
    if gps:
        text(d, (x, cy), 'GPS 1', fm, P['Dim'], anchor='rm')
        x -= tw(fm, 'GPS 1') + 14
    if radio:
        text(d, (x, cy), '868', fm, P['Dim'], anchor='rm')
        d.ellipse([x - tw(fm, '868') - 14, cy - 3, x - tw(fm, '868') - 8, cy + 3], fill=P['Green'])
        x -= tw(fm, '868') + 22


def hint_bar(d, page, actions, dots=()):
    """The key-hint bar (BRIEF § 5): what the keys do HERE, same style as the L2's tab bar.

    Left: this page's actions. Right: the five symbol keys, the page switch; the current page's key is
    gold, the rest dim. Drawn as chips so it reads as keys, not as prose.

    S515/EST-467: this now mirrors main/chrome.cpp::drawHintBar's arithmetic EXACTLY — one hint face
    (sans 11) for key caps, labels and page names, a cap of max(30, key width + 10), the 4/8/10 spacing,
    and the same 22/18 right-cluster gaps. A cap that would run under the symbol cluster (or off the left
    margin) is not drawn, so a preview can never claim more room than the glass has.
    """
    top = TM['H'] - TM['hint_h']
    d.rectangle([0, top, TM['W'], TM['H']], fill=P['Bar'])
    hline(d, 0, TM['W'], top, P['Line'])
    f = font(FONT_SANS, TM['f_hint'])
    cy = top + TM['hint_h'] // 2

    # right cluster: the five symbol keys, from the right edge inward (the device's own loop)
    pages = ['home', 'nodes', 'chat', 'map', 'rptr']
    labels = ['LAUNCHER', 'NODES', 'CHAT', 'MAP', 'RPTR']
    sx = TM['W'] - TM['pad']
    for kind, lab, pname in reversed(list(zip(SIGILS, labels, pages))):
        on = (pname == page)
        col = P['Acc'] if on else P['Dimmer']
        sx -= tw(f, lab)
        text(d, (sx, cy), lab, f, col, anchor='lm')
        sx -= 22
        sigil(d, kind, sx, cy, 7, col, width=2)
        if pname in dots:
            d.ellipse([sx + 9 - 3, cy - 8 - 3, sx + 9 + 3, cy - 8 + 3], fill=P['Bad'])
        sx -= 18
    right_start = sx

    # left cluster: the same fit rule and spacing as the device
    hx = TM['pad']
    for keys, what in actions:
        kw = max(30, tw(f, keys) + 10)
        lw = tw(f, what)
        item_w = kw + 4 + 8 + lw + 10
        if hx + item_w > right_start - 8:
            break
        key_cap(d, [hx, cy - 9, hx + kw, cy + 9], keys, P['Dim'], f)
        hx += kw + 4
        text(d, (hx, cy), what, f, P['Dimmer'], anchor='lm')
        hx += 8 + lw + 10


def key_cap(d, box, label, ink, f=None):
    """One key cap in the hint bar's own style (S514/EST-444): dark plate, 1 px outline, centred ink.

    The bar's chips and the Chat composer's Enter cap share this, exactly as main/chrome.cpp::drawKeyCap
    does on the device, so the preview cannot draw a filled "button" the panel no longer has.
    """
    plate(d, box, P['Panel2'], P['KeyLine'], r=3)
    f = f or font(FONT_MONO, TM['f_hint'])
    text(d, ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2), label, f, ink, anchor='mm')


def new_screen():
    img = Image.new('RGB', (TM['W'], TM['H']), P['Screen'])
    return img, ImageDraw.Draw(img)


def save(img, name, scale=1):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    out = img if scale == 1 else img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    out.save(path)
    print('wrote %s  (%dx%d)' % (path, out.width, out.height))
    return path


# ------------------------------------------------------------------------------------------ launcher
MODULES = [
    # id, name, meta, state, detail lines. `state` is the ONE source of the row's truth: the chip and the
    # subtitle are derived from it, so a row cannot say STOPPED next to "later" (EST-425 detail fix).
    dict(name='OM Lite', meta='mesh companion · 4 pages', state='RUNNING',
         sub='started 09:41 · up 1 h 12 m',
         kv=[('STATE', 'running'), ('MEMORY', '26.1 MB PSRAM · 7.9 MB low'),
             ('TASKS', '3 · mesh, ui, sd'), ('OWNS', 'radio 868 · MeshCore session'),
             ('HISTORY', 'SD card · 3 channels')],
         actions=('Open', 'Stop')),
    dict(name='Mesh-Torry', meta='game · Wi-Fi · later', state='LATER',
         sub='not built yet',
         kv=[('STATE', 'not built'), ('MEMORY', '—'), ('NEEDS', 'Wi-Fi · Mesh-Torry server'),
             ('PLAN', 'module on the shared core')],
         actions=(None, None)),
    dict(name='OPS-TOC Lite', meta='ops console · later', state='LATER',
         sub='not built yet',
         kv=[('STATE', 'not built'), ('MEMORY', '—'), ('NEEDS', 'Mesh-Torry · GPS (optional)'),
             ('PLAN', 'module on the shared core')],
         actions=(None, None)),
]


def launcher(dots=(), name='2026-11-13-launcher-home.png'):
    """The Launcher home: this app's own menu (BRIEF § 3), CD clarity in the L2's clothes.

    Left: the module list, one row per module, each with a state chip (CD's status dot made explicit).
    Right: the focused module's detail and its actions. Both panes start under one page header, which is
    the mock's approved Tanmatsu two-pane shape; the list/detail split is 320/444, not a dashboard.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']

    status_bar(d)

    # page header: title left, the shared core's readiness right, one line under
    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Launcher', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), 'core ready · radio free', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    # ---- left pane: the module list
    list_x0, list_x1 = pad, pad + TM['list_w']
    fl = font(FONT_MONO, TM['f_label'])
    spaced(d, (list_x0, work_top + 16), 'MODULES', fl, P['Dim'])
    text(d, (list_x1 - 12, work_top + 16), '3 · 1 running', font(FONT_MONO, TM['f_label']),
         P['Dimmer'], anchor='ra')

    rows_top = work_top + 40
    sel = 0
    row_right = list_x1 - TM['ring_inset']       # the one right edge every row's content respects
    for i, m in enumerate(MODULES):
        y0 = rows_top + i * TM['row_h']
        y1 = y0 + TM['row_h']
        running = m['state'] == 'RUNNING'
        later = m['state'] == 'LATER'
        if i == sel:
            # the focus ring: gold, inset from the pane edge like the L2's kSelPadX (focus_band)
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])

        dot = P['Green'] if running else P['Dimmer']
        d.ellipse([list_x0 + 6, y0 + 22, list_x0 + 14, y0 + 30], fill=dot)
        name_col = P['AccInk'] if i == sel else (P['Dim'] if later else P['Ink'])
        text(d, (list_x0 + 24, y0 + 13), m['name'], font(FONT_SANS, TM['f_row']), name_col)
        # state chip, right-aligned: CD's status dot spelled out, so "running" is never a guess
        chip = m['state']
        fc = font(FONT_MONO, TM['f_label'])
        cw = tw(fc, chip) + 12
        ccol, cline = ((P['Green'], P['Line']) if running else (P['Dimmer'], P['Line']))
        plate(d, [row_right - 8 - cw, y0 + 12, row_right - 8, y0 + 12 + 18], P['Panel2'], cline, r=3)
        text(d, (row_right - 8 - cw / 2, y0 + 21), chip, fc, ccol, anchor='mm')
        text(d, (list_x0 + 24, y0 + 36), m['meta'], font(FONT_MONO, TM['f_meta']),
             P['Dimmer'] if later else P['Dim'])

    # the honest footnote under the list (BRIEF § 3: one module crash restarts the app)
    fnote = font(FONT_MONO, TM['f_label'])
    note = ('The stock launcher starts one app at a time. Here the modules start and stop '
            'inside this app; the core keeps the radio.')
    ny = rows_top + len(MODULES) * TM['row_h'] + 26
    for line in wrap(d, note, fnote, TM['list_w'] - 14):
        text(d, (list_x0, ny), line, fnote, P['Dimmer'])
        ny += 17

    # ---- right pane: the focused module
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    m = MODULES[sel]
    x = list_x1 + 8 + 24
    rw = W - pad - x
    yy = work_top + 18
    text(d, (x, yy), m['name'], font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), m['sub'], font(FONT_MONO, TM['f_meta']), P['Dim'])
    yy += 26

    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in m['kv']:
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 120, yy), value, fk, P['Ink'])
        yy += 30

    # actions: gold for the primary, a plate for the second (never a bare "×" like the CD web page:
    # on a keyboard launcher the action needs a word and a key)
    yy += 14
    hline(d, x, x + rw, yy, P['Line'])
    yy += 18
    bx = x
    for a in m['actions']:
        if not a:
            continue
        fab = font(FONT_SANS, TM['f_body'])
        bw = tw(fab, a) + 52
        primary = (a == 'Open')
        if primary:
            plate(d, [bx, yy, bx + bw, yy + 40], P['SelBg2'], P['Acc'], r=6)
            text(d, (bx + 18, yy + 20), a, fab, P['AccInk'], anchor='lm')
            text(d, (bx + bw - 18, yy + 20), '↵' if a == 'Open' else 's', fk, P['Acc'], anchor='rm')
        else:
            plate(d, [bx, yy, bx + bw, yy + 40], P['Panel2'], P['Line'], r=6)
            text(d, (bx + 18, yy + 20), a, fab, P['Ink'], anchor='lm')
            text(d, (bx + bw - 18, yy + 20), 's', fk, P['Dim'], anchor='rm')
        bx += bw + 12

    # the stop rule, in words: what Stop actually does (Filip asked for exactly this)
    yy += 66
    fnote2 = font(FONT_MONO, TM['f_label'])
    stop = ('Stop ends the module\'s tasks and frees its memory; the core and the radio '
            'keep running, so no restart is needed.')
    for line in wrap(d, stop, fnote2, rw):
        text(d, (x, yy), line, fnote2, P['Dimmer'])
        yy += 16

    hint_bar(d, 'home', [(',', 'settings'), ('l/r', 'select'), ('Enter', 'open'), ('esc', 'quit')], dots)
    # S515/EST-461: the same page drawn with BOTH notification dots for the hint-dots preview. The launcher is
    # deliberately not the page the dots live on, which is the point: every page shows them.
    return save(img, name)


# ---------------------------------------------------------------------------------------- om lite
# Invented, like every fixture in this file: the real node names in the L2 screenshots are other people's
# infrastructure and must not be copied into a public-repo render (rule 9).
NODE_KEY = '4b 91 2e ... c7'      # ONE fake key: the detail header and the PUBLIC KEY row both read this
NODES = [
    ('client', 'TEST-A1', 'direct', '14h', '3.2', True),
    ('repeater', 'RPT-Sever', 'path 2', '18m', '7.5', True),
    ('repeater', 'RPT-Jug', 'path 2', '49m', '6.1', False),
    ('repeater', 'RPT-Vzhod', 'path 3', '9h', '-1.4', False),
    ('repeater', 'RPT-Zahod', 'path 3', '9h', '0.2', False),
    ('repeater', 'RPT-Center', 'path 4', '9h', '-2.8', False),
    ('client', 'LOCAL-KB', 'direct', '2d', '4.4', False),
]


def nodes():
    """OM Lite > Nodes at 800 px: list left (favourites on top), detail right.

    The "expanded" step of BRIEF § 4: the L2's 320 px row shows name + one meta column; here the row
    carries hops, SNR and last heard, and the detail pane shows the Info page that on the L2 is a
    separate full-screen push. Same palette, same row line, same focus ring, same gold scroll thumb.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Nodes', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), '14 nodes heard · 2 in the last hour', fm, P['Dim'],
         anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    fl = font(FONT_MONO, TM['f_label'])
    spaced(d, (list_x0, work_top + 16), 'NODES', fl, P['Dim'])
    text(d, (list_x1 - 12, work_top + 16), '7 of 14', font(FONT_MONO, TM['f_label']), P['Dimmer'],
         anchor='ra')

    rows_top = work_top + 40
    row_h = 46
    row_right = list_x1 - TM['ring_inset']
    for i, (kind, name, hops, age, snr, fav) in enumerate(NODES):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == 0:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])
        col = P['Client'] if kind == 'client' else P['Green']
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=col)
        nc = P['AccInk'] if i == 0 else P['Ink']
        text(d, (list_x0 + 24, y0 + 10), name, font(FONT_SANS, 16), nc)
        meta = font(FONT_MONO, TM['f_meta'])
        text(d, (list_x0 + 24, y0 + 29), hops, meta, P['Dimmer'])
        text(d, (row_right - 74, y0 + 11), age, meta, P['Dim'], anchor='ra')
        text(d, (row_right - 6, y0 + 29), snr + ' dB', meta, P['Dim'], anchor='ra')
        if fav:
            text(d, (row_right - 6, y0 + 11), '★', meta, P['Acc'], anchor='ra')

    # the gold scroll indicator: the L2's EST-414 rule, in the pane's own right padding, clear of the
    # focus ring (the band stops `ring_inset` short, the track takes the last `scroll_w` px)
    scroll_indicator(d, (list_x0, rows_top, list_x1, rows_top + len(NODES) * row_h),
                     total=14, visible=len(NODES))

    # detail pane
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    rw = W - pad - x
    yy = work_top + 18
    text(d, (x, yy), NODES[0][1], font(FONT_SANS, 26), P['Ink'])
    yy += 34
    fm2 = font(FONT_MONO, TM['f_meta'])
    # ONE fake key, used by the header line and the PUBLIC KEY row, so the two cannot disagree
    text(d, (x, yy), 'client · key ' + NODE_KEY + ' · favourite', fm2, P['Dim'])
    yy += 26

    fk = font(FONT_MONO, TM['f_kv'])
    kv = [('TYPE', 'client (companion)'), ('HOPS', 'direct · 0 hops'),
          ('SNR / RSSI', '3.2 dB / -71 dBm'), ('LAST HEARD', '14 h ago'),
          ('PUBLIC KEY', NODE_KEY), ('POSITION', 'shared · 2 h old'),
          ('PATH', 'direct'), ('CHANNELS', '#test-a, #test-b')]
    for label, value in kv:
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28

    yy += 8
    hline(d, x, x + rw, yy, P['Line'])
    yy += 16
    fab = font(FONT_SANS, TM['f_body'])
    bx = x
    for a, key, primary in (('DM', 'd', True), ('Trace', 't', False), ('Favourite', 'f', False)):
        bw = tw(fab, a) + 50
        if primary:
            plate(d, [bx, yy, bx + bw, yy + 38], P['SelBg2'], P['Acc'], r=6)
            text(d, (bx + 16, yy + 19), a, fab, P['AccInk'], anchor='lm')
            text(d, (bx + bw - 16, yy + 19), key, fk, P['Acc'], anchor='rm')
        else:
            plate(d, [bx, yy, bx + bw, yy + 38], P['Panel2'], P['Line'], r=6)
            text(d, (bx + 16, yy + 19), a, fab, P['Ink'], anchor='lm')
            text(d, (bx + bw - 16, yy + 19), key, fk, P['Dim'], anchor='rm')
        bx += bw + 12

    hint_bar(d, 'nodes', [('↑↓', 'move'), ('Enter', 'info'), ('d', 'DM'), ('t', 'trace')])
    return save(img, '2026-11-13-nodes.png')


# S514/EST-449: the search preview's fixture — six invented nodes, exactly TWO containing "era".
NODES_SEARCH = [
    ('client', 'TEST-Vera', 'direct', '8m', '4.1', True),
    ('repeater', 'RPT-Kamera', 'path 2', '18m', '7.5', True),
    ('repeater', 'RPT-Jug', 'path 2', '49m', '6.1', False),
    ('repeater', 'RPT-Vzhod', 'path 3', '9h', '-1.4', False),
    ('repeater', 'RPT-Zahod', 'path 3', '9h', '0.2', False),
    ('client', 'LOCAL-KB', 'direct', '2d', '4.4', False),
]


def nodes_search():
    """OM Lite > Nodes, S514/EST-449: the search bar as the list's TOP ROW, always visible.

    Query "era" typed, "2 of 6", the two matches below. The bar is FOCUSED (the gold 2 px ring of a row)
    and no list row is drawn as selected while the bar owns the keys. ONE field: the query left, the count
    right, both 12 px clear of the plate's border (the L2's S508 correction). No magnifier glyph: the
    firmware faces carry Latin-1 + Latin Extended-A only, so there is no U+2315 / U+1F50D to draw.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Nodes', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), '6 nodes heard · 2 in the last hour', fm, P['Dim'],
         anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    bar_x1 = list_x1 - TM['ring_inset']
    bar_y, bar_h = work_top + 4, 30
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], fill=P['FieldBg'])
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], outline=P['Acc'], width=TM['ring_w'])
    text(d, (list_x0 + 12, bar_y + bar_h // 2), 'era', font(FONT_SANS, TM['f_body']), P['Ink'],
         anchor='lm')
    text(d, (bar_x1 - 12, bar_y + bar_h // 2), '2 of 6', font(FONT_MONO, TM['f_hint']), P['Dim'],
         anchor='rm')

    rows_top = bar_y + bar_h + 6
    row_h = 46
    row_right = list_x1 - TM['ring_inset']
    shown = [n for n in NODES_SEARCH if 'era' in n[1].lower()]
    for i, (kind, name, hops, age, snr, fav) in enumerate(shown):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i + 1 < len(shown):
            hline(d, list_x0, row_right, y1, P['RowLine'])
        col = P['Client'] if kind == 'client' else P['Green']
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=col)
        text(d, (list_x0 + 24, y0 + 10), name, font(FONT_SANS, 16), P['Ink'])
        meta = font(FONT_MONO, TM['f_meta'])
        text(d, (list_x0 + 24, y0 + 29), hops, meta, P['Dimmer'])
        text(d, (row_right - 74, y0 + 11), age, meta, P['Dim'], anchor='ra')
        text(d, (row_right - 6, y0 + 29), snr + ' dB', meta, P['Dim'], anchor='ra')
        if fav:
            text(d, (row_right - 6, y0 + 11), '★', meta, P['Acc'], anchor='ra')

    # detail pane: the first match (the list's own order)
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), shown[0][1], font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'client · key ' + NODE_KEY + ' · favourite', fm, P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in (('TYPE', 'client (companion)'), ('HOPS', 'direct · 0 hops'),
                         ('SNR / RSSI', '4.1 dB / -71 dBm'), ('LAST HEARD', '8 m ago'),
                         ('PUBLIC KEY', NODE_KEY), ('PATH', 'direct')):
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28

    hint_bar(d, 'nodes', [('type', 'search'), ('↓', 'list'), ('esc', 'clear/back')])
    return save(img, '2026-11-19-nodes-search.png')


# S514/EST-455: the favourites preview. Two favourites at the top (TEST-Vera, RPT-Kamera), then the rest by
# last heard; the menu is open on the THIRD node (TEST-B2, a client, so all four actions are honest).
NODES_FAV = [
    ('client', 'TEST-Vera', 'direct', '8m', '4.1', True),
    ('repeater', 'RPT-Kamera', 'path 2', '18m', '7.5', True),
    ('client', 'TEST-B2', 'direct', '49m', '6.1', False),
    ('repeater', 'RPT-Vzhod', 'path 3', '9h', '-1.4', False),
    ('repeater', 'RPT-Zahod', 'path 3', '9h', '0.2', False),
    ('client', 'LOCAL-KB', 'direct', '2d', '4.4', False),
]


def nodes_favourites():
    """OM Lite > Nodes, S514/EST-455: favourites on top, and one node's menu.

    The two favourites carry the SAME mark the device draws: a small gold DOT, because the generated
    faces (tools/mkfonts.py) carry ASCII + U+00A0..U+017F only and have no U+2605 star. The menu is the
    bottom sheet in the confirm sheet's house style: scrim, gold outline, the node's name and facts, and
    only the rows that work today. The focused row is Direct message (a client).
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Nodes', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), '6 nodes heard · 2 in the last hour', fm, P['Dim'],
         anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    bar_x1 = list_x1 - TM['ring_inset']
    bar_y, bar_h = work_top + 4, 30
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], fill=P['FieldBg'])
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], outline=P['Line'], width=1)
    text(d, (list_x0 + 12, bar_y + bar_h // 2), 'search nodes', font(FONT_SANS, TM['f_body']),
         P['Dimmer'], anchor='lm')
    text(d, (bar_x1 - 12, bar_y + bar_h // 2), '6 of 6', font(FONT_MONO, TM['f_hint']), P['Dim'],
         anchor='rm')

    rows_top = bar_y + bar_h + 6
    row_h = 46
    row_right = list_x1 - TM['ring_inset']
    focus_row = 2          # the third node: its menu is the sheet below
    for i, (kind, name, hops, age, snr, fav) in enumerate(NODES_FAV):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == focus_row:
            focus_band(d, list_x0, list_x1, y0, y1)
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])
        col = P['Client'] if kind == 'client' else P['Green']
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=col)
        nc = P['AccInk'] if i == focus_row else P['Ink']
        text(d, (list_x0 + 24, y0 + 10), name, font(FONT_SANS, 16), nc)
        meta = font(FONT_MONO, TM['f_meta'])
        text(d, (list_x0 + 24, y0 + 29), hops, meta, P['Dimmer'])
        text(d, (row_right - 6, y0 + 11), age, meta, P['Dim'], anchor='ra')
        text(d, (row_right - 6, y0 + 29), snr + ' dB', meta, P['Dim'], anchor='ra')
        if fav:
            # the device's mark: a 3 px gold dot, in the name's reserve, clear of the age column
            d.ellipse([row_right - 77, y0 + 13, row_right - 71, y0 + 19], fill=P['Acc'])

    scroll_indicator(d, (list_x0, rows_top, list_x1, rows_top + len(NODES_FAV) * row_h),
                     total=6, visible=len(NODES_FAV))

    # detail pane: the focused (third) node
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), 'TEST-B2', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'client · key ' + NODE_KEY, fm, P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in (('TYPE', 'client (companion)'), ('HOPS', 'direct · 0 hops'),
                         ('SNR / RSSI', '6.1 dB / -74 dBm'), ('LAST HEARD', '49 m ago'),
                         ('PUBLIC KEY', NODE_KEY), ('PATH', 'direct')):
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28
    yy += 8
    text(d, (x, yy), 'Enter: actions for this node.', font(FONT_SANS, TM['f_body']), P['Dimmer'])
    text(d, (x, yy + 22), 'f: favourite. DM from the menu or the Chat page.',
         font(FONT_SANS, TM['f_body']), P['Dimmer'])

    # ---- the node menu sheet (the confirm sheet's house style) ----
    img = Image.blend(img, Image.new('RGB', img.size, '#000000'), 0.62)
    d = ImageDraw.Draw(img)
    sheet_w, row_h, head_h, spad = 560, 42, 64, 16
    rows = [('Direct message', ''), ('Info', ''), ('Favourite', 'off'), ('Remove contact', '')]
    sheet_h = 2 * spad + head_h + len(rows) * row_h
    x0 = (W - sheet_w) // 2
    x1 = x0 + sheet_w
    y0 = H - TM['hint_h'] - 8 - sheet_h
    plate(d, [x0, y0, x1, y0 + sheet_h], P['Panel'], P['Acc'], r=0, w=2)
    text(d, (x0 + 20, y0 + spad), 'TEST-B2', font(FONT_SANS, 22), P['Ink'])
    text(d, (x0 + 20, y0 + spad + 34), 'client · last heard 49m ago', fm, P['Dim'])
    hline(d, x0 + 1, x1 - 1, y0 + spad + head_h - 1, P['Line'])
    ry = y0 + spad + head_h
    for i, (label, value) in enumerate(rows):
        if i == 0:
            focus_band(d, x0 + 8, x1 - 8, ry + 1, ry + row_h - 2)
        text(d, (x0 + 24, ry + row_h // 2), label, font(FONT_SANS, 17),
             P['AccInk'] if i == 0 else P['Ink'], anchor='lm')
        if value:
            text(d, (x1 - 24, ry + row_h // 2), value, font(FONT_MONO, TM['f_hint']), P['Dim'],
                 anchor='rm')
        if i + 1 < len(rows):
            hline(d, x0 + 8, x1 - 8, ry + row_h - 1, P['RowLine'])
        ry += row_h

    hint_bar(d, 'nodes', [('↑↓', 'move'), ('Enter', 'choose'), ('esc', 'close')])
    return save(img, '2026-11-19-nodes-favourites.png')


# S514/EST-456: the trace result, 3 hops. One line per hop with the 2-byte hop hash (or the repeater's name)
# and its SNR, then the SNR at this node, then the total — the L2's TraceView shape.
NODE_TRACE_HOPS = [
    ('RPT-Kamera', '7.0'),
    ('2e33', '11.5'),
    ('RPT-Vzhod', '2.0'),
]


def node_trace():
    """OM Lite > Nodes, S514/EST-456: a 3-hop trace's result.

    The list and the detail pane are the Nodes page behind the sheet; the sheet is the trace result (the L2's
    TraceView): every hop's name (or its 2-byte hash when no repeater matches) with its SNR x 4 / 4, the SNR at
    this node, and the total with the round trip. Fixtures invented (rule 9).
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Nodes', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), '6 nodes heard · 2 in the last hour', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    bar_x1 = list_x1 - TM['ring_inset']
    bar_y, bar_h = work_top + 4, 30
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], fill=P['FieldBg'])
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], outline=P['Line'], width=1)
    text(d, (list_x0 + 12, bar_y + bar_h // 2), 'search nodes', font(FONT_SANS, TM['f_body']),
         P['Dimmer'], anchor='lm')
    text(d, (bar_x1 - 12, bar_y + bar_h // 2), '6 of 6', font(FONT_MONO, TM['f_hint']), P['Dim'],
         anchor='rm')

    rows_top = bar_y + bar_h + 6
    row_h = 46
    row_right = list_x1 - TM['ring_inset']
    for i, (kind, name, hops, age, snr, fav) in enumerate(NODES_FAV):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        hline(d, list_x0, row_right, y1, P['RowLine'])
        col = P['Client'] if kind == 'client' else P['Green']
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=col)
        text(d, (list_x0 + 24, y0 + 10), name, font(FONT_SANS, 16), P['Ink'])
        text(d, (list_x0 + 24, y0 + 29), hops, fm, P['Dimmer'])
        text(d, (row_right - 6, y0 + 11), age, fm, P['Dim'], anchor='ra')
        text(d, (row_right - 6, y0 + 29), snr + ' dB', fm, P['Dim'], anchor='ra')
        if fav:
            d.ellipse([row_right - 77, y0 + 13, row_right - 71, y0 + 19], fill=P['Acc'])

    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), 'RPT-Zahod', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'repeater · key 4b 91 2e … c7', fm, P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in (('TYPE', 'repeater'), ('HOPS', '3 bytes of path'), ('SNR / RSSI', '0.2 dB / -96 dBm'),
                         ('LAST HEARD', '9h ago'), ('PUBLIC KEY', '4b 91 2e … c7'), ('PATH', 'via a repeater')):
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28
    yy += 8
    text(d, (x, yy), 'Enter: actions for this node.', font(FONT_SANS, TM['f_body']), P['Dimmer'])
    text(d, (x, yy + 22), 'f: favourite. DM from the menu or the Chat page.',
         font(FONT_SANS, TM['f_body']), P['Dimmer'])

    # ---- the trace result sheet, over the page ----
    img = Image.blend(img, Image.new('RGB', img.size, '#000000'), 0.62)
    d = ImageDraw.Draw(img)
    sheet_w, row_h, head_h, spad = 560, 42, 64, 16
    rows = [(name, 'SNR ' + snr) for name, snr in NODE_TRACE_HOPS]
    rows.append(('at this node', 'SNR -6.2'))
    rows.append(('3 hops · RTT 2.4 s', ''))
    sheet_h = 2 * spad + head_h + len(rows) * row_h
    x0 = (W - sheet_w) // 2
    x1 = x0 + sheet_w
    y0 = H - TM['hint_h'] - 8 - sheet_h
    plate(d, [x0, y0, x1, y0 + sheet_h], P['Panel'], P['Acc'], r=0, w=2)
    text(d, (x0 + 20, y0 + spad), 'RPT-Zahod', font(FONT_SANS, 22), P['Ink'])
    text(d, (x0 + 20, y0 + spad + 34), '3 hops · RTT 2.4 s', fm, P['Dim'])
    hline(d, x0 + 1, x1 - 1, y0 + spad + head_h - 1, P['Line'])
    ry = y0 + spad + head_h
    for i, (label, value) in enumerate(rows):
        col = P['Dimmer'] if i == len(rows) - 1 else P['Ink']
        text(d, (x0 + 24, ry + row_h // 2), label, font(FONT_SANS, 17), col, anchor='lm')
        if value:
            text(d, (x1 - 24, ry + row_h // 2), value, font(FONT_MONO, TM['f_hint']), P['Dim'], anchor='rm')
        if i + 1 < len(rows):
            hline(d, x0 + 8, x1 - 8, ry + row_h - 1, P['RowLine'])
        ry += row_h

    hint_bar(d, 'nodes', [('Enter', 'menu'), ('f', 'favourite'), ('esc', 'close')])
    return save(img, '2026-11-19-node-trace.png')


CHATS = [
    ('#', 'test-a', 'TEST-A1: morning, radio check', '3m', 2),
    ('#', 'test-b', 'LOCAL-KB: history save check', '12m', 0),
    ('@', 'TEST-A1', 'Direct: got the message, thanks', '1h', 1),
    ('#', 'public', 'no messages yet', '—', 0),
]


def chat():
    """OM Lite > Chat: the chat list left, the open chat right, composer at the pane's floor.

    The typing goes straight into the composer from the physical keyboard (BRIEF § 6), so there is no
    on-screen keyboard at all: the pane that the L2 gives to the OSK is given to messages here.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Chat', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), '2 channels · 1 DM · 3 unread', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    fl = font(FONT_MONO, TM['f_label'])
    spaced(d, (list_x0, work_top + 16), 'CHANNELS', fl, P['Dim'])
    text(d, (list_x1 - 12, work_top + 16), '4 chats', font(FONT_MONO, TM['f_label']),
         P['Dimmer'], anchor='ra')

    rows_top = work_top + 38
    row_h = 62
    for i, (mark, name, preview, age, unread) in enumerate(CHATS):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == 0:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, list_x1 - TM['ring_inset'], y1, P['RowLine'])
        nc = P['AccInk'] if i == 0 else P['Ink']
        right = list_x1 - TM['ring_inset']
        text(d, (list_x0 + 4, y0 + 12), mark + name, font(FONT_SANS, 16), nc)
        text(d, (right, y0 + 13), age, font(FONT_MONO, TM['f_meta']), P['Dim'], anchor='ra')
        text(d, (list_x0 + 4, y0 + 34), preview, font(FONT_MONO, TM['f_meta']),
             P['Dim'] if unread else P['Dimmer'])
        if unread:
            fb = font(FONT_MONO, 11)
            bw = tw(fb, str(unread)) + 12
            plate(d, [right - bw, y0 + 36, right, y0 + 54], P['Bad'], None, r=8)
            text(d, (right - bw / 2, y0 + 45), str(unread), fb, '#ffffff', anchor='mm')

    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])

    # open chat: header, messages, composer
    x = list_x1 + 8 + 24
    rw = W - pad - x
    yy = work_top + 16
    text(d, (x, yy), '# test-a', font(FONT_SANS, 20), P['Ink'])
    text(d, (x + rw, yy + 4), 'ADVERT', font(FONT_MONO, TM['f_label']), P['Acc'], anchor='ra')
    yy += 34
    hline(d, x - 24, W - pad, yy, P['Line'])
    yy += 14

    msgs = [
        ('09:12', 'TEST-A1', 'Morning all, the north repeater is back up.', False),
        ('09:14', 'LOCAL-KB', 'Good, path 3 now instead of 4.', False),
        ('09:20', 'TEST-A1', 'Morning! Coffee is on. š č ž', False),
        ('09:41', 'you', 'Testing from the Tanmatsu keyboard.', True),
    ]
    fname = font(FONT_SANS, TM['f_body'])
    fbody = font(FONT_SANS, TM['f_body'])
    fstamp = font(FONT_MONO, TM['f_meta'])
    for stamp, who, body, mine in msgs:
        text(d, (x, yy), stamp, fstamp, P['Dimmer'])
        text(d, (x + 60, yy), who, fname, P['Acc'] if mine else P['Ink'])
        text(d, (x + rw, yy), '4h' if not mine else 'now', fstamp, P['Dimmer'], anchor='ra')
        yy += 20
        for line in wrap(d, body, fbody, rw - 60):
            text(d, (x + 60, yy), line, fbody, P['Ink'])
            yy += 20
        yy += 14

    # composer at the pane's floor
    ch = H - TM['hint_h'] - 64
    hline(d, x - 24, W - pad, ch - 12, P['Line'])
    plate(d, [x, ch, x + rw - 64, ch + 40], P['FieldBg'], P['Line'], r=4)
    d.rectangle([x + 12, ch + 12, x + 13, ch + 28], fill=P['Acc'])   # caret, gold like the L2's
    text(d, (x + 22, ch + 20), 'message the channel, Enter sends', font(FONT_MONO, TM['f_meta']),
         P['Dimmer'], anchor='lm')
    # S514/EST-444: the Enter key cap in the hint bar's style, not a filled "Send" button; the draft is
    # empty here, so the cap is dim.
    key_cap(d, [x + rw - 84, ch + 6, x + rw - 8, ch + 34], '↵ Enter', P['Dimmer'],
            font(FONT_SANS, TM['f_hint']))

    hint_bar(d, 'chat', [('↑↓', 'move'), ('Enter', 'open'), ('r', 'reply'), ('esc', 'back')])
    return save(img, '2026-11-13-chat.png')


# --------------------------------------------------------------------------------------- chat + emoji
def chat_emoji():
    """S514/EST-457: the open chat with emoji from the app's SUBSET (shots/2026-11-19-chat-emoji.png).

    `Radio check 👍🔥🗿` is the ticket's line: two glyphs the subset holds (drawn at 2x from the same .bin the
    app embeds) and the moai, which it does not — that one keeps today's behaviour and leaves its half-em gap.
    Emoji also appear in a card preview and in the composer echo, the other two surfaces the ticket names.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Chat', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), '2 channels · 1 DM · 3 unread', font(FONT_MONO, TM['f_meta']),
         P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    fl = font(FONT_MONO, TM['f_label'])
    spaced(d, (list_x0, work_top + 16), 'CHANNELS', fl, P['Dim'])
    text(d, (list_x1 - 12, work_top + 16), '4 chats', font(FONT_MONO, TM['f_label']), P['Dimmer'], anchor='ra')

    rows_top = work_top + 38
    row_h = 62
    preview_font = font(FONT_MONO, TM['f_meta'])
    chats = [
        ('# ', 'test-a', 'TEST-A1: radio check 👍', '09:38', 2),
        ('# ', 'test-b', 'LOCAL-KB: history save check', '09:41', 0),
        ('# ', 'public', 'no messages yet', '', 0),
        ('@ ', 'Don\'t Panic', 'see you on the trail 🔥', '08:12', 1),
    ]
    for i, (mark, name, preview, age, unread) in enumerate(chats):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == 0:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, list_x1 - TM['ring_inset'], y1, P['RowLine'])
        nc = P['AccInk'] if i == 0 else P['Ink']
        right = list_x1 - TM['ring_inset']
        text(d, (list_x0 + 4, y0 + 12), mark + name, font(FONT_SANS, 16), nc)
        text(d, (right, y0 + 13), age, font(FONT_MONO, TM['f_meta']), P['Dim'], anchor='ra')
        text_mixed(d, (list_x0 + 4, y0 + 34), preview, preview_font, P['Dim'] if unread else P['Dimmer'])
        if unread:
            fb = font(FONT_MONO, 11)
            bw = tw(fb, str(unread)) + 12
            plate(d, [right - bw, y0 + 36, right, y0 + 54], P['Bad'], None, r=8)
            text(d, (right - bw / 2, y0 + 45), str(unread), fb, '#ffffff', anchor='mm')

    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])

    x = list_x1 + 8 + 24
    rw = W - pad - x
    yy = work_top + 16
    text(d, (x, yy), '# test-a', font(FONT_SANS, 20), P['Ink'])
    text(d, (x + rw, yy + 4), 'ADVERT', font(FONT_MONO, TM['f_label']), P['Acc'], anchor='ra')
    yy += 34
    hline(d, x - 24, W - pad, yy, P['Line'])
    yy += 14

    msgs = [
        ('09:12', 'TEST-A1', 'Morning all, the north repeater is back up 👍', False),
        ('09:20', 'TEST-A1', 'Morning! Coffee is on ☕ š č ž', False),
        ('09:38', 'TEST-A1', 'Radio check 👍🔥🗿', False),
        ('09:41', 'you', 'Testing from the Tanmatsu keyboard ❤️', True),
    ]
    fname = font(FONT_SANS, TM['f_body'])
    fbody = font(FONT_SANS, TM['f_body'])
    fstamp = font(FONT_MONO, TM['f_meta'])
    for stamp, who, body, mine in msgs:
        text(d, (x, yy), stamp, fstamp, P['Dimmer'])
        text(d, (x + 60, yy), who, fname, P['Acc'] if mine else P['Ink'])
        text(d, (x + rw, yy), 'now' if mine else '2h', fstamp, P['Dimmer'], anchor='ra')
        yy += 20
        for line in wrap_mixed(body, fbody, rw - 60):
            text_mixed(d, (x + 60, yy), line, fbody, P['Ink'])
            yy += 20
        yy += 14

    # composer at the pane's floor, holding text with an emoji: the echo draws it too
    ch = H - TM['hint_h'] - 64
    hline(d, x - 24, W - pad, ch - 12, P['Line'])
    plate(d, [x, ch, x + rw - 64, ch + 40], P['FieldBg'], P['Line'], r=4)
    draft = 'nice one 🔥'
    text_mixed(d, (x + 12, ch + 20), draft, fbody, P['Ink'], anchor='lm')
    d.rectangle([x + 12 + mixed_width(draft, fbody) + 3, ch + 12,
                 x + 13 + mixed_width(draft, fbody) + 3, ch + 28], fill=P['Acc'])
    key_cap(d, [x + rw - 84, ch + 6, x + rw - 8, ch + 34], '↵ Enter', P['Ink'],
            font(FONT_SANS, TM['f_hint']))

    hint_bar(d, 'chat', [('↑↓', 'move'), ('Enter', 'open'), ('r', 'reply'), ('esc', 'back')])
    return save(img, '2026-11-19-chat-emoji.png')


# ------------------------------------------------------------------------------------------- chat cards
# The device's own grid arithmetic (core/chat_cards.cpp::cardGrid), mirrored so the render is at the same numbers.
def card_grid(box, gutter, header_h, card_h, gap):
    x0, y0, x1, y1 = box
    col_w = (x1 - x0 - gutter) // 2
    return dict(x0=x0, y0=y0, x1=x1, y1=y1, col_w=col_w, gutter=gutter, header_h=header_h,
                card_h=card_h, gap=gap, col_x=[x0, x0 + col_w + gutter], cards_y=y0 + header_h,
                visible=max(1, ((y1 - y0) - header_h + gap) // (card_h + gap)))


def draw_channel_card(d, g, col, row, scroll, name, preview, stamp, unread, focused):
    x = g['col_x'][col]
    y = g['cards_y'] + (row - scroll) * (g['card_h'] + g['gap'])
    w, h = g['col_w'], g['card_h']
    if focused:
        plate(d, [x, y, x + w - 1, y + h - 1], P['SelBg'], P['Acc'], r=TM['card_radius'], w=TM['ring_w'])
        name_col = P['AccInk']
    else:
        plate(d, [x, y, x + w - 1, y + h - 1], P['Panel'], P['Line'], r=TM['card_radius'], w=1)
        name_col = P['Ink']

    fn = font(FONT_SANS, TM['f_row'])
    text(d, (x + 12, y + 9), name, fn, name_col)
    if unread:
        fb = font(FONT_MONO, 11)
        label = str(min(unread, 9))
        bw = max(18, tw(fb, label) + 12)
        plate(d, [x + w - 12 - bw, y + 11, x + w - 12, y + 11 + bw], P['Bad'], None, r=bw // 2)
        text(d, (x + w - 12 - bw / 2, y + 11 + bw / 2), label, fb, '#ffffff', anchor='mm')

    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (x + 12, y + 40), preview, fm, P['Dim'])
    if stamp:
        text(d, (x + w - 12, y + 41), stamp, font(FONT_MONO, TM['f_hint']), P['Dimmer'], anchor='ra')


CHANNEL_CARDS = [
    # name, the newest message as "sender: text", its local HH:MM, unread
    ('test-a', 'TEST-A1: morning, radio check', '09:12', 2),
    ('test-b', 'LOCAL-KB: history save check', '09:41', 0),
    ('public', 'no messages yet', '', 0),
]


def chat_cards():
    """OM Lite > Chat, LIST state: two columns of cards, Channels left and Direct messages right.

    Filip (S513): "make channels like rectangles in two columns, so first column is channels, the second is
    DMs." One card per chat: the name, the unread badge, then the newest message and its local time. The DM
    column is EMPTY — this app has no direct messages yet, so it shows the L2's own line and no card. Focus is
    on the second channel, drawn with the app's 2 px gold outline.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Chat', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), '3 channels · 2 unread', font(FONT_MONO, TM['f_meta']),
         P['Dimmer'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    g = card_grid((pad, y + TM['head_h'], W - pad, H - TM['hint_h']),
                  TM['card_gutter'], TM['card_header_h'], TM['card_h'], TM['card_gap'])

    fh = font(FONT_SANS, TM['f_meta'])
    text(d, (g['col_x'][0], g['y0'] + 7), 'Channels', fh, P['Dimmer'])
    text(d, (g['col_x'][1], g['y0'] + 7), 'Direct messages', fh, P['Dimmer'])

    for i, (name, preview, stamp, unread) in enumerate(CHANNEL_CARDS):
        draw_channel_card(d, g, 0, i, 0, name, preview, stamp, unread, focused=(i == 1))

    # The DM column: no cards, the L2's empty line in the dim colour (firmware/src/ui/chat_selector.cpp).
    text(d, (g['col_x'][1] + 12, g['cards_y'] + 12), 'No direct messages yet',
         font(FONT_SANS, TM['f_body']), P['Dim'])

    hint_bar(d, 'chat', [('Enter', 'open'), ('esc', 'back'), ('↑↓', 'move'), ('←→', 'column'), (',', 'settings')])
    return save(img, '2026-11-18-chat-two-columns.png')


DM_CARDS = [
    # name, the newest message, its local HH:MM, unread
    ('Alpha', 'see you at the repeater at six', '09:48', 3),
    ('Mira', 'got the map pack, thanks', '09:31', 0),
    ('dm-3f7a01c2', 'hello from an old log', '08:55', 1),
    ('Tomaž', 'the path came back, 2 hops', '08:12', 0),
]


def chat_dm_column():
    """OM Lite > Chat, LIST state with the DM column populated (S513/EST-434, items 5 and 6).

    Same two-column grid as 2026-11-18-chat-two-columns.png, but the right column now carries the threads the RAM
    store holds: newest first, the contact's name (or the 8-hex prefix of a restored thread), the unread badge,
    the newest text and its LOCAL HH:MM. Focus is on the second DM card, ringed in gold.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Chat', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), '3 channels · 2 unread · 4 DM threads',
         font(FONT_MONO, TM['f_meta']), P['Dimmer'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    g = card_grid((pad, y + TM['head_h'], W - pad, H - TM['hint_h']),
                  TM['card_gutter'], TM['card_header_h'], TM['card_h'], TM['card_gap'])

    fh = font(FONT_SANS, TM['f_meta'])
    text(d, (g['col_x'][0], g['y0'] + 7), 'Channels', fh, P['Dimmer'])
    text(d, (g['col_x'][1], g['y0'] + 7), 'Direct messages', fh, P['Dimmer'])

    for i, (name, preview, stamp, unread) in enumerate(CHANNEL_CARDS):
        draw_channel_card(d, g, 0, i, 0, name, preview, stamp, unread, focused=False)

    for i, (name, preview, stamp, unread) in enumerate(DM_CARDS):
        draw_channel_card(d, g, 1, i, 0, name, preview, stamp, unread, focused=(i == 1))

    hint_bar(d, 'chat', [('Enter', 'open'), ('esc', 'back'), ('↑↓', 'move'), ('←→', 'column'), (',', 'settings')])
    return save(img, '2026-11-18-chat-dm-column.png')


# ---------------------------------------------------------------------------------------------- map
# Rule 9: this map is an INVENTED TEST GRID. No real geography, no coordinates, no place names — the
# labels are the same fake node names the other previews use, and the grid is drawn from scratch.
MAP_MARKERS = [
    # name, fraction of the map box, kind, selected, when it was last heard
    ('TEST-A1', 0.33, 0.58, 'client', True, 'last 14 h'),
    ('RPT-Sever', 0.66, 0.24, 'repeater', False, 'last 18 m'),
    ('RPT-Jug', 0.24, 0.72, 'repeater', False, 'last 49 m'),
]


def map_page():
    """OM Lite > Map at 800 px: the tile area left, the marker/trace panel right.

    The "expanded" step of BRIEF § 4: the L2's full-width map gets a side panel here instead of a
    separate push, and the keyboard keys are shown where the touch gestures were (pan with the arrows,
    zoom with +/-). Same palette, same focus ring, same gold for "this is the selected marker".
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Map', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), 'offline · tiles.pack · no GPS', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    panel_w = 232
    mx0, my0 = pad, work_top + 8
    mx1, my1 = W - pad - panel_w, H - TM['hint_h'] - 12
    d.rectangle([mx0, my0, mx1, my1], fill=P['Panel'])
    d.rectangle([mx0, my0, mx1, my1], outline=P['Line'], width=1)

    # the invented grid: cell lines, a few blocks, and two "roads" — nothing here is a real place
    step = 34
    gx = mx0 + 12
    while gx < mx1:
        vline(d, gx, my0 + 1, my1 - 1, P['Line'])
        gx += step
    gy = my0 + 12
    while gy < my1:
        hline(d, mx0 + 1, mx1 - 1, gy, P['Line'])
        gy += step
    for bx, by_, bw_, bh_ in ((0.08, 0.12, 0.16, 0.12), (0.30, 0.55, 0.14, 0.16),
                              (0.62, 0.62, 0.20, 0.12), (0.70, 0.10, 0.12, 0.14)):
        d.rectangle([mx0 + (mx1 - mx0) * bx, my0 + (my1 - my0) * by_,
                     mx0 + (mx1 - mx0) * (bx + bw_), my0 + (my1 - my0) * (by_ + bh_)], fill=P['Panel2'])
    d.line([(mx0 + 20, my0 + (my1 - my0) * 0.62), (mx0 + (mx1 - mx0) * 0.44, my0 + (my1 - my0) * 0.50),
            (mx0 + (mx1 - mx0) * 0.78, my0 + (my1 - my0) * 0.40), (mx1 - 20, my0 + (my1 - my0) * 0.47)],
           fill=P['Dim'], width=2, joint='curve')

    def pt(fx, fy):
        return (mx0 + (mx1 - mx0) * fx, my0 + (my1 - my0) * fy)

    # the trace: the selected node's path through its last two hops, gold like the L2's trace
    a, b, c = pt(*MAP_MARKERS[0][1:3]), pt(*MAP_MARKERS[1][1:3]), pt(*MAP_MARKERS[2][1:3])
    d.line([b, a], fill=P['Acc'], width=2)
    d.line([a, c], fill=P['Acc'], width=2)
    for p in (b, a, c):
        d.ellipse([p[0] - 3, p[1] - 3, p[0] + 3, p[1] + 3], fill=P['Acc'])

    # markers; the selected one carries the gold focus ring, the others only their type colour
    for name, fx, fy, kind, sel, _age in MAP_MARKERS:
        px, py = pt(fx, fy)
        col = P['Client'] if kind == 'client' else P['Green']
        if sel:
            d.ellipse([px - 11, py - 11, px + 11, py + 11], outline=P['Acc'], width=TM['ring_w'])
        d.ellipse([px - 6, py - 6, px + 6, py + 6], fill=col, outline=P['Screen'], width=2)
        text(d, (px + 16, py - 7), name, font(FONT_MONO, TM['f_meta']),
             P['AccInk'] if sel else P['Dim'])

    # centre crosshair (dim: the arrows move this) and the two honest captions
    cx, cy = (mx0 + mx1) // 2, (my0 + my1) // 2
    hline(d, cx - 10, cx - 4, cy, P['Dim'])
    hline(d, cx + 4, cx + 10, cy, P['Dim'])
    vline(d, cx, cy - 10, cy - 4, P['Dim'])
    vline(d, cx, cy + 4, cy + 10, P['Dim'])
    text(d, (mx0 + 12, my1 - 40), 'invented test grid', font(FONT_MONO, TM['f_label']), P['Dimmer'])
    hline(d, mx0 + 12, mx0 + 72, my1 - 20, P['Dim'])
    vline(d, mx0 + 12, my1 - 24, my1 - 16, P['Dim'])
    vline(d, mx0 + 72, my1 - 24, my1 - 16, P['Dim'])
    text(d, (mx0 + 80, my1 - 26), '200 m', font(FONT_MONO, TM['f_label']), P['Dim'])
    plate(d, [mx1 - 108, my1 - 38, mx1 - 12, my1 - 12], P['Panel2'], P['Line'], r=4)
    text(d, ((mx1 - 60), my1 - 25), '−   12   +', font(FONT_MONO, TM['f_meta']), P['Dim'],
         anchor='mm')

    # side panel: the markers, then the trace, then the one rule that matters on this device
    sx0, sx1 = mx1 + 16, W - pad
    spaced(d, (sx0, work_top + 16), 'MARKERS', font(FONT_MONO, TM['f_label']), P['Dim'])
    text(d, (sx1, work_top + 16), '3 shown', font(FONT_MONO, TM['f_label']), P['Dimmer'], anchor='ra')
    rows_top = work_top + 40
    row_h = 52
    for i, (name, fx, fy, kind, sel, age) in enumerate(MAP_MARKERS):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if sel:
            focus_band(d, sx0, sx1, y0, y1)
        else:
            hline(d, sx0, sx1 - TM['ring_inset'], y1, P['RowLine'])
        right = sx1 - TM['ring_inset']
        d.ellipse([sx0 + 6, y0 + 15, sx0 + 14, y0 + 23],
                  fill=P['Client'] if kind == 'client' else P['Green'])
        text(d, (sx0 + 24, y0 + 8), name, font(FONT_SANS, 16),
             P['AccInk'] if sel else P['Ink'])
        text(d, (right, y0 + 8), age, font(FONT_MONO, TM['f_label']),
             P['Acc'] if sel else P['Dimmer'], anchor='ra')
        text(d, (sx0 + 24, y0 + 28), 'client' if kind == 'client' else 'repeater',
             font(FONT_MONO, TM['f_meta']), P['Dimmer'])

    ty = rows_top + len(MAP_MARKERS) * row_h + 18
    spaced(d, (sx0, ty), 'TRACE', font(FONT_MONO, TM['f_label']), P['Dim'])
    text(d, (sx0, ty + 20), 'TEST-A1 → RPT-Sever', font(FONT_SANS, TM['f_body']), P['Ink'])
    text(d, (sx0, ty + 40), '2 hops · 3 points', font(FONT_MONO, TM['f_meta']), P['Dim'])

    fnote = font(FONT_MONO, TM['f_label'])
    ny = H - TM['hint_h'] - 58
    for line in wrap(d, 'No GPS: set a position by hand, or leave it off.', fnote, sx1 - sx0):
        text(d, (sx0, ny), line, fnote, P['Dimmer'])
        ny += 15

    hint_bar(d, 'map', [('↑↓←→', 'pan'), ('+/−', 'zoom'), ('t', 'trace'), ('esc', 'back')])
    return save(img, '2026-11-13-map.png')


# --------------------------------------------------------------------------------------------- rptr
# Invented repeater names (rule 9): the same fake family the Nodes preview uses, never a real callsign.
RPTRS = [
    ('RPT-Sever', '18m', 'path 2', True),
    ('RPT-Jug', '49m', 'path 2', True),
    ('RPT-Vzhod', '9h', 'path 3', False),
]


def keyicon(d, cx, cy, colour):
    """The remembered password, drawn the L2 way: a ring and a stem, never a font glyph."""
    d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], outline=colour, width=2)
    d.line([(cx + 5, cy), (cx + 14, cy)], fill=colour, width=2)
    d.line([(cx + 11, cy), (cx + 11, cy + 5)], fill=colour, width=2)
    d.line([(cx + 14, cy), (cx + 14, cy + 4)], fill=colour, width=2)


def rptr():
    """OM Lite > RPTR: the saved repeaters left, the open session right (status card + pages).

    Scan is manual only (BRIEF § 6) so the list carries one Scan chip and nothing moves on its own; the
    key icon on a row is the remembered password, and the console is the session's last page.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'RPTR', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), 'manual scan · idle · 3 saved', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    row_right = list_x1 - TM['ring_inset']
    spaced(d, (list_x0, work_top + 16), 'REPEATERS', font(FONT_MONO, TM['f_label']), P['Dim'])
    # the Scan chip: manual only, one key, never a spinner that runs by itself
    fchip = font(FONT_MONO, TM['f_label'])
    cw = tw(fchip, 'SCAN  s') + 14
    plate(d, [row_right - cw, work_top + 8, row_right, work_top + 30], P['Panel2'], P['Line'], r=3)
    text(d, (row_right - cw / 2, work_top + 19), 'SCAN  s', fchip, P['Dim'], anchor='mm')

    rows_top = work_top + 44
    row_h = 58
    for i, (name, age, hops, remembered) in enumerate(RPTRS):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == 0:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=P['Green'])
        text(d, (list_x0 + 24, y0 + 10), name, font(FONT_SANS, 16),
             P['AccInk'] if i == 0 else P['Ink'])
        text(d, (list_x0 + 24, y0 + 32), 'last ' + age + ' · ' + hops, font(FONT_MONO, TM['f_meta']),
             P['Dimmer'])
        if remembered:
            keyicon(d, row_right - 26, y0 + row_h // 2, P['Acc'] if i == 0 else P['Dim'])

    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])

    # the open session
    x = list_x1 + 8 + 24
    rw = W - pad - x
    yy = work_top + 16
    text(d, (x, yy), 'RPT-Sever', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'logged in · session 4 m · password remembered', font(FONT_MONO, TM['f_meta']),
         P['Dim'])
    yy += 26

    # the status card: the repeater's own read-back, four cells
    card_h = 96
    plate(d, [x, yy, x + rw, yy + card_h], P['Panel2'], P['Line'], r=6)
    cells = [('UPTIME', '3 d 4 h'), ('BATTERY', '87%'), ('NOISE FLOOR', '-117 dBm'), ('QUEUE', '0')]
    cw2 = rw // 2
    for i, (lab, val) in enumerate(cells):
        cx = x + 16 + (i % 2) * cw2
        cy = yy + (i // 2) * 46
        text(d, (cx, cy + 8), lab, font(FONT_MONO, TM['f_label']), P['Dimmer'])
        text(d, (cx, cy + 24), val, font(FONT_SANS, 16), P['Ink'])
    yy += card_h + 18

    spaced(d, (x, yy), 'PAGES', font(FONT_MONO, TM['f_label']), P['Dim'])
    yy += 22
    pages = [('Name', 'RPT-Sever'), ('Radio', '869.618 · SF 8'), ('Repeat', 'off'),
             ('Regions', '2'), ('Console', 'open')]
    fk = font(FONT_MONO, TM['f_kv'])
    for lab, val in pages:
        hline(d, x, x + rw, yy + 26, P['RowLine'])
        text(d, (x, yy + 5), lab, font(FONT_SANS, TM['f_body']), P['Ink'])
        text(d, (x + rw - 16, yy + 5), val, fk, P['Dim'], anchor='ra')
        text(d, (x + rw, yy + 5), '›', fk, P['Dimmer'], anchor='ra')
        yy += 28

    fnote = font(FONT_MONO, TM['f_label'])
    text(d, (x, yy + 6), 'Console: type a command, Enter sends · Esc backs out.',
         fnote, P['Dimmer'])

    hint_bar(d, 'rptr', [('↑↓', 'move'), ('Enter', 'open'), ('s', 'scan'), ('esc', 'back')])
    return save(img, '2026-11-13-rptr.png')


# ----------------------------------------------------------------------------------------- settings
# EST-426: the row shows the COMMITTED value while the sheet is open. EST-422's rule is that Cancel
# changes NOTHING and only Apply runs the change (ui/settings_actions.h:1-30), so the row behind the
# sheet must still read the OLD value until Apply; showing the new one would mean it already happened.
# Both strings live here, so the row and the question can never disagree again.
TX_POWER_OLD = '22 dBm'      # the L2's seed/SLO value (firmware/src/ui/settings_prefs.h:38,42)
TX_POWER_NEW = '20 dBm'
CONFIRM_Q = 'Change TX power to %s?' % TX_POWER_NEW
CONFIRM_BODY = ('Cancel changes nothing. Apply writes the value to the radio and shows it in the row.')


def sheet(img, question, body):
    """The EST-422 confirmation sheet, at 800 px.

    S515/EST-467 (Filip: "make a default selection 'Cancel' as an extra safety"): **Cancel sits on the LEFT
    and OPENS FOCUSED** — Enter on open cancels and changes NOTHING; Left/Right move the focus to Apply,
    where Enter applies; Esc always cancels. The gold ring shows where Enter will land.
    """
    img = Image.blend(img, Image.new('RGB', img.size, '#000000'), 0.62)
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = 132, 214, 668, 406
    plate(d, [x0, y0, x1, y1], P['Panel'], P['Line'], r=8)
    fq = font(FONT_SANS, 20)
    yy = y0 + 26
    for line in wrap(d, question, fq, x1 - x0 - 48):
        text(d, (x0 + 24, yy), line, fq, P['Ink'])
        yy += 28
    fb = font(FONT_MONO, TM['f_meta'])
    yy += 8
    for line in wrap(d, body, fb, x1 - x0 - 48):
        text(d, (x0 + 24, yy), line, fb, P['Dim'])
        yy += 18

    by = y1 - 62
    bw, bh = 148, 42
    fab = font(FONT_SANS, TM['f_body'])
    # Cancel: the default side, left, a plain plate
    cx = x1 - 24 - 2 * bw - 12
    plate(d, [cx, by, cx + bw, by + bh], P['Panel2'], P['Line'], r=6)
    d.rectangle([cx - 2, by - 2, cx + bw + 2, by + bh + 2], outline=P['Acc'], width=2)   # CANCEL focused
    text(d, (cx + 18, by + bh // 2), 'Cancel', fab, P['Ink'], anchor='lm')
    text(d, (cx + bw - 18, by + bh // 2), 'esc', fb, P['Dim'], anchor='rm')
    # Apply: right, gold, the only filled plate on the screen
    ax = x1 - 24 - bw
    plate(d, [ax, by, ax + bw, by + bh], P['SelBg2'], P['Acc'], r=6)
    text(d, (ax + 18, by + bh // 2), 'Apply', fab, P['AccInk'], anchor='lm')
    text(d, (ax + bw - 18, by + bh // 2), 'Enter', fb, P['Acc'], anchor='rm')
    # the sheet is modal: the hint bar below says so (drawn by the caller, after this)
    return img


def setrow(d, x0, x1, y, label, value, focused=False, chevron=True):
    """One settings row: label left, value right in mono, a chevron for a sub-page, and the L2's row
    line. `focused` draws the gold ring on the row's own rectangle."""
    h = 34
    if focused:
        focus_band(d, x0, x1, y, y + h)
    else:
        hline(d, x0, x1 - TM['ring_inset'], y + h, P['RowLine'])
    right = x1 - TM['ring_inset']
    text(d, (x0 + 4, y + 9), label, font(FONT_SANS, TM['f_body']),
         P['AccInk'] if focused else P['Ink'])
    fk = font(FONT_MONO, TM['f_kv'])
    vx = right - (18 if chevron else 4)
    text(d, (vx, y + 9), value, fk, P['Acc'] if focused else P['Dim'], anchor='ra')
    if chevron:
        text(d, (right, y + 9), '›', fk, P['Acc'] if focused else P['Dimmer'], anchor='ra')
    return y + h + 2


def settings():
    """OM Lite > Settings: the L2's five tabs, the EST-422 confirm rule, one page per tab.

    The sheet is *shown open* so Filip can approve its look before any code: it is the same sheet for
    every important setting (radio region/preset, TX power, path hash, node name, the Wi-Fi/BT/keyboard
    switches, every Delete/Forget), so one approval covers the rule (ui/settings_actions.h:1-30).
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d, clock='22:31')

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Settings', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), 'general · saved', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    # the five tabs, the L2's own row (General / Nodes / Chat / Map / RPTR), current = gold underline
    tabs = ['General', 'Nodes', 'Chat', 'Map', 'RPTR']
    tab_h = 30
    ft = font(FONT_MONO, TM['f_label'])
    for i, name in enumerate(tabs):
        tx0 = W * i // len(tabs)
        tx1 = W * (i + 1) // len(tabs)
        on = (i == 0)
        text(d, ((tx0 + tx1) // 2, work_top + tab_h // 2), name, ft, P['Acc'] if on else P['Dimmer'],
             anchor='mm')
        if on:
            hline(d, tx0 + 18, tx1 - 18, work_top + tab_h - 3, P['Acc'])
            d.line([(tx0 + 18, work_top + tab_h - 2), (tx1 - 18, work_top + tab_h - 2)], fill=P['Acc'])
    hline(d, 0, W, work_top + tab_h, P['Line'])
    top = work_top + tab_h + 16

    def section(xx, yy, label):
        spaced(d, (xx, yy), label, font(FONT_MONO, TM['f_label']), P['Dim'])
        return yy + 20

    lx0, lx1 = pad, 392
    rx0, rx1 = 412, W - pad

    yy = section(lx0, top, 'RADIO')
    yy = setrow(d, lx0, lx1, yy, 'Region / preset', 'SLO · 869.618 / 62.5 · SF 8')
    yy = setrow(d, lx0, lx1, yy, 'TX power', TX_POWER_OLD, focused=True)
    yy = setrow(d, lx0, lx1, yy, 'Path hash', '2 B')
    yy = section(lx0, yy + 14, 'NETWORK')
    yy = setrow(d, lx0, lx1, yy, 'Wi-Fi', 'off')
    yy = setrow(d, lx0, lx1, yy, 'Bluetooth', 'off')

    yy = section(rx0, top, 'IDENTITY')
    yy = setrow(d, rx0, rx1, yy, 'Node name', 'TEST-01')
    yy = setrow(d, rx0, rx1, yy, 'Public key', 'a1 b2 c3 ... 9f')
    yy = section(rx0, yy + 14, 'TIME')
    yy = setrow(d, rx0, rx1, yy, 'Time source', 'mesh · NTP')
    yy = setrow(d, rx0, rx1, yy, 'Clock', '11:23')
    yy = section(rx0, yy + 14, 'KEYBOARD')
    setrow(d, rx0, rx1, yy, 'Layout', 'QWERTY · 69 keys')

    img = sheet(img, CONFIRM_Q, CONFIRM_BODY)
    hint_bar(ImageDraw.Draw(img), 'settings',
             [('l/r', 'choose'), ('Enter', 'apply/cancel'), ('esc', 'cancel')])
    return save(img, '2026-11-13-settings.png')


# ------------------------------------------------------------------------------------------- start menu
# The start screen (S513/EST-432): three app cards, one per module, in Filip's names and order. Invented
# numbers, as everywhere in this file: the memory and uptime are placeholders, not a device reading.
MENU_CARDS = [
    dict(name='OM Lite', desc='mesh companion', state='RUNNING',
         memory='4.0 KB requested', uptime='up 1 h 12 m'),
    dict(name='OPS-TOC', desc='field ops map & checklists', state='LATER'),
    dict(name='Mesh-Torry', desc='the territory game', state='LATER'),
]
MENU_OWNS = {
    'OM Lite': 'radio 868 · MeshCore session · SD history',
    'OPS-TOC': 'field checklists · map pins · no GPS',
    'Mesh-Torry': 'the territory game · Wi-Fi · later',
}


def menu():
    """The start menu: three large app cards side by side, the focused one ringed in gold.

    Same rounded-card style as the Chat list (S513/EST-430): kPanel + 1 px kLine when resting, kSelBg + 2 px
    kAcc when focused, radius 6. Each card: the name large, one dim description, the state chip, then for the
    running card its memory and uptime (the numbers the old detail pane carried). The focused card's own line
    sits under the row; the hint bar shows the new keys.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Tanmatsu', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), 'core ready · radio free', font(FONT_MONO, TM['f_meta']),
         P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    top = y + TM['head_h'] + TM['menu_top']
    gap = TM['menu_gap']
    cw = (W - 2 * pad - 2 * gap) // 3
    ch = TM['menu_card_h']
    for i, card in enumerate(MENU_CARDS):
        x = pad + i * (cw + gap)
        focused = (i == 0)
        if focused:
            plate(d, [x, top, x + cw - 1, top + ch - 1], P['SelBg'], P['Acc'],
                  r=TM['menu_radius'], w=TM['ring_w'])
            name_col = P['AccInk']
        else:
            plate(d, [x, top, x + cw - 1, top + ch - 1], P['Panel'], P['Line'],
                  r=TM['menu_radius'], w=1)
            name_col = P['Ink']

        ix = x + 16
        text(d, (ix, top + 14), card['name'], font(FONT_SANS, TM['f_title']), name_col)
        text(d, (ix, top + 52), card['desc'], font(FONT_MONO, TM['f_meta']), P['Dim'])

        running = card['state'] == 'RUNNING'
        later = card['state'] == 'LATER'
        fc = font(FONT_MONO, TM['f_label'])
        bw = tw(fc, card['state']) + 14
        plate(d, [ix, top + 84, ix + bw, top + 106], P['Panel2'], P['Line'], r=3)
        chip_col = P['Green'] if running else (P['Dimmer'] if later else P['Dim'])
        text(d, (ix + bw / 2, top + 95), card['state'], fc, chip_col, anchor='mm')

        if running:
            fm = font(FONT_MONO, TM['f_meta'])
            text(d, (ix, top + 128), card['memory'], fm, P['Dim'])
            text(d, (ix, top + 150), card['uptime'], fm, P['Dim'])
        elif later:
            text(d, (ix, top + 132), 'coming later', font(FONT_MONO, TM['f_meta']), P['Dim'])
        else:
            text(d, (ix, top + 132), 'stopped · ↵ starts it', font(FONT_MONO, TM['f_meta']), P['Dim'])

    # one short dim line about the focused card: what it owns, in the mesh's own numbers
    text(d, (pad, top + ch + 28), MENU_OWNS[MENU_CARDS[0]['name']],
         font(FONT_MONO, TM['f_meta']), P['Dim'])

    hint_bar(d, 'home', [(',', 'settings'), ('←→', 'select'), ('Enter', 'open'), ('esc', 'quit')])
    return save(img, '2026-11-18-menu-cards.png')


# ------------------------------------------------------------------------------------- settings general
def settings_general():
    """EST-445: Settings, part 1 — the frame and the General tab.

    Same house style as the Chat/menu cards: the L2's status bar, the page head, the five-tab row
    (General · Nodes · Chat · Map · RPTR, the current one gold with an underline), then the General
    page as sections of label/value rows. Values are dim mono on the right, a chevron marks the Name
    sub-page, and the focused row carries the 2 px gold ring. The body scrolls; the thumb at the right
    shows there is more below (RADIO and DEVICE continue). All numbers are invented (public-repo rule 9).
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d, clock='22:31')

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Settings', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), 'general · tab 1 of 4',
         font(FONT_MONO, TM['f_meta']), P['Dimmer'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    # EST-448: one tab per OWNER (General + the registry), not one per function.
    tab_top = y + TM['head_h'] + 1
    tab_h = 30
    tabs = ['General', 'OM Lite', 'OPS-TOC', 'Mesh-Torry']
    ft = font(FONT_MONO, TM['f_label'])
    for i, name in enumerate(tabs):
        x0 = W * i // len(tabs)
        x1 = W * (i + 1) // len(tabs)
        on = (i == 0)
        text(d, ((x0 + x1) // 2, tab_top + tab_h // 2), name, ft, P['Acc'] if on else P['Dimmer'],
             anchor='mm')
        if on:
            d.rectangle([x0 + 18, tab_top + tab_h - 3, x1 - 18, tab_top + tab_h - 2],
                        fill=P['Acc'])
    hline(d, 0, W, tab_top + tab_h, P['Line'])

    body_top = tab_top + tab_h
    body_bot = H - TM['hint_h']
    lx0, lx1 = pad, W - pad
    row_h, sect_h = 34, 22

    def section(yy, label):
        spaced(d, (lx0 + 4, yy + 5), label, font(FONT_MONO, TM['f_label']), P['Dim'])
        return yy + sect_h

    yy = body_top + 4
    total_rows = [
        ('sect', 'NODE', None, None),
        ('row', 'Name', 'TNMT (SI)', True),
        ('row', 'Public key', 'a1b2c3d4e5f60718', False),
        ('row', 'Advert now', 'Enter', False),
        ('sect', 'ADVERT', None, None),
        ('row', 'Automatic advert', 'Off', False),
        ('sect', 'SOUND', None, None),
        ('row', 'Sound', 'On', False),
        ('row', 'Volume', '- 60 % +', False),
        ('row', 'Message', 'Soft Hi', False),
        ('row', 'Direct message', 'Wood Warm', False),
        ('row', 'Sent', 'Soft Tick', False),
        ('row', 'New node', 'Soft Lo', False),
        ('row', 'Error', 'Bell Fall', False),
        ('row', 'Boot', 'Off', False),
        ('sect', 'RADIO (READ ONLY)', None, None),
        ('row', 'Frequency', '869.618 MHz', False),
        ('row', 'Bandwidth', '62.5 kHz', False),
        ('row', 'Spreading', 'SF 8', False),
        ('row', 'Coding', '4/8', False),
        ('row', 'TX power', '22 dBm', False),
        ('row', 'Path hash', '2 B', False),
        ('sect', 'DEVICE (READ ONLY)', None, None),
        ('row', 'Time', '22:31 · rtc', False),
        ('row', 'SD card', 'mounted · history on', False),
        ('row', 'Contacts', '3 / 32', False),
        ('row', 'App version', '48', False),
        ('row', 'Free internal heap', '120 KB', False),
        ('row', 'Free PSRAM', '26.1 MB', False),
    ]
    total_h = sum(sect_h if k == 'sect' else row_h for k, *_ in total_rows)

    sound_rows = ('Sound', 'Volume', 'Message', 'Direct message', 'Sent', 'New node', 'Error', 'Boot')
    for kind, label, value, focused in total_rows:
        if kind == 'sect':
            if yy + sect_h > body_bot:
                break
            yy = section(yy, label)
            continue
        if yy + row_h > body_bot:
            break
        setrow(d, lx0, lx1, yy, label, value, focused=focused,
               chevron=(label == 'Name' or label in sound_rows))
        yy += row_h + 2

    scroll_indicator(d, (lx0, body_top, W - 8, body_bot), total_h, body_bot - body_top, 0)

    # the ticket's five keys, plus the global Settings key. The bar fits them by shortening the labels
    # (the device's chrome drops a chip that would run under the symbol keys).
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-general.png')


def _settings_owner_frame(tab_index, right_text):
    """The common chrome for an EST-448 owner tab: status bar, page head, the four owner tabs, body box."""
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d, clock='22:31')

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Settings', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), right_text,
         font(FONT_MONO, TM['f_meta']), P['Dimmer'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    tab_top = y + TM['head_h'] + 1
    tab_h = 30
    tabs = ['General', 'OM Lite', 'OPS-TOC', 'Mesh-Torry']
    ft = font(FONT_MONO, TM['f_label'])
    for i, name in enumerate(tabs):
        x0 = W * i // len(tabs)
        x1 = W * (i + 1) // len(tabs)
        on = (i == tab_index)
        text(d, ((x0 + x1) // 2, tab_top + tab_h // 2), name, ft, P['Acc'] if on else P['Dimmer'],
             anchor='mm')
        if on:
            d.rectangle([x0 + 18, tab_top + tab_h - 3, x1 - 18, tab_top + tab_h - 2],
                        fill=P['Acc'])
    hline(d, 0, W, tab_top + tab_h, P['Line'])
    return img, d, W, H, pad, tab_top + tab_h


def settings_sound():
    """S514/EST-450: the General tab's new SOUND section.

    The section is scrolled to the top of the body so all eight rows are visible: Sound (master, On),
    Volume (the L2's 60%), then one row per event with its stored sound — Message Soft Hi, Direct
    message Wood Warm, Sent Soft Tick, New node Soft Lo, Error Bell Fall, Boot Off. The event rows are
    sub-pages (chevron). The values are the L2's own defaults; no real node/channel name is used
    (public-repo rule 9). Height of everything above SOUND: NODE (22 + 3*36) + ADVERT (22 + 36).
    """
    img, d, W, H, pad, body_top = _settings_owner_frame(0, 'general · tab 1 of 4')
    body_bot = H - TM['hint_h']
    lx0, lx1 = pad, W - pad
    row_h, sect_h = 34, 22

    items = [
        ('sect', 'NODE', None),
        ('row', 'Name', 'TNMT (SI)'),
        ('row', 'Public key', 'a1b2c3d4e5f60718'),
        ('row', 'Advert now', 'Enter'),
        ('sect', 'ADVERT', None),
        ('row', 'Automatic advert', 'Off'),
        ('sect', 'SOUND', None),
        ('row', 'Sound', 'On'),
        ('row', 'Volume', '- 60 % +'),
        ('row', 'Message', 'Soft Hi'),
        ('row', 'Direct message', 'Wood Warm'),
        ('row', 'Sent', 'Soft Tick'),
        ('row', 'New node', 'Soft Lo'),
        ('row', 'Error', 'Bell Fall'),
        ('row', 'Boot', 'Off'),
        ('sect', 'RADIO (READ ONLY)', None),
        ('row', 'Frequency', '869.618 MHz'),
        ('row', 'Bandwidth', '62.5 kHz'),
        ('row', 'Spreading', 'SF 8'),
        ('row', 'Coding', '4/8'),
        ('row', 'TX power', '22 dBm'),
        ('row', 'Path hash', '2 B'),
    ]
    total_h = sum(sect_h if k == 'sect' else row_h + 2 for k, *_ in items)
    scroll = 4 + sect_h + 3 * (row_h + 2) + sect_h + (row_h + 2)   # SOUND header at the body top

    yy = body_top + 4 - scroll
    for kind, label, value in items:
        if kind == 'sect':
            if body_top <= yy and yy + sect_h <= body_bot:
                spaced(d, (lx0 + 4, yy + 5), label, font(FONT_MONO, TM['f_label']), P['Dim'])
            yy += sect_h
            continue
        if body_top <= yy and yy + row_h <= body_bot:
            setrow(d, lx0, lx1, yy, label, value, focused=(label == 'Sound'), chevron=True)
        yy += row_h + 2

    scroll_indicator(d, (lx0, body_top, W - 8, body_bot), total_h, body_bot - body_top, scroll)
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-sound.png')


def settings_sound_picker():
    """S514/EST-451: the Message event's sound picker, SCROLLED past the first screenful.

    A LIST sub-page has no tab row: page head, then the L2's library grouped by shape (SOLO, DUO, TRIO,
    MULTI, ALERT) and OFF last. The list is scrolled so the focused row (Wood Warm, previewed) sits ON the
    last visible line, with more rows below — the Settings-root scroll rule the picker now uses. The gold
    frame is inside the body, not one pixel under it. Names are the L2 library's own (public:
    firmware/src/sound_synth.cpp); there is no node or channel text here.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d, clock='22:31')

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Settings', font(FONT_SANS, TM['f_title']), P['Ink'],
         anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), 'Message',
         font(FONT_MONO, TM['f_meta']), P['Dimmer'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    body_top = TM['status_h'] + TM['head_h'] + 1
    body_bot = H - TM['hint_h']
    lx0, lx1 = pad, W - pad
    row_h, sect_h = 34, 22
    row_step = row_h + 2

    shapes = [
        ('SOLO', ['Beep Clean', 'Beep High', 'Soft Tick', 'Bell C6', 'Bell Low',
                  'Glass Ping', 'Marimba Tock', 'Wood Pluck', 'Organ Mellow', 'Kalimba Tine']),
        ('DUO', ['Bell TK-TK', 'Soft Hi', 'Marimba Hi', 'Bell Hi', 'Wood Warm',
                 'Chime Bright', 'Soft Lo', 'Organ Low', 'Kalimba Rise', 'Wood Double']),
        ('TRIO', ['Glass Trio', 'Chime Arp', 'Bell Rise', 'Bell Fall', 'Marimba Roll',
                  'Wood Motif', 'Soft Motif', 'Organ Rise', 'Kalimba Turn', 'Glass Top']),
        ('MULTI', ['Bell Chime', 'Chime Run', 'Marimba Run', 'Wood Groove', 'Soft Chord',
                   'Organ Chord', 'Kalimba Lick', 'Glass Sparkle', 'Bell Road', 'Wood Alert']),
        ('ALERT', ['Battery Low']),
    ]
    items = []
    for name, names in shapes:
        items.append(('sect', name, None))
        for n in names:
            tag = 'now' if n == 'Soft Hi' else ('preview' if n == 'Wood Warm' else '')
            items.append(('row', n, tag))
    items.append(('sect', 'OFF', None))
    items.append(('row', 'Off', ''))

    total_h = sum(sect_h if k == 'sect' else row_step for k, *_ in items)

    # Scroll so the focused row's bottom lands exactly on the drawn bottom line.
    focus_name = 'Wood Warm'
    focus_top = 0
    yy = 0
    for k, label, _ in items:
        if k == 'sect':
            yy += sect_h
        else:
            if label == focus_name:
                focus_top = yy
                break
            yy += row_step
    scroll = focus_top + row_h - (body_bot - body_top)

    yy = body_top - scroll
    for k, label, tag in items:
        if k == 'sect':
            if body_top <= yy and yy + sect_h <= body_bot:
                spaced(d, (lx0 + 4, yy + 5), label, font(FONT_MONO, TM['f_label']), P['Dim'])
            yy += sect_h
            continue
        if body_top <= yy and yy + row_h <= body_bot:
            setrow(d, lx0, lx1, yy, label, tag, focused=(label == focus_name), chevron=False)
        yy += row_step

    scroll_indicator(d, (lx0, body_top, W - 8, body_bot), total_h, body_bot - body_top, scroll)
    hint_bar(d, 'settings', [('↑↓', 'row'), ('Enter', 'play/choose'), ('esc', 'back'), (',', 'settings')])
    return save(img, '2026-11-19-sound-picker-scrolled.png')


def _omlite_rows():
    """The OM Lite tab's sections, as the device builds them (S514/EST-446 + EST-447).

    One scrolling list: NODES · CHAT · MAP · RPTR, no new top tabs. Every number and name here is an
    INVENTED fixture (public-repo rule 9); the channel scopes are the public, derivable ones and no private
    key is ever drawn. `settings_nodes` and `settings_map` show the same list at a different focus/scroll.
    """
    return [
        ('sect', 'NODES', None, None),
        ('row', 'Add heard nodes to the radio', 'Off', False),
        ('row', 'Stored nodes', 'Radio 10 / 300 · SD 412 · SD ok', False),
        ('row', 'Clear radio (SD keeps them)', '', False),
        ('row', 'Remove a contact', '', True),
        ('row', 'Clear all contacts', '', False),
        ('row', 'New-node notice', 'On', False),
        ('sect', 'CHAT', None, None),
        ('row', "Don't Panic", 'default', True),
        ('row', 'Public', 'no scope', True),
        ('row', '#slovenija', 'scope #si', True),
        ('row', '#slovenia', 'scope #europe', True),
        ('row', 'Add hashtag channel', '', True),
        ('row', 'Add channel by key', '', True),
        ('sect', 'MAP', None, None),
        ('row', 'Start position', 'Ljubljana', False),
        ('row', 'Start zoom', 'z16', False),
        ('row', 'slo (z9-15)', '35496 tiles', False),
        ('row', 'slo16 (z16)', '118240 tiles', False),
        ('row', 'slo17 (z17)', '899104 tiles · 3.4 GB', False),
        ('sect', 'RPTR', None, None),
        ('row', 'Repeater admin comes later', '', False),
    ]


def _draw_omlite_rows(d, W, pad, body_top, body_bot, focus_label, scroll):
    """Draw the OM Lite tab with the frame's own row arithmetic: headers 22, rows 34, focus by label,
    clamped scroll, rows clipped to the body (the device draws no text on the chrome)."""
    lx0, lx1 = pad, W - pad
    row_h, sect_h = 34, 22
    rows = _omlite_rows()
    total = sum(sect_h if kind == 'sect' else row_h for kind, *_ in rows)
    visible = body_bot - body_top
    scroll = max(0, min(scroll, max(0, total - visible)))
    yy = body_top - scroll
    for kind, label, value, chevron in rows:
        h = sect_h if kind == 'sect' else row_h
        top, bot = yy, yy + h
        yy = bot
        if bot <= body_top or top >= body_bot:
            continue
        if top < body_top or bot > body_bot:
            continue          # no text on the chrome, exactly like main/settings_page.cpp
        if kind == 'sect':
            spaced(d, (lx0 + 4, top + 5), label, font(FONT_MONO, TM['f_label']), P['Dim'])
        else:
            setrow(d, lx0, lx1, top, label, value, focused=(label == focus_label), chevron=chevron)
    scroll_indicator(d, (lx0, body_top, W - 8, body_bot), total, visible, scroll)
    return total


def settings_omlite():
    """EST-448 + EST-447: the OM Lite tab — ONE scrolling list with the same section treatment as General.

    NODES · CHAT · MAP · RPTR are the pages' settings inside the app's own tab; the `,` key from a page opens
    here at that page's section. All four sections are filled now (EST-446 channels, EST-447 contacts and
    map); RPTR is still the one honest line.
    """
    img, d, W, H, pad, body_top = _settings_owner_frame(1, 'OM Lite · tab 2 of 4')
    body_bot = H - TM['hint_h']
    _draw_omlite_rows(d, W, pad, body_top, body_bot, "Don't Panic", 0)
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-omlite.png')



def settings_opstoc():
    """EST-448: a LATER app's tab — present, named, and honestly empty.

    OPS-TOC is not built, so it supplies no settings data; the frame draws its ONE dim line instead of an
    empty page or a fabricated row. The same state Mesh-Torry's tab is in today.
    """
    img, d, W, H, pad, body_top = _settings_owner_frame(2, 'OPS-TOC · tab 3 of 4')
    text(d, (pad + 6, body_top + 16), 'Settings come with the app',
         font(FONT_SANS, TM['f_body']), P['Dim'])
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-opstoc.png')


def settings_chat():
    """S514/EST-446 (+447): the OM Lite tab's CHAT section — the device's channels and their scopes.

    Four rows, one per channel, name left and the ticket's words right: "default" / "no scope" /
    "scope #si" / "scope #europe"; a chevron opens the channel page. Under them the two add pages.
    Inside the OM Lite tab, never a second Settings (EST-448's structure). The scope strings are the
    public, derivable ones; no private channel's key is ever drawn or stored here (rule 9).
    """
    img, d, W, H, pad, body_top = _settings_owner_frame(1, 'OM Lite · tab 2 of 4')
    body_bot = H - TM['hint_h']
    _draw_omlite_rows(d, W, pad, body_top, body_bot, "Don't Panic", 0)
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-chat.png')


def settings_nodes():
    """S514/EST-447: the OM Lite tab's NODES section — contacts, remove, clear, new-node notice.

    The `,` key from the Nodes page lands here. Contacts is read only; Remove a contact opens the list
    sub-page; Clear all raises the shared sheet; New-node notice is a stored toggle (default On). The rows
    below it are the CHAT section, as on the device (one scrolling list, not a second tab).
    """
    img, d, W, H, pad, body_top = _settings_owner_frame(1, 'OM Lite · tab 2 of 4')
    body_bot = H - TM['hint_h']
    _draw_omlite_rows(d, W, pad, body_top, body_bot, 'Add heard nodes to the radio', 0)
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-nodes.png')


def settings_map():
    """S514/EST-447: the OM Lite tab's MAP section — start position, start zoom, the packs on the card.

    The page is SCROLLED so the section is in view exactly as the device's clamped scroll can place it:
    NODES (4 rows) + CHAT (6 rows) above it, then MAP and RPTR below, which fits the 356 px body. The pack
    counts and the z17 size are invented fixtures (rule 9); "not on card" is what a missing pack would say.
    """
    img, d, W, H, pad, body_top = _settings_owner_frame(1, 'OM Lite · tab 2 of 4')
    body_bot = H - TM['hint_h']
    # Height of everything above the MAP header in _omlite_rows(): NODES (22 + 6*34) + CHAT (22 + 6*34).
    above_map = (22 + 6 * 34) + (22 + 6 * 34)
    _draw_omlite_rows(d, W, pad, body_top, body_bot, 'Start position', above_map)
    hint_bar(d, 'settings', [('Enter', 'open/change'), ('esc', 'back'), ('←→', 'tab'), ('↑↓', 'row'),
                             (',', 'settings')])
    return save(img, '2026-11-19-settings-map.png')


# ------------------------------------------------------------------------------------------------ dm send
DM_THREAD = [
    # stamp, the delivery state drawn after OUR time (None for an incoming line), who, body, mine
    ('09:41', None, 'Alpha', 'Are you near the north repeater today?', False),
    ('09:43', None, 'Alpha', 'The path came back with two hops, so it should work.', False),
    ('09:48', 'delivered', 'you', 'Yes, on my way after lunch.', True),
    ('09:52', 'sending', 'you', 'I will ping you when I am there.', True),
]


def dm_enter():
    """OM Lite > Chat > DM thread, with the Enter key cap (S513/EST-439; S514/EST-444 rework).

    The open DM view: our own messages on the right under "you", each with its state AFTER the time — "delivered"
    once the ACK came back, "sending" until it does (and "no confirmation" after the last attempt, which the
    drawing code takes from the L2's DmState words; the font has no check glyph, so the word is drawn). The
    composer is the channel's own, because a DM types and sends the same way, and its right end is now the
    hint bar's "↵ Enter" key cap instead of the old filled Send button (empty draft = dim).
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Chat', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), '4 messages', font(FONT_MONO, TM['f_meta']), P['Dimmer'],
         anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    work_top = y + TM['head_h']
    text(d, (pad, work_top + 18), 'DM: Alpha', font(FONT_SANS, 20), P['Ink'])

    left_x = pad + 58                      # kTmChatBodyX0: incoming under the time column
    right_x = W - pad - 46                 # kTmChatBodyX1: outgoing keeps the age column clear
    fstamp = font(FONT_MONO, TM['f_meta'])
    fname = font(FONT_SANS, TM['f_body'])
    fbody = font(FONT_SANS, TM['f_body'])

    yy = work_top + 56
    for stamp, state, who, body, mine in DM_THREAD:
        # the time, then the state for our own lines ("09:48 delivered")
        txt = stamp + (' ' + state if state else '')
        text(d, (pad + 4, yy), txt, fstamp, P['Dimmer'])
        if mine:
            text(d, (right_x, yy), 'you', font(FONT_MONO, TM['f_meta']), P['Dim'], anchor='ra')
        else:
            text(d, (left_x, yy), who, font(FONT_SANS, TM['f_meta']), P['Acc'])
        text(d, (W - pad, yy), 'now' if mine else '4h', fstamp, P['Dimmer'], anchor='ra')
        yy += 20
        lines = wrap(d, body, fbody, right_x - left_x)
        for line in lines:
            if mine:
                text(d, (right_x, yy), line, fbody, P['Ink'], anchor='ra')
            else:
                text(d, (left_x, yy), line, fbody, P['Ink'])
            yy += 20
        yy += 14

    # the composer, the same one a channel has; S514/EST-444: the Enter key cap, dim while the draft is empty
    ch = H - TM['hint_h'] - 56
    plate(d, [pad, ch, W - pad, ch + 44], P['FieldBg'], P['Line'], r=4)
    text(d, (pad + 12, ch + 22), 'message this contact, Enter sends', font(FONT_MONO, TM['f_meta']),
         P['Dimmer'], anchor='lm')
    key_cap(d, [W - pad - 84, ch + 8, W - pad - 8, ch + 36], '↵ Enter', P['Dimmer'],
            font(FONT_SANS, TM['f_hint']))

    hint_bar(d, 'chat', [('Enter', 'send'), ('esc', 'back'), ('type', 'write'), ('↑↓', 'scroll'), (',', 'settings')])
    return save(img, '2026-11-19-dm-enter.png')


# ------------------------------------------------------------- the chat time row (S515/EST-462)
# stamp, the delivery state drawn after OUR time (None for an incoming line), who, body, mine, and the dim meta
# tail core/chat_meta builds: hops/via · bytes · copies (incoming) or bytes · repeats (ours). One row is a restored
# old record: its hops byte was fake in v57, so it honestly shows nothing.
META_THREAD = [
    ('09:12', None, 'Alpha', 'Was at the coast today, the direct path is clear.', False,
     'direct · 87 B'),
    ('09:14', None, 'Beta', 'The third repeater works from the hill.', False,
     '3 hops via TEST-RPT-A · 101 B'),
    ('09:16', None, 'Gamma', 'This one only resolves as a hash.', False,
     '3 hops via 86a8 · 94 B'),
    ('09:18', None, 'Delta', 'A line read back from before the update.', False, ''),
    ('09:20', 'delivered', 'you', 'Testing the new time row.', True, '87 B'),
    ('09:22', 'delivered', 'you', 'Second one was heard back twice.', True, '87 B · repeated 2×'),
]


def chat_meta():
    """OM Lite > Chat > # Public, with the S515/EST-462 time row above every message.

    After the sender name (incoming) or right-aligned just left of "you" (ours), in the dimmer ink: the real path
    as "3 hops via <last repeater>" (a contact name only when exactly one matches, else the hash), the frame's byte
    count, and the copies heard — "heard 2×" / "repeated 2×", only from two up. A restored v57 line shows only what
    its record carried: no hop count, no bytes. RSSI and SNR are deliberately absent (DEVICE.md § 5b).
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Chat', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), '6 messages', font(FONT_MONO, TM['f_meta']), P['Dimmer'],
         anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    work_top = y + TM['head_h']
    text(d, (pad, work_top + 18), '# Public', font(FONT_SANS, 20), P['Ink'])

    left_x = pad + 58                      # kTmChatBodyX0: incoming under the time column
    right_x = W - pad - 46                 # kTmChatBodyX1: outgoing keeps the age column clear
    fstamp = font(FONT_MONO, TM['f_meta'])
    fname = font(FONT_SANS, TM['f_body'])
    fmeta = font(FONT_MONO, TM['f_meta'])
    fbody = font(FONT_SANS, TM['f_body'])

    ages = ['12m', '10m', '9m', '8m', '2m', '1m']
    # Six one-line messages must all fit above the composer (all six are the point of this preview).
    yy = work_top + 40
    for (stamp, state, who, body, mine, meta), age in zip(META_THREAD, ages):
        txt = stamp + (' ' + state if state else '')
        text(d, (pad + 4, yy), txt, fstamp, P['Dimmer'])
        if mine:
            text(d, (right_x, yy), 'you', fmeta, P['Dim'], anchor='ra')
            if meta:
                tx = right_x - tw(fmeta, 'you') - 10      # metaOutgoingRight: left of "you"
                text(d, (tx, yy), meta, fmeta, P['Dimmer'], anchor='ra')
        else:
            text(d, (left_x, yy), who, fname, P['Acc'])
            if meta:
                tx = left_x + tw(fname, who) + 10         # metaIncomingX: after the sender
                text(d, (tx, yy), meta, fmeta, P['Dimmer'])
        text(d, (W - pad, yy), age, fstamp, P['Dimmer'], anchor='ra')
        yy += 19
        lines = wrap(d, body, fbody, right_x - left_x)
        for line in lines:
            if mine:
                text(d, (right_x, yy), line, fbody, P['Ink'], anchor='ra')
            else:
                text(d, (left_x, yy), line, fbody, P['Ink'])
            yy += 19
        yy += 7

    ch = H - TM['hint_h'] - 56
    plate(d, [pad, ch, W - pad, ch + 44], P['FieldBg'], P['Line'], r=4)
    text(d, (pad + 12, ch + 22), 'message the channel, Enter sends', font(FONT_MONO, TM['f_meta']),
         P['Dimmer'], anchor='lm')
    key_cap(d, [W - pad - 84, ch + 8, W - pad - 8, ch + 36], '↵ Enter', P['Dimmer'],
            font(FONT_SANS, TM['f_hint']))

    hint_bar(d, 'chat', [('Enter', 'send'), ('esc', 'back'), ('type', 'write'), ('↑↓', 'scroll'), (',', 'settings')])
    return save(img, '2026-11-19-chat-meta.png')



# ------------------------------------------------------------------ RPTR part 1 (S514/EST-453)
# Same two-pane shape as the device's main/rptr_page.cpp. Every name and key is an INVENTED fixture
# (public-repo rule 9): no real repeater, operator or password appears here.
RPTR_FIXTURES = [
    ('TEST-RPT-A', 'direct · 6.75 dB', '11 22 33 … a1', '32 B', True),
    ('TEST-RPT-B', 'direct · 2.25 dB', '44 55 66 … b2', '32 B', True),
    ('TEST-RPT-C', 'path · no snr', '77 88 99 … c3', '6 B', False),
]


def _rptr_list_pane(d, W, pad, work_top, focus, live=False):
    list_x0, list_x1 = pad, pad + TM['list_w']
    row_right = list_x1 - TM['ring_inset']
    spaced(d, (list_x0, work_top + 16), 'REPEATERS', font(FONT_MONO, TM['f_label']), P['Dim'])
    fchip = font(FONT_MONO, TM['f_label'])
    cw = tw(fchip, 'SCAN  s') + 14
    plate(d, [row_right - cw, work_top + 8, row_right, work_top + 30], P['Panel2'], P['Line'], r=3)
    text(d, (row_right - cw / 2, work_top + 19), 'SCAN  s', fchip, P['Dim'], anchor='mm')
    rows_top = work_top + 44
    for i, (name, meta, key, klen, saved) in enumerate(RPTR_FIXTURES):
        y0 = rows_top + i * TM['row_h']
        y1 = y0 + TM['row_h']
        if i == focus:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])
        d.ellipse([list_x0 + 2, y0 + TM['row_h'] // 2 - 4,
                   list_x0 + 10, y0 + TM['row_h'] // 2 + 4], fill=P['Green'])
        text(d, (list_x0 + 22, y0 + 22), name, font(FONT_SANS, 16),
             P['AccInk'] if i == focus else P['Ink'])
        text(d, (list_x0 + 22, y0 + 42), meta, font(FONT_MONO, TM['f_meta']), P['Dimmer'])
        if saved:
            text(d, (row_right - 6, y0 + 20), 'saved', font(FONT_MONO, TM['f_label']),
                 P['Acc'] if i == focus else P['Dim'], anchor='ra')
    vline(d, list_x1 + 8, work_top, TM['H'] - TM['hint_h'] - 8, P['Line'])
    return list_x1 + 8 + 24, W - pad - (list_x1 + 8 + 24)


def rptr_list():
    """RPTR part 1, the found list: scan is manual ('s'), Enter opens the login."""
    img, d = new_screen()
    W, pad = TM['W'], TM['pad']
    status_bar(d)
    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'RPTR', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), 'manual scan · 3 found', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1
    x, rw = _rptr_list_pane(d, W, pad, work_top, 0)

    yy = work_top + 16
    text(d, (x, yy + 24), 'Repeaters', font(FONT_SANS, 26), P['Ink'])
    yy += 58
    text(d, (x, yy), 'Scan asks every repeater in range (zero hop).', font(FONT_MONO, TM['f_meta']),
         P['Dim'])
    yy += 18
    text(d, (x, yy), 'Enter opens the login; a remembered password is filled in.', font(FONT_MONO, TM['f_meta']),
         P['Dim'])
    yy += 30
    hline(d, x, x + rw, yy, P['Line'])
    yy += 22
    text(d, (x, yy + 14), 'TEST-RPT-A', font(FONT_SANS, 17), P['Ink'])
    yy += 36
    for lab, val in [('Key', '11 22 33 … a1'), ('SNR', 'in 4.25 dB · out 6.75 dB'),
                     ('Hops', '0 · in contacts')]:
        text(d, (x, yy), lab, font(FONT_MONO, TM['f_kv']), P['Dim'])
        text(d, (x + 70, yy), val, font(FONT_MONO, TM['f_kv']), P['Ink'])
        yy += 20
    yy += 8
    text(d, (x, yy), 'Enter = log in', font(FONT_MONO, TM['f_hint']), P['Acc'])
    hint_bar(d, 'rptr', [('↑↓', 'move'), ('Enter', 'log in'), ('s', 'scan'), ('esc', 'back')])
    return save(img, '2026-11-19-rptr-list.png')


def rptr_status():
    """RPTR part 1, the STATUS view: the repeater's own counters, every value with its unit."""
    img, d = new_screen()
    W, pad = TM['W'], TM['pad']
    status_bar(d)
    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'RPTR', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), 'status', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1
    x, rw = _rptr_list_pane(d, W, pad, work_top, 0)

    yy = work_top + 16
    text(d, (x, yy + 24), 'TEST-RPT-A', font(FONT_SANS, 26), P['Ink'])
    yy += 58
    text(d, (x, yy), 'The repeater\'s own counters', font(FONT_MONO, TM['f_meta']), P['Dim'])
    yy += 24
    cells = [('UPTIME', '1d 01:01'), ('BATTERY', '4.11 V (92%)'),
             ('NOISE FLOOR', '-117 dBm'), ('LAST RSSI', '-83 dBm'),
             ('LAST SNR', '8.25 dB'), ('TX QUEUE', '2'),
             ('PACKETS RX', '123456'), ('PACKETS TX', '7890'),
             ('AIRTIME', '3600 s'), ('RX AIRTIME', '7200 s'),
             ('EVENTS', '5'), ('CRC ERRORS', '2')]
    col_w = rw // 2
    for i, (lab, val) in enumerate(cells):
        cx = x + (i % 2) * col_w
        cy = yy + (i // 2) * 34
        text(d, (cx, cy), lab, font(FONT_MONO, TM['f_label']), P['Dimmer'])
        text(d, (cx, cy + 15), val, font(FONT_SANS, 16), P['Ink'])
    yy += 6 * 34 + 6
    text(d, (x, yy), 'Clock 1750000100 (repeater\'s own) · RTT 431 ms', font(FONT_MONO, TM['f_meta']),
         P['Dim'])
    yy += 22
    text(d, (x, yy), '12 s ago · r refreshes · Esc logs out', font(FONT_MONO, TM['f_hint']), P['Dimmer'])
    hint_bar(d, 'rptr', [('↑↓', 'move'), ('r', 'refresh'), ('esc', 'log out')])
    return save(img, '2026-11-19-rptr-status.png')


# ------------------------------------------------- S515/EST-461: the NEW contacts, and the hint bar's dots
# Invented, like every fixture here. Two favourites on top, then the still-NEW rows (their red dot), then the
# rest by last heard — core/mc_store.h::contactsOrder's order, drawn at the device's own row geometry.
NODES_FRESH = [
    ('client', 'TEST-A1', 'direct', '14h', '3.2', 'favourite', False),
    ('repeater', 'RPT-Sever', 'path 2', '18m', '7.5', 'favourite', False),
    ('client', 'TEST-B2', 'direct', 'now', '4.1', None, True),      # NEW
    ('room', 'ROOM-3', 'path 2', '2m', '1.8', None, True),          # NEW
    ('repeater', 'RPT-Jug', 'path 2', '49m', '6.1', None, False),
    ('repeater', 'RPT-Vzhod', 'path 3', '9h', '-1.4', None, False),
    ('client', 'LOCAL-KB', 'direct', '2d', '4.4', None, False),
]


def nodes_fresh():
    """OM Lite > Nodes after a first advert: the new rows sit under the favourites, each with a red dot.

    The dot is drawn where main/nodes_page.cpp draws it (list_x1 - ring_inset - 46, row top + 16, r 3) in the
    chat list's badge red, so the preview and the glass cannot disagree about the size or the place.
    """
    img, d = new_screen()
    W, H, pad = TM['W'], TM['H'], TM['pad']
    status_bar(d)

    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Nodes', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fm = font(FONT_MONO, TM['f_meta'])
    text(d, (W - pad, y + TM['head_h'] // 2), '7 nodes heard · 5 in the last hour', fm, P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])
    work_top = y + TM['head_h'] + 1

    list_x0, list_x1 = pad, pad + TM['list_w']
    fl = font(FONT_MONO, TM['f_label'])
    spaced(d, (list_x0, work_top + 16), 'NODES', fl, P['Dim'])
    text(d, (list_x1 - 12, work_top + 16), '7 of 9', font(FONT_MONO, TM['f_label']), P['Dimmer'], anchor='ra')

    rows_top = work_top + 40
    row_h = 46
    row_right = list_x1 - TM['ring_inset']
    for i, (kind, name, hops, age, snr, mark, fresh) in enumerate(NODES_FRESH):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == 2:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)   # the box is on the first NEW row; its dot stays
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])
        col = P['Client'] if kind == 'client' else (P['Green'] if kind == 'repeater' else P['Warn'])
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=col)
        nc = P['AccInk'] if i == 2 else P['Ink']
        text(d, (list_x0 + 24, y0 + 10), name, font(FONT_SANS, 16), nc)
        meta = font(FONT_MONO, TM['f_meta'])
        text(d, (list_x0 + 24, y0 + 29), hops, meta, P['Dimmer'])
        text(d, (row_right - 74, y0 + 11), age, meta, P['Dim'], anchor='ra')
        text(d, (row_right - 6, y0 + 29), snr + ' dB', meta, P['Dim'], anchor='ra')
        if mark == 'favourite':
            d.ellipse([row_right - 74 - 3, y0 + 16 - 3, row_right - 74 + 3, y0 + 16 + 3], fill=P['Acc'])
        if fresh:
            # main/nodes_page.cpp: disc(list_x1 - kTmRingInset - 46, y + 16, 3, kBad)
            d.ellipse([row_right - 46 - 3, y0 + 16 - 3, row_right - 46 + 3, y0 + 16 + 3], fill=P['Bad'])

    scroll_indicator(d, (list_x0, rows_top, list_x1, rows_top + len(NODES_FRESH) * row_h), total=9,
                     visible=len(NODES_FRESH))

    # detail pane: the focused row, which is the first NEW node
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), 'TEST-B2', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'client · key ' + NODE_KEY + ' · new, not opened yet', fm, P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    kv = [('TYPE', 'client (companion)'), ('HOPS', 'direct · 0 hops'), ('SNR / RSSI', '4.1 dB / -68 dBm'),
          ('LAST HEARD', 'now'), ('PUBLIC KEY', NODE_KEY), ('PATH', 'direct')]
    for label, value in kv:
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28
    yy += 8
    hline(d, x, W - pad, yy, P['Line'])
    yy += 16
    text(d, (x, yy), 'Enter: actions for this node.', font(FONT_SANS, TM['f_body']), P['Dimmer'])
    text(d, (x, yy + 22), 'f: favourite. The dot goes once the box lands here.',
         font(FONT_SANS, TM['f_body']), P['Dimmer'])

    hint_bar(d, 'nodes', [('Enter', 'menu'), ('f', 'favourite'), ('/', 'search'), ('esc', 'back')],
             dots=('chat', 'nodes'))
    return save(img, '2026-11-19-nodes-fresh.png')


def hint_dots():
    """The hint bar with BOTH notification dots: Chat (a channel or a DM is unread) and Nodes (a contact is
    still new). Drawn on the Launcher on purpose — the dots belong to the chrome, not to a page."""
    return launcher(dots=('chat', 'nodes'), name='2026-11-19-hint-dots.png')


# ------------------------------------------- S515/EST-465: the store view, the row tag and the node menu
# Invented, like every fixture here. `store` = True means in the radio (the gold "R" chip); every other row is
# archive-only. One row carries all three marks (radio + favourite + fresh), which is the row the ticket asks
# to check.
NODES_VIEWS = [
    ('client', 'TEST-A1', 'direct', '14h', '3.2', True, True, True),
    ('repeater', 'RPT-Sever', 'path 2', '18m', '7.5', True, True, False),
    ('repeater', 'RPT-Jug', 'path 2', '49m', '6.1', True, False, False),
    ('client', 'TEST-B2', 'direct', '2m', '4.1', False, False, True),
    ('room', 'ROOM-3', 'path 2', '3m', '1.8', False, False, False),
    ('repeater', 'RPT-Vzhod', 'path 3', '9h', '-1.4', False, False, False),
    ('client', 'LOCAL-KB', 'direct', '2d', '4.4', False, False, False),
]


def _nodes_head_store(d, W, pad, radio, archive, view):
    """The head band: title, the three chips (active gold), the live counts on the right."""
    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Nodes', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    fh = font(FONT_MONO, TM['f_hint'])
    fm = font(FONT_MONO, TM['f_meta'])
    head_right = 'Radio %d / 300 · SD %d' % (radio, archive)
    text(d, (W - pad, y + TM['head_h'] // 2), head_right, fm, P['Dim'], anchor='rm')
    title_w = tw(font(FONT_SANS, TM['f_title']), 'Nodes')
    cx = pad + title_w + 28
    chip_h = 22
    cy = y + (TM['head_h'] - chip_h) // 2
    for name in ('All', 'Radio', 'SD'):
        cw = tw(fh, name) + 20
        on = (name == view)
        d.rectangle([cx, cy, cx + cw, cy + chip_h], fill=P['SelBg'] if on else P['FieldBg'],
                    outline=P['Acc'] if on else P['Line'], width=2 if on else 1)
        text(d, (cx + cw // 2, cy + chip_h // 2), name, fh, P['AccInk'] if on else P['Dim'], anchor='mm')
        cx += cw + 8
    hline(d, 0, W, y + TM['head_h'], P['Line'])


def _nodes_store_list(d, W, rows, focus_row, bar_count, view):
    pad = TM['pad']
    list_x0, list_x1 = pad, pad + TM['list_w']
    work_top = TM['status_h'] + TM['head_h'] + 1
    bar_x1 = list_x1 - TM['ring_inset']
    bar_y, bar_h = work_top + 4, 30
    d.rectangle([list_x0, bar_y, bar_x1, bar_y + bar_h], fill=P['FieldBg'], outline=P['Line'], width=1)
    text(d, (list_x0 + 12, bar_y + bar_h // 2), 'search nodes', font(FONT_SANS, TM['f_body']),
         P['Dimmer'], anchor='lm')
    text(d, (bar_x1 - 12, bar_y + bar_h // 2), bar_count, font(FONT_MONO, TM['f_hint']), P['Dim'],
         anchor='rm')
    rows_top = bar_y + bar_h + 6
    row_h = 46
    row_right = list_x1 - TM['ring_inset']
    for i, (kind, name, hops, age, snr, store, fav, fresh) in enumerate(rows):
        y0 = rows_top + i * row_h
        y1 = y0 + row_h
        if i == focus_row:
            focus_band(d, list_x0 - 8, list_x1, y0, y1)
        else:
            hline(d, list_x0, row_right, y1, P['RowLine'])
        col = P['Client'] if kind == 'client' else (P['Green'] if kind == 'repeater' else P['Warn'])
        d.ellipse([list_x0 + 6, y0 + row_h // 2 - 4, list_x0 + 14, y0 + row_h // 2 + 4], fill=col)
        # main/nodes_page.cpp: a 14x14 gold plate at list_x0+23, the letter R in the background ink.
        if store:
            d.rectangle([list_x0 + 23, y0 + 7, list_x0 + 37, y0 + 21], fill=P['Acc'])
            text(d, (list_x0 + 30, y0 + 14), 'R', font(FONT_MONO, TM['f_hint']), P['Bg'], anchor='mm')
        nc = P['AccInk'] if i == focus_row else P['Ink']
        text(d, (list_x0 + 43, y0 + 10), name, font(FONT_SANS, 16), nc)
        meta = font(FONT_MONO, TM['f_meta'])
        text(d, (list_x0 + 43, y0 + 29), hops, meta, P['Dimmer'])
        text(d, (row_right - 74, y0 + 11), age, meta, P['Dim'], anchor='ra')
        text(d, (row_right - 6, y0 + 29), snr + ' dB', meta, P['Dim'], anchor='ra')
        if fav:
            d.ellipse([row_right - 77, y0 + 13, row_right - 71, y0 + 19], fill=P['Acc'])
        if fresh:
            d.ellipse([row_right - 49, y0 + 13, row_right - 43, y0 + 19], fill=P['Bad'])
    scroll_indicator(d, (list_x0, rows_top, list_x1, rows_top + len(rows) * row_h),
                     total=len(rows), visible=len(rows))
    return rows_top, list_x0, list_x1, row_h


def nodes_views():
    """S515/EST-465: the Nodes page with the store view. All three chips in the head band (All active), the head
    right carrying the live counts, and a gold "R" on every radio row. The first row is the one with ALL THREE
    marks: radio (R), favourite (gold dot) and fresh (red dot). Rows 4-6 are archive-only and carry no R."""
    img, d = new_screen()
    W, H = TM['W'], TM['H']
    status_bar(d)
    radio = sum(1 for r in NODES_VIEWS if r[5])
    archive = len(NODES_VIEWS) - radio
    _nodes_head_store(d, W, TM['pad'], radio, archive, 'All')
    rows_top, list_x0, list_x1, row_h = _nodes_store_list(
        d, W, NODES_VIEWS, 0, '%d of %d' % (len(NODES_VIEWS), len(NODES_VIEWS)), 'All')

    work_top = TM['status_h'] + TM['head_h'] + 1
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), 'TEST-A1', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'client · key ' + NODE_KEY + ' · radio · favourite · new', font(FONT_MONO, TM['f_meta']),
         P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in (('TYPE', 'client (companion)'), ('HOPS', 'direct · 0 hops'),
                         ('SNR / RSSI', '3.2 dB / -71 dBm'), ('LAST HEARD', '14 h ago'),
                         ('PUBLIC KEY', NODE_KEY), ('STORE', 'radio'), ('PATH', 'direct')):
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28
    yy += 8
    text(d, (x, yy), 'Enter: actions for this node.', font(FONT_SANS, TM['f_body']), P['Dimmer'])
    text(d, (x, yy + 22), '←→: All / Radio / SD. f: favourite.',
         font(FONT_SANS, TM['f_body']), P['Dimmer'])

    hint_bar(d, 'nodes', [('<>', 'view'), ('Enter', 'menu'), ('/', 'search'), ('esc', 'back'),
                          ('f', 'fav'), ('a', 'advert')], dots=('chat', 'nodes'))
    return save(img, '2026-11-19-nodes-views.png')


def popup_msg():
    """S515/EST-467: an incoming channel message as the chrome pop-up over the Nodes page.

    The first line is the channel and the sender (gold), then the message wrapped to at most three lines
    with an ellipsis; "+1 more" is the message that arrived while this one was up. This is a HOST RENDER of
    the approved panel geometry (main/chrome.cpp drawMessagePopup), not a screenshot.
    """
    img, d = new_screen()
    W, H = TM['W'], TM['H']
    status_bar(d)
    radio = sum(1 for r in NODES_VIEWS if r[5])
    archive = len(NODES_VIEWS) - radio
    _nodes_head_store(d, W, TM['pad'], radio, archive, 'All')
    rows_top, list_x0, list_x1, row_h = _nodes_store_list(
        d, W, NODES_VIEWS, 1, '%d of %d' % (len(NODES_VIEWS), len(NODES_VIEWS)), 'All')

    work_top = TM['status_h'] + TM['head_h'] + 1
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), 'RPT-Sever', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'repeater · key ' + NODE_KEY + ' · radio', font(FONT_MONO, TM['f_meta']), P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in (('TYPE', 'repeater'), ('HOPS', 'path 2 · 2 hops'),
                         ('SNR / RSSI', '7.5 dB / -88 dBm'), ('LAST HEARD', '18 m ago'),
                         ('PUBLIC KEY', NODE_KEY), ('STORE', 'radio'), ('PATH', 'path 2')):
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28

    hint_bar(d, 'nodes', [('<>', 'view'), ('Enter', 'menu'), ('/', 'search'), ('esc', 'back'),
                          ('f', 'fav'), ('a', 'advert')], dots=('chat', 'nodes'))

    # ---- the pop-up panel, centred, drawn over the page exactly like drawMessagePopup() -----------------
    pw = 560
    head = '#Test Channel · TEST-A1'
    msg = ('Hello from the test channel: the new pop-up shows the channel, the sender and the message on any page, '
           'wraps it to three lines and marks anything longer with an ellipsis, and stays up for ten seconds or '
           'until the first key dismisses it.')
    ft = font(FONT_SANS, TM['f_title'])
    fb = font(FONT_SANS, TM['f_body'])
    maxw = pw - 48
    words = msg.split(' ')
    lines, cur = [], ''
    for wd in words:
        trial = wd if not cur else cur + ' ' + wd
        if tw(fb, trial) <= maxw:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = wd
    if cur:
        lines.append(cur)
    clipped = len(lines) > 3
    lines = lines[:3]
    if clipped:
        while lines and tw(fb, lines[-1] + '...') > maxw:
            lines[-1] = lines[-1][:-1]
        lines[-1] = lines[-1] + '...'
    extra = 1
    ph = 20 + 26 + len(lines) * 22 + (22 if extra else 0) + 16
    px, py = (W - pw) // 2, (H - ph) // 2
    # a soft scrim under the plate so the page reads as dimmed, like the device's dither
    d.rectangle([px - 10, py - 10, px + pw + 10, py + ph + 10], outline=P['Line'], width=1)
    d.rounded_rectangle([px, py, px + pw, py + ph], radius=8, fill=P['Panel2'], outline=P['Acc'], width=2)
    text(d, (px + 24, py + 22), head, ft, P['AccInk'], anchor='lm')
    ly = py + 40
    for ln in lines:
        text(d, (px + 24, ly + 10), ln, fb, P['Ink'], anchor='lm')
        ly += 22
    if extra:
        text(d, (px + 24, ly + 10), '+%d more' % extra, fb, P['Dim'], anchor='lm')
    return save(img, '2026-11-19-popup-msg.png')


def node_menu_radio():
    """S515/EST-465: the node menu on an SD-only node — "Add to radio" (never "Move to SD" here), the
    store row sits under Trace, and Delete everywhere is the last row. The list keeps the same R tags."""
    img, d = new_screen()
    W, H = TM['W'], TM['H']
    status_bar(d)
    radio = sum(1 for r in NODES_VIEWS if r[5])
    archive = len(NODES_VIEWS) - radio
    _nodes_head_store(d, W, TM['pad'], radio, archive, 'All')
    _nodes_store_list(d, W, NODES_VIEWS, 3, '%d of %d' % (len(NODES_VIEWS), len(NODES_VIEWS)), 'All')

    work_top = TM['status_h'] + TM['head_h'] + 1
    list_x1 = TM['pad'] + TM['list_w']
    vline(d, list_x1 + 8, work_top, H - TM['hint_h'] - 8, P['Line'])
    x = list_x1 + 8 + 24
    yy = work_top + 18
    text(d, (x, yy), 'TEST-B2', font(FONT_SANS, 26), P['Ink'])
    yy += 34
    text(d, (x, yy), 'client · key ' + NODE_KEY + ' · on SD', font(FONT_MONO, TM['f_meta']), P['Dim'])
    yy += 26
    fk = font(FONT_MONO, TM['f_kv'])
    for label, value in (('TYPE', 'client (companion)'), ('HOPS', 'direct · 0 hops'),
                         ('SNR / RSSI', '4.1 dB / -68 dBm'), ('LAST HEARD', '2 m ago'),
                         ('PUBLIC KEY', NODE_KEY), ('STORE', 'SD'), ('PATH', 'direct')):
        text(d, (x, yy), label, fk, P['Dimmer'])
        text(d, (x + 130, yy), value, fk, P['Ink'])
        yy += 28

    # ---- the sheet: scrim, gold outline, the node's name and facts, seven rows.
    img = Image.blend(img, Image.new('RGB', img.size, '#000000'), 0.62)
    d = ImageDraw.Draw(img)
    fm = font(FONT_MONO, TM['f_meta'])
    sheet_w, row_h, head_h, spad = 560, 42, 64, 16
    rows = [('Direct message', ''), ('Info', ''), ('Ping', ''), ('Trace', ''),
            ('Add to radio', ''), ('Favourite', 'off'), ('Delete everywhere', '')]
    sheet_h = 2 * spad + head_h + len(rows) * row_h
    x0 = (W - sheet_w) // 2
    x1 = x0 + sheet_w
    y0 = max(28, H - TM['hint_h'] - 8 - sheet_h)
    plate(d, [x0, y0, x1, y0 + sheet_h], P['Panel'], P['Acc'], r=0, w=2)
    text(d, (x0 + 20, y0 + spad), 'TEST-B2', font(FONT_SANS, 22), P['Ink'])
    text(d, (x0 + 20, y0 + spad + 34), 'client · last heard 2m ago · on SD', fm, P['Dim'])
    hline(d, x0 + 1, x1 - 1, y0 + spad + head_h - 1, P['Line'])
    ry = y0 + spad + head_h
    for i, (label, value) in enumerate(rows):
        if i == 4:
            focus_band(d, x0 + 8, x1 - 8, ry + 1, ry + row_h - 2)
        text(d, (x0 + 24, ry + row_h // 2), label, font(FONT_SANS, 17),
             P['AccInk'] if i == 4 else P['Ink'], anchor='lm')
        if value:
            text(d, (x1 - 24, ry + row_h // 2), value, font(FONT_MONO, TM['f_hint']), P['Dim'], anchor='rm')
        if i + 1 < len(rows):
            hline(d, x0 + 8, x1 - 8, ry + row_h - 1, P['RowLine'])
        ry += row_h

    hint_bar(d, 'nodes', [('up/dn', 'move'), ('Enter', 'choose'), ('esc', 'close')])
    return save(img, '2026-11-19-node-menu-radio.png')



# ------------------------------------------------------------------------------------ map nodes (S515/EST-466)
# Invented nodes on invented ground (rule 9): the same fake family the Nodes preview uses. The markers are the
# ones main/map_page.cpp draws — a 4 px disc in the Nodes list's own type colour over a 5 px near-black disc, a
# 1 px gold ring for a favourite, a 2 px gold ring for the node the menu centred on — and the plates are the
# core/map_nodes.h geometry (7 px right of the marker, 15 px tall, 4 px of air each side).
MAP_NODE_INK = '#1a1a18'   # main/map_page.cpp kMapVoid: the plate and the marker's outline


def _map_base(z):
    """The Map page's chrome and an invented tile area: the panel is 800x386 under the head (main/map_page.h)."""
    img, d = new_screen()
    W, pad = TM['W'], TM['pad']
    status_bar(d)
    y = TM['status_h']
    text(d, (pad, y + TM['head_h'] // 2), 'Map', font(FONT_SANS, TM['f_title']), P['Ink'], anchor='lm')
    text(d, (W - pad, y + TM['head_h'] // 2), 'z%d' % z, font(FONT_MONO, TM['f_meta']), P['Dim'], anchor='rm')
    hline(d, 0, W, y + TM['head_h'], P['Line'])

    mx0, my0 = 0, y + TM['head_h']
    mx1, my1 = W, TM['H'] - TM['hint_h']
    d.rectangle([mx0, my0, mx1 - 1, my1 - 1], fill='#2b3126')          # land
    d.rectangle([mx0, my0, mx1 - 1, my1 - 1], outline=P['Line'], width=1)
    # fields, a lake and two roads: the invented grid the old map preview used, at the real page's full width
    for bx, by, bw, bh, col in ((0.06, 0.10, 0.16, 0.20, '#3a4433'), (0.30, 0.55, 0.14, 0.22, '#333a2c'),
                                (0.62, 0.62, 0.20, 0.20, '#3a4433'), (0.70, 0.08, 0.18, 0.24, '#333a2c')):
        d.rectangle([mx0 + (mx1 - mx0) * bx, my0 + (my1 - my0) * by,
                     mx0 + (mx1 - mx0) * (bx + bw), my0 + (my1 - my0) * (by + bh)], fill=col)
    d.ellipse([mx0 + 0.44 * (mx1 - mx0), my0 + 0.20 * (my1 - my0),
               mx0 + 0.56 * (mx1 - mx0), my0 + 0.34 * (my1 - my0)], fill='#2b3f4a')   # a lake
    d.line([(mx0 + 0.02 * (mx1 - mx0), my0 + 0.72 * (my1 - my0)),
            (mx0 + 0.40 * (mx1 - mx0), my0 + 0.52 * (my1 - my0)),
            (mx0 + 0.78 * (mx1 - mx0), my0 + 0.44 * (my1 - my0)),
            (mx1 - 10, my0 + 0.50 * (my1 - my0))], fill='#6c7360', width=3, joint='curve')
    d.line([(mx0 + 0.24 * (mx1 - mx0), my0 + 4),
            (mx0 + 0.34 * (mx1 - mx0), my0 + 0.40 * (my1 - my0)),
            (mx0 + 0.30 * (mx1 - mx0), my1 - 4)], fill='#6c7360', width=2, joint='curve')
    return img, d, (mx0, my0, mx1, my1)


def _map_markers(d, area, nodes, labels=False, highlight=None):
    """The markers (and, from z13, the names), in the real order: discs first, then the plates on top."""
    mx0, my0, mx1, my1 = area
    w, h = mx1 - mx0, my1 - my0
    fhint = font(FONT_MONO, TM['f_hint'])
    placed = []
    for name, fx, fy, kind, fav, _heard in nodes:
        px, py = int(mx0 + w * fx), int(my0 + h * fy)
        if highlight == name:
            d.ellipse([px - 10, py - 10, px + 10, py + 10], outline=P['Acc'], width=2)
        d.ellipse([px - 5, py - 5, px + 5, py + 5], fill=MAP_NODE_INK)          # the 1 px outline
        d.ellipse([px - 4, py - 4, px + 4, py + 4],
                  fill=P['Client'] if kind == 'client' else (P['Green'] if kind == 'repeater' else P['Warn']))
        if fav:
            d.ellipse([px - 6, py - 6, px + 6, py + 6], outline=P['Acc'], width=1)
        nodes_name = name
        if labels:
            tw_ = tw(fhint, nodes_name)
            box = [px + 7, py - 7, px + 7 + tw_ + 8, py + 8]
            # the greedy: a plate that overlaps one already drawn is skipped, the marker is not
            if any(not (box[2] <= b[0] or b[2] <= box[0] or box[3] <= b[1] or b[3] <= box[1]) for b in placed):
                continue
            if box[2] > mx1 or box[3] > my1:
                continue
            placed.append(box)
            d.rectangle(box, fill=MAP_NODE_INK)
            text(d, (box[0] + 4, box[1] + 2), nodes_name, fhint, P['Ink'])


def map_nodes_z12():
    """S515/EST-466, item 6: z12 — MARKERS, no labels (the names start at z13). Six positioned nodes: clients,
    a repeater, a room, and one favourite with its gold ring. Nothing here waits for a tile: this is the layer
    that is drawn over decoded tiles, stand-ins and the void alike."""
    img, d, area = _map_base(12)
    _map_markers(d, area, MAP_NODES_Z12)
    text(d, (area[0] + 12, area[3] - 26), 'Esri, HERE, Garmin, OSM', font(FONT_MONO, TM['f_label']),
         P['Dim'])
    hint_bar(d, 'map', [(',', 'settings'), ('dpad', 'pan'), ('+/-', 'zoom'), ('esc', 'back')])
    return save(img, '2026-11-19-map-nodes.png')


def map_nodes_z14():
    """S515/EST-466, item 6: z14 — labels from z13 up, and the node menu's Map item's highlight. Two clients sit
    almost on top of each other (TEST-B2 and TEST-C3): the favourite's plate wins and the other name is SKIPPED
    while its marker is still drawn. TEST-A1 carries the menu's 10 px gold ring (until the user pans or zooms)."""
    img, d, area = _map_base(14)
    _map_markers(d, area, MAP_NODES_Z14, labels=True, highlight='TEST-A1')
    text(d, (area[0] + 12, area[3] - 26), 'Esri, HERE, Garmin, OSM', font(FONT_MONO, TM['f_label']),
         P['Dim'])
    hint_bar(d, 'map', [(',', 'settings'), ('dpad', 'pan'), ('+/-', 'zoom'), ('esc', 'back')])
    return save(img, '2026-11-19-map-nodes-z14.png')


MAP_NODES_Z12 = [
    ('TEST-A1', 0.30, 0.30, 'client', True, 40),
    ('TEST-B2', 0.52, 0.62, 'client', False, 300),
    ('TEST-C3', 0.72, 0.26, 'client', False, 900),
    ('RPT-Sever', 0.20, 0.66, 'repeater', False, 120),
    ('RPT-Jug', 0.64, 0.44, 'repeater', False, 2400),
    ('ROOM-Test', 0.44, 0.14, 'room', False, 5000),
]

# z14: the same family, closer in. TEST-B2 and TEST-C3 are 3 px apart so their plates collide.
MAP_NODES_Z14 = [
    ('TEST-A1', 0.33, 0.34, 'client', True, 40),
    ('TEST-B2', 0.50, 0.62, 'client', False, 300),
    ('TEST-C3', 0.51, 0.63, 'client', False, 900),
    ('RPT-Sever', 0.22, 0.70, 'repeater', False, 120),
    ('RPT-Jug', 0.66, 0.46, 'repeater', False, 2400),
    ('ROOM-Test', 0.46, 0.14, 'room', False, 5000),
]

RENDERERS = dict(map_nodes_z12=map_nodes_z12, map_nodes_z14=map_nodes_z14, nodes_views=nodes_views, node_menu_radio=node_menu_radio, launcher=launcher, nodes_fresh=nodes_fresh, hint_dots=hint_dots, menu=menu, nodes=nodes, nodes_search=nodes_search,
                 nodes_favourites=nodes_favourites, node_trace=node_trace, chat=chat, chat2=chat_cards,
                 chat_emoji=chat_emoji,
                 chat_dm=chat_dm_column, chat_meta=chat_meta, dm_enter=dm_enter, map=map_page, rptr=rptr, rptr_list=rptr_list, rptr_status=rptr_status, settings=settings,
                 settings_general=settings_general, settings_omlite=settings_omlite,
                 settings_opstoc=settings_opstoc, settings_chat=settings_chat,
                 settings_nodes=settings_nodes, settings_map=settings_map,
                 settings_sound=settings_sound, settings_sound_picker=settings_sound_picker, popup_msg=popup_msg)


def main(argv):
    names = argv[1:] or list(RENDERERS)
    bad = [n for n in names if n not in RENDERERS]
    if bad:
        raise SystemExit('unknown preview(s): %s (have: %s)' % (', '.join(bad), ', '.join(RENDERERS)))
    for n in names:
        RENDERERS[n]()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
