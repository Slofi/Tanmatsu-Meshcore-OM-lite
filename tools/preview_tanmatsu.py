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


def hint_bar(d, page, actions):
    """The key-hint bar (BRIEF § 5): what the keys do HERE, same style as the L2's tab bar.

    Left: this page's actions. Right: the five symbol keys, the page switch; the current page's key is
    gold, the rest dim. Drawn as chips so it reads as keys, not as prose.
    """
    top = TM['H'] - TM['hint_h']
    d.rectangle([0, top, TM['W'], TM['H']], fill=P['Bar'])
    hline(d, 0, TM['W'], top, P['Line'])
    f = font(FONT_SANS, TM['f_hint'])
    fm = font(FONT_MONO, TM['f_hint'])
    cy = top + TM['hint_h'] // 2

    # right cluster: the five symbol keys
    pages = ['home', 'nodes', 'chat', 'map', 'rptr']
    labels = ['LAUNCHER', 'NODES', 'CHAT', 'MAP', 'RPTR']
    x = TM['W'] - TM['pad']
    for kind, lab, pname in reversed(list(zip(SIGILS, labels, pages))):
        on = (pname == page)
        col = P['Acc'] if on else P['Dimmer']
        text(d, (x, cy), lab, fm, col, anchor='rm')
        x -= tw(fm, lab) + 7
        sigil(d, kind, x - 8, cy, 7, col, width=2)
        x -= 22 if kind != 'cloud' else 24
    x -= 10
    vline(d, x, top + 5, TM['H'] - 6, P['Line'])
    x -= 14

    # left cluster: this page's actions
    for keys, what in reversed(actions):
        text(d, (x, cy), what, f, P['Dimmer'], anchor='rm')
        x -= tw(f, what) + 7
        kw = tw(fm, keys) + 12
        plate(d, [x - kw, cy - 9, x, cy + 9], P['Panel2'], P['KeyLine'], r=3)
        text(d, (x - kw / 2, cy), keys, fm, P['Dim'], anchor='mm')
        x -= kw + 16


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


def launcher():
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

    hint_bar(d, 'home', [('↑↓', 'move'), ('↵', 'open'), ('s', 'stop'), ('esc', 'quit')])
    return save(img, '2026-11-13-launcher-home.png')


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

    hint_bar(d, 'nodes', [('↑↓', 'move'), ('↵', 'info'), ('d', 'DM'), ('t', 'trace')])
    return save(img, '2026-11-13-nodes.png')


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
    plate(d, [x + rw - 52, ch, x + rw, ch + 40], P['SelBg2'], P['Acc'], r=4)
    text(d, (x + rw - 26, ch + 20), 'Send', font(FONT_SANS, TM['f_body']), P['AccInk'], anchor='mm')

    hint_bar(d, 'chat', [('↑↓', 'move'), ('↵', 'open'), ('r', 'reply'), ('esc', 'back')])
    return save(img, '2026-11-13-chat.png')


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

    hint_bar(d, 'chat', [('↑↓', 'move'), ('←→', 'column'), ('↵', 'open'), ('esc', 'back')])
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

    hint_bar(d, 'chat', [('↑↓', 'move'), ('←→', 'column'), ('↵', 'open'), ('esc', 'back')])
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

    hint_bar(d, 'rptr', [('↑↓', 'move'), ('↵', 'open'), ('s', 'scan'), ('esc', 'back')])
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

    Same rule as the L2's `ui/settings_actions.h`: the question is "Change X to Y?", **Cancel sits on the
    LEFT (the default side) and Apply on the right**, Cancel answers None and changes NOTHING, only Apply
    runs the change. The keyboard translation of the same rule is on the keys: `esc` on Cancel, `↵` on
    Apply — so a mistaken key press cannot change a setting (BRIEF § 5).
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
    text(d, (cx + 18, by + bh // 2), 'Cancel', fab, P['Ink'], anchor='lm')
    text(d, (cx + bw - 18, by + bh // 2), 'esc', fb, P['Dim'], anchor='rm')
    # Apply: right, gold, the only filled plate on the screen
    ax = x1 - 24 - bw
    plate(d, [ax, by, ax + bw, by + bh], P['SelBg2'], P['Acc'], r=6)
    text(d, (ax + 18, by + bh // 2), 'Apply', fab, P['AccInk'], anchor='lm')
    text(d, (ax + bw - 18, by + bh // 2), '↵', fb, P['Acc'], anchor='rm')
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
    status_bar(d)

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
             [('esc', 'cancel'), ('↵', 'apply'), ('↑↓', 'move')])
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

    hint_bar(d, 'home', [('←→', 'select'), ('↵', 'open'), ('s', 'stop'), ('esc', 'quit')])
    return save(img, '2026-11-18-menu-cards.png')


# ------------------------------------------------------------------------------------------------ dm send
DM_THREAD = [
    # stamp, the delivery state drawn after OUR time (None for an incoming line), who, body, mine
    ('09:41', None, 'Alpha', 'Are you near the north repeater today?', False),
    ('09:43', None, 'Alpha', 'The path came back with two hops, so it should work.', False),
    ('09:48', 'delivered', 'you', 'Yes, on my way after lunch.', True),
    ('09:52', 'sending', 'you', 'I will ping you when I am there.', True),
]


def dm_send():
    """OM Lite > Chat > DM thread, SENDING (S513/EST-439, items 3 and 4).

    The open DM view: our own messages on the right under "you", each with its state AFTER the time — "delivered"
    once the ACK came back, "sending" until it does (and "no confirmation" after the last attempt, which the
    drawing code takes from the L2's DmState words; the font has no check glyph, so the word is drawn). The
    composer is the channel's own, because a DM now types and sends the same way.
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

    # the composer, the same one a channel has
    ch = H - TM['hint_h'] - 56
    plate(d, [pad, ch, W - pad, ch + 44], P['FieldBg'], P['Line'], r=4)
    text(d, (pad + 12, ch + 22), 'message this contact, Enter sends', font(FONT_MONO, TM['f_meta']),
         P['Dimmer'], anchor='lm')
    plate(d, [W - pad - 94, ch, W - pad - 8, ch + 44], P['SelBg2'], P['Acc'], r=4)
    text(d, (W - pad - 51, ch + 22), 'Send', font(FONT_SANS, TM['f_body']), P['AccInk'], anchor='mm')

    hint_bar(d, 'chat', [('type', 'write'), ('↑↓', 'scroll'), ('↵', 'send'), ('esc', 'back')])
    return save(img, '2026-11-18-dm-send.png')


RENDERERS = dict(launcher=launcher, menu=menu, nodes=nodes, chat=chat, chat2=chat_cards,
                 chat_dm=chat_dm_column, dm_send=dm_send, map=map_page, rptr=rptr, settings=settings)


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
