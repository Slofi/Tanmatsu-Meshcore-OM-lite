# OM Lite on the Tanmatsu: progress

> **Publication note (2026-11-15, S503).** This repository is public, so three things were genericised before the
> first push: the **channel name** (`Test Channel` stands in for the real one), the **place** ("central Slovenia"
> stands in for the city), and the **reference coordinates and the tile numbers that follow from them**
> (46.15 N / 14.99 E, z16 tile 35496/23275). Quoted log lines were rewritten to match, so they are faithful in
> shape and numbers but are not a byte-for-byte transcript of the original runs. **Nothing technical was removed:**
> every measurement, defect, correction and open item is here. The unedited log lives in the private notes.

# Tanmatsu: PROGRESS

> Discipline copied from `firmware/docs/REBUILD.md` § "How to work": newest entry on top, real exit codes,
> what was read vs. what was assumed, what could NOT be checked, and the next step. One shared preview
> tool, no per-ticket scripts.
>
> **Written by DeepSeek (S499, EST-424 ticket run), 2026-11-13.** Fenced Paperclip run: no session start/end.

## S516 (Claude) v79: Left/Right hint chips drawn as arrows

Filip (23:3x): *"Node page has < > in the tips bar, and Chat has l/r there.. change both to arrows pls.."*. The hint face (`sans_hint11.bin`) has no U+2190/2192 (checked in its glyph table, nor U+2194/2191/2193/21B5), so `drawKeyCap()` draws a chip labelled "l/r" or "<>" as two filled triangles (5 px half-height, tips 18 px apart), like the Enter cap's drawn return arrow. Applies to every page that uses such a chip (Nodes, Chat, Launcher, RPTR, Settings). Suite exit 0, build exit 0.

## S516 (EST-482) top-bar notifications, page keys only on the Launcher, map head removed

> Written by **DeepSeek** for ticket **EST-482**: fenced Paperclip run, no session start or end, nothing
> installed, flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point
> **v77 `4b672cc`**, tree clean, **not installed** (v76 runs on the device). **RESULT: version 78; nothing
> installed, flashed, pushed or committed.**

### What it does

**1. Page keys only on the Launcher.** `main/chrome.cpp::drawHintBar` draws the six sigils (+ labels) only when
`active == Page::Launcher`; every other page's bar is its hints alone and may use the full 18…782 px. The
`compact_page_keys` parameter (EST-480) is gone from the header, the implementation and the Nodes caller. **No
page gets a dropped hint back, because no hint array had been shortened:** the only compression ever applied was
the page-key *labels*/sigils themselves; the Nodes list already carried all six hints and had used `compact` so
they could fit beside the cluster. Measured on `sans_hint11.bin`, every page's full array now ends well inside
the bar:

| bar | ends at x | spare to 782 |
|---|---|---|
| Nodes list (6 chips) | 501 | 281 |
| Chat list (4) | 375 | 407 |
| Settings (4) | 386 | 396 |
| Rptr list (4) | 363 | 419 |
| Map (3) | 260 | 522 |

(The menu/search/sel/mention variants are shorter still; none drops a chip.)

**2. Notification icons in the TOP status bar; no red dots.** The v57/EST-461 hint-bar dots are removed
(`s.disc(..., kBad)` is gone from `drawHintBar`). `main/chrome.cpp::drawStatusBar` now draws a **speech bubble**
while any channel or DM has unread and a **node/location pin** while any fresh node is unacked, from the same
`statusIconMask()` rule and the same store counters the dots used (now in `core/nodes_fresh.h`). **Placement:
FIXED slots after the clock** — centres x=66 and x=88, 22 px apart, because the widest clock ("23:14" on the real
hint face) is 32 px from x=18 and ends at 50. An icon appearing or going therefore moves nothing: the clock, the
radio dot, the wifi sigil, the battery and the gear keep their exact positions. Both icons are drawn in the
status bar's **accent** colour (`kAcc`, not red), 15×11 and 13×14 px inside the 26 px bar. The refresh path is
unchanged: `main.cpp` computes the mask once per loop pass and `statusIconsNeedsRepaint()` raises `dirty`, so a
message arriving on any page shows the icon with no key press. Every page, including the Launcher, already calls
`drawStatusBar()`. **The Nodes list's per-row new-node dot STAYS:** it is drawn by its own `s.disc(...)` in
`main/nodes_page.cpp:217`, not through the removed chrome dot code — only the primitive `Screen::disc` is shared.
The Chat list's red unread badge is also unchanged: it carries a COUNT on each card, not this notification.

**3. The Map page head is removed.** `main/map_page.cpp` no longer calls `drawPageHead("Map", "zN")`. The canvas
now starts at the status bar's bottom and is 44 px taller: `main/map_page.h` has
`kMapAreaY = 26` (= `kTmStatusH`) and `kMapAreaH = 480 - 26 - 24 = 430`. The zoom level is a small house plate
(fill `kPanel2`, 1 px `kLine`, radius 4, 20 px tall) in the map's **TOP-RIGHT corner**, drawn after the tiles,
node markers and labels so a name plate cannot hide it; the crosshair is centred and the attribution is
bottom-right, so nothing else collides. **Tiles: 800×430 is 3.13×1.68 tiles, so a view needs 4–5 columns × 2–3
rows = 8…15 tiles.** The 430 px height can never need a fourth row (2×256 = 512 > 430); compared with the old
386, the third row starts at `shift_y` 83 instead of 127. The cache keeps `kCacheSlots = 20`, i.e. **5 slots of
headroom over a full 5×3 view**, exactly as before. Every user was checked: the tile fill/stand-ins clip against
`kMapAreaY/H`, `viewTilesFor`/`panBy`/`zoomTo`/`clampWorldPixel`/`centreOnNode` take the same constants from
`main.cpp`, the marker label greedy and cull use the new height, and no other y offset assumed the head (the only
absolute offsets are relative to `kMapAreaY`: +4 and +28 in the page). The map's own hint bar is unchanged (three
keys, now without the sigils).

**4. Wi-Fi hint wording.** General › WI-FI › "Wi-Fi at boot" (Off) now says the stock launcher's own sentence:
*"Off: the stock launcher does not connect at start; Repository and Update firmware / radio override this"*.
Measured on the real Meta face (`sans_meta12.bin`) it is **619 px** from x=119; the right-aligned "Off" starts at
759, so there are **640 px available** and **21 px clear**. Preview checked at the same geometry
(`shots/2026-11-20-settings-wifi-hint.png`); the DejaVu stand-in is 616 px and also clears the value.

**5. The Launcher's Settings tile is removed.** The full-width Settings bar under the cards is gone (its 56 px
height and the `kTmMenuSettingsH` constant), Settings remains the **diamond symbol key**, and
`kMenuSettingsFocus` / `MenuAction::OpenSettings` / `menuEnterActionAt()` / its `main.cpp` Enter branch are
deleted. `menuFocusMove` now clamps on the three cards: Left/Right stop at the ends, Up/Down stay put. The cards
plus their one dim "owns" line are **re-centred** in the 386 px work box (70…456): group height
232+28+24 = 284, so the card top moves from 92 to **121**, leaving 51 px above and below. The host tests cover the
new moves.

### Numbers

`sh test/host/run.sh` **exit 0**. `make build` **exit 0**, `application.bin` **1,758,240 B (0x1AD420)**, **+112 B**
over v77's recorded hex `0x1AD3B0` (the old PROGRESS line wrote that as 1,758,160 B, 32 B above its own hex — a
pre-existing transcription slip, noted rather than copied). Only the pre-existing warning remains
(`main.cpp:2232 unused variable 'order'`). **Two arms, each reverted:**
* old map-head constants restored in `main/map_page.h` (`kMapAreaY = 70`, `kMapAreaH = 386`) →
  `FAIL test/host/test_map_nodes.cpp:92  om::ui::kMapAreaY == om::ui::kTmStatusH (got 70, want 26)`
  (`map nodes: 173 checks, 1 FAILURES`, exit 1);
* the two status-icon bits swapped behind `statusIconMask()` →
  `FAIL test/host/test_nodes_fresh.cpp:68  om::ui::kStatusIconBitChat == 1u << 0 (got 2, want 1)`
  `FAIL test/host/test_nodes_fresh.cpp:69  om::ui::kStatusIconBitNode == 1u << 1 (got 4, want 2)`
  `FAIL test/host/test_nodes_fresh.cpp:70  om::ui::statusIconMask(1, 1, 1) & 0xFCu == 0u (got 4, want 0)`
  (`test_nodes_fresh: 112 checks, 3 failures`, exit 1).
Restored: `test_map_nodes` 180 checks, `test_nodes_fresh` 112, the menu 22, Settings 297, suite exit 0.

The new host test `testMapAreaMeetsChrome()` (in `test_map_nodes.cpp`) takes the real `kMapArea*` from
`main/map_page.h` and the chrome from `main/tm_theme.h` (run.sh now passes `-I ../../main`) and checks
`kMapAreaY == kTmStatusH`, `kMapAreaH == 430`, and `kMapAreaY + kMapAreaH == kTmH - kTmHintH` — the canvas top is
the status bar's bottom and its bottom is the hint bar's top. `test_nodes_map.cpp`'s saved-view case moved to the
new 430 px area (centre 2215; at z17 4215, at z15 892). The map-nodes cull/label/timing tests now run against the
real 800×430 (2,000 rows still 254–279 µs).

**Previews:** `shots/2026-11-20-topbar-notify.png` (the status bar with both icons on the Launcher),
`-map-nohead.png` (the taller map, z16 plate top-right, no head, no page keys),
`-launcher-nosettings.png` (three cards re-centred, no Settings tile, page keys on the Launcher only) and
`-settings-wifi-hint.png` (the new sentence beside "Off"). The preview tool now draws the status-bar icons and
the page keys only on `'home'`, and `_map_base()` has no head; all 51 renderers were re-run into `/tmp` to prove
the tool still runs.

### What was read
`docs/PROGRESS.md` (the S516 entries at the top), `main/chrome.{h,cpp}`, `main/map_page.{h,cpp}`,
`main/launcher_page.{h,cpp}`, `main/nodes_page.cpp`, `main/main.cpp` (the launcher key branch, the map start/pan/
zoom/centre code, the notification mask, Settings › General), `main/settings_page.cpp`, `core/menu_cards.{h,cpp}`,
`core/nodes_fresh.h`, `core/map_view.{h,cpp}`, `core/map_nodes.h`, `core/settings.h`, `test/host/run.sh` and the
map/menu/nodes/Settings tests, `tools/preview_tanmatsu.py`, and the device font blobs `sans_meta12.bin` /
`sans_hint11.bin` (widths by the same VLW parser the preview tool uses).

### What was NOT checked (honest list)
* **Nothing is on glass.** All four previews and every geometry above are host renders; v76 still runs on the
  device.
* The status-bar icon shapes are new vector drawings; their legibility at 1:1 on the panel was not measured,
  only rendered at 800×480.
* The map's saved last view is stored as a top-left world pixel, so a view saved with the OLD 386 px area and
  restored with the new 430 px one is centred 22 px lower rather than re-migrated; the ticket did not ask for a
  migration and the format is unchanged.
* The Wi-Fi sentence is measured in the device face by the font parser, not read off the glass; the preview uses
  DejaVu and is 3 px narrower.


**Claude's review of EST-482 (S516, 2026-11-20 23:2x):** previews checked (top-bar bubble + pin in fixed slots after the clock, no red dots; the map from the status bar to the hint bar with the z plate top-right; the launcher without the Settings tile). Map: 20 cache slots for at most 15 visible tiles at 430 px; the canvas-edge test is armed. Known, accepted: a map view saved under v77 restores 22 px off-centre once. Accepted. Suite exit 0, build exit 0.
## S516 (EST-481) @[Name] mentions

> Written by **DeepSeek** for ticket **EST-481**: fenced Paperclip run, no session start or end, nothing
> installed, flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point
> **v76 `0b6efb8`**, tree clean, **not installed** (v75 runs on the device). **RESULT: version 77; nothing
> installed, flashed, pushed or committed.**

Mentions in the MeshCore form. **The evidence is Claude's, 2026-11-20:** of 4,811 MeshCore messages stored by
OverMesh on gandalf, **1,044 contain `@[Name]`** and **15 a plain `@Name`**; OverMesh's own reply writes
`@[${name}] ` with `[` `]` CR LF removed and the result trimmed. Our reply used to write `@Name ` (the L2's older
plain form). **That file was NOT readable from this sandbox:** reading `~/overmesh/static/js/app.js` on gandalf
as `ds-ro` answers *No such file or directory*, so the cleaning rule below follows the ticket's quotation, not
a re-read of OverMesh. The L2's plain-form separator rule WAS re-read
(`firmware/src/mesh_link.cpp:650-690`, read-only, allowed).

**What it does**
1. **Reply** (channel only; a DM's reply adds no prefix): `chatReplyPrefix` now writes **`@[Name] `** through
   `core/chat_mentions.cpp::mentionReplyPrefix`: `[` `]` CR LF are removed, the result is trimmed, and a name that
   cleans to nothing writes **nothing** (never `@[] `). A buffer that cannot hold the whole prefix writes nothing
   either — a cut name would mention the wrong node.
2. **Drawing a message**: a valid `@[Name]` token (at byte 0 or after a space) is drawn as **`@Name`** in the
   accent colour. The stored text is NEVER changed: the chat view builds a separate DRAWN copy and carries the
   token's byte spans; the ring, the card log and Info keep the exact bytes. **Widths stay right because the wrap
   measures the DRAWN copy** (`main.cpp` transforms, then `core/chat_layout::wrapText` measures that text), and
   `drawMsgLine` draws the same bytes in pieces at the font's own advances.
3. **"mentions you"**: `mentionsUs` accepts `@[<our name>]` (exact, ASCII case-insensitive) and the plain
   `@<our name>` followed by end/space/`,.!?:;` (the L2 rule), both only at the position rule. The new-message
   pop-up's 64-byte head gains ` · mentions you` (1+20+4+20+16+NUL = 62, so it fits). Our own sends never pop
   (the seen ring already drops our echo). **Sound: NOT changed.** `core/sound_events.h` has six events and no
   Mention (Boot, Message, Dm, Sent, Error, NewNode). The L2's shared synth does have
   `om::sound::Event::Mention` (default Chime Bright), but this app's event set, its 9-byte settings blob and its
   Settings rows do not; adding one would be a new event + a settings-format change, which the ticket forbids
   here. The message/DM sound plays as before.
4. **Typing + name picker** (channel AND DM composers; not Nodes search/name/RPTR fields):
   `core/chat_mention_edit.h` owns every keystroke. A typed `@` at the position rule becomes `@[]` with the caret
   between the brackets; mid-word it is a plain `@`. Letters go before the auto `]`; `]` steps over it (never
   `]]`); Right arrow (or Enter with no suggestion) leaves the mode keeping what was typed; Esc closes the
   pop-up only, a second Esc does today's Esc. **Enter takes the highlighted name** and never sends while the mode
   is on. Backspace on an EMPTY `@[]` removes both brackets and leaves a plain `@` (mode ends). The picker shows
   up to **5** names matching the typed letters (case-insensitive substring; nothing typed = the list), ordered
   **favourites, then radio, then SD/session rows**, most recently heard inside a group; each row shows `fav` /
   `radio`. **Overflow:** a picked name that does not fit is **REFUSED** with the 3 s notice *"no room for that
   name"* (not cut — a half name would mention the wrong node); `@[]` is never opened when it would not fit (the
   `@` stays plain). **The cursor:** the composer was append-only, so the minimum is one bool `chat_mention` with
   the invariant that while it is true the draft's TAIL is `@[typed]` and the caret sits before the auto `]`;
   otherwise the caret is at the end. There is no arbitrary editing and no second cursor.
5. **Pure + tested:** `test/host/test_chat_mentions.cpp` — **147 checks** over the prefix and its cleaning, the
   finder (start/after space/mid-word, unclosed, nested, empty, UTF-8), `mentionsUs` (both forms, case,
   separators, `TNMT (SI)` with spaces and parentheses, a longer name NOT us), the drawn form and its spans, the
   keystroke rules, the byte limit, and the ranking (substring, case, favourites, max 5, duplicates). `run.sh`
   runs it plain **and under ASan/UBSan**. `test_chat_select.cpp`'s reply-prefix case is updated to the bracket
   form.

**The lock rule:** the suggestion scan holds `StoreLock` and copies `name`/`favourite`/`in_radio`/`heard_at` into
`MentionRow`; nothing that points into the node table survives the unlock (the S516 round 1 class).

**One pre-existing defect this ticket HAD to fix to build:** `main/chrome.cpp` included `core/nodes_fresh.h`
*inside* `namespace om::ui`. That only compiled because `nodes_fresh.h`'s own `#include "mc_store.h"` had not been
seen yet, so `mc_store` was nested into `om::ui::om::mc` with it; once an earlier include pulls `mc_store.h`
globally (this ticket's `chat_mentions.h` does), the nested copy disappears and the file fails with
`'om::ui::om::mc' has not been declared`. The include is moved to the file's global include block; chrome.cpp only
uses the two `kHintDot*` constants from it, so nothing else changes.

**Numbers:** `sh test/host/run.sh` **exit 0**; the new suite **147/147** in both the plain and the ASan stage.
`make build` **exit 0**, `application.bin` **1,758,160 B (0x1AD3B0)**, **+5,312 B** over v76's recorded
1,752,848 B; total image **1,757,752 B**; DIRAM **303,884 B used / 268,484 B free (53.09 %)** — **+608 B** over
v76 (the ChatView's per-message mention spans and the picker's five copied rows; the 8-message view is static).
**Two arms**, each reverted:
* boundary check removed from the finder →
  `FAIL test/host/test_chat_mentions.cpp:101  !om::ui::mentionFind("a@[b]", 0, &t)`
  `FAIL test/host/test_chat_mentions.cpp:184  "a@b c" != "a@[b] c"` (`128 checks, 2 failure(s)`, exit 1);
* the favourite flag ignored by the ranking →
  `FAIL test/host/test_chat_mentions.cpp:380  "Radio-new" != "New-fav"` (`134 checks, 1 failure(s)`, exit 1).
Restored: 147/147, exit 0.

**Previews:** `shots/2026-11-20-chat-mentions.png` (a message with `@Beta` in the accent, one mentioning us as
`@TNMT (SI)`, and the pop-up saying *mentions you*) and `shots/2026-11-20-mention-picker.png` (the composer in
mention mode — `hello @[Al]`, caret before the `]` — with the suggestion pop-up above it). The pop-up's own text
is drawn in the same `@Name` form (a display copy in `msgPopupOffer`), so one message cannot read two ways on one
screen.

### What was read
`core/chat_select.{h,cpp}`, `core/chat_layout.{h,cpp}`, `core/chat_page.h`, `main/chat_page.cpp`,
`core/msg_popup.h`, `main/chrome.{h,cpp}`, `core/sound_events.h` and `firmware/src/sound_synth.{h,cpp}`,
`main/mc_link.cpp` (the channel/DM RX paths and the pop-up offers), `main/main.cpp` (the chat state, the view
build, the composer key branch, the module's onTop hook), `core/mc_store.h`, `core/node_table.h`,
`firmware/src/mesh_link.cpp:650-690` (the L2's plain mention rule), `test/host/run.sh`,
`test/host/test_chat_select.cpp`, `tools/preview_tanmatsu.py`.

### What was NOT checked (honest list)
* **Nothing is on glass.** The picker geometry, the caret between the brackets, the mention accent on the real
  panel and the pop-up's width are host renders only (v75 still runs on the device).
* The 1,044 / 15 / 4,811 counts are Claude's read-only count; **the OverMesh source is not visible from this
  sandbox as `ds-ro`** (empty answer, quoted above), so the cleaning rule is the ticket's quotation.
* The suggestion scan is O(nodes) with a 5-slot insertion; 2,000 rows were not timed here (the node-table sort
  timing from EST-463 is the only nearby number).
* The `mentions you` head fits 64 bytes by arithmetic (62 with both names cut to 20 bytes), not by a glass
  measurement; the pop-up uses the existing `Face::Title` fit.


**Claude's review of EST-481 (S516, 2026-11-20 23:1x):** accepted: reply `@[Name] `, the drawn copy + spans (wrap measures the drawn text, stored bytes untouched), `mentionsUs` with the position rule, the picker (StoreLock + copied rows, Enter never sends, Right/`]` step over, Backspace on empty `@[]`), and the include fix in chrome.cpp. Stack checked: `ChatView` is now 5,280 B but static since v71; the largest frames are unchanged (`app_main` 2,224, `chatInfoRows` 1,936). **One drawing fix by Claude:** the mention caret was drawn 3 px after the text before the auto `]`, on top of the bracket's thin stroke, so the preview read `hello @[Al|` as if the `]` were missing; the composer now draws text-before, caret, then `]` 4 px right. The picker's tag sits 8 px inside the highlight frame on the device (the preview tool draws it differently). No mention SOUND (this app's sound events have none; noted for the sound-levels work). Suite exit 0, build exit 0.
## S516 (EST-480) Wi-Fi toggle + Nodes hints back

> Written by **DeepSeek** for ticket **EST-480**: fenced Paperclip run, no session start or end, nothing
> installed, flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point
> **v75 `c7e20ab`**, tree clean, **not installed** (v74 runs on the device). **RESULT: version 76; nothing
> installed, flashed, pushed or committed.**

Two small items. **(A)** Settings › General gains **WI-FI › "Wi-Fi at boot"** (Off / On, **default Off**) with a
dim hint while Off: *"Off: the stock launcher does not connect at start; its store and updates still connect
when opened"* (the ticket's sentence; 594 px on the real Meta face, ending 46 px before the right-aligned
"Off"). OUR choice lives in NVS **`mcset`/`wifiboot`** (u8, next to `heardto`/`saveheard`/`newnode`),
never derived from the launcher's key. The launcher's own switch is NVS **`system`/`ntp`** (u8): the stock
launcher connects Wi-Fi at boot ONLY when it is 1; a missing key reads as off (ticket's citation, read-only source at
`/home/slofi/dev/tanmatsu-launcher` @ `3ceac92`: `main/main.c:187`, `main/ntp.c:22-49`; that path is **not
visible from this sandbox**, so the citation is the ticket's, not a re-read). With our setting **Off**, OM Lite
reads `system`/`ntp` at EVERY start and writes **0 only when it is present and not already 0** (absent already
reads as off, so it is left absent); one log line either way. **On** writes 1 once at the switch and never touches
the key at start. No other `system` key is opened, read or written. The decision is pure
(`core/wifi_boot.h`, `startAction`/`switchAction`) and the NVS code is a handful of small functions in
`main.cpp`.
**(B)** The Nodes hint bar carries both shortcut hints again. Measured with the DEVICE's own hint face
(`main/fonts/sans_hint11.bin`, advance sum, the preview tool's own 4/8/10 + 18/14 arithmetic): with the six page
labels the page-key cluster starts at x=350, so the bar could only draw `<> view`, `Enter menu`, `/ search` —
`esc back`, `f fav`, `a advert` were all dropped (only three chips could be drawn). Fallback chosen, in the
ticket's order: **(1)** `a advert` → `a adv` (the two shortcuts are back as `f fav`, `a adv`); **(2)** the label
words of the six page sigils are dropped **on the Nodes page only** (the sigils stay, the active one still
gold). That moves the cluster's start to x=590 and the six action chips end at x=501, an 89 px clear gap
before the sigils — no overlap, nothing cut.
`drawHintBar()` grew an explicit `compact_page_keys` flag; only the Nodes list sets it, so every other page keeps
its labels. Shot: `shots/2026-11-20-nodes-hints.png`.

**Numbers:** `sh test/host/run.sh` **exit 0**; the new `wifi-boot` suite **30 checks, 0 failures**.
`make build` **exit 0**, `application.bin` **1,752,848 B (0x1ABF10)**, **+2,032 B** over v75's recorded
1,750,816 B; total image **1,752,448 B**; DIRAM **303,276 B used / 269,092 B free (52.99 %)** — the same as v75
(no new static RAM: the row and its strings are constants, the helpers are code). One host test armed: deleting the
`ntp != 0` branch gave `FAIL test/host/test_wifi_boot.cpp:82  (int)tm::wifiboot::startAction(Mode::Off, true, 1)
== (int)NtpWrite::Zero (got 0, want 1)` and `wifi-boot: 1/21 checks FAILED` (exit 1); reverted, 30/30 exit 0.
No SSH needed (the device is on Filip's USB; nothing installed).

**Previews:** `shots/2026-11-20-nodes-hints.png` (all six chips + the six unlabelled sigils, 89 px clear) and
`shots/2026-11-20-settings-wifi.png` (General, WI-FI focused, the full hint in the Meta face). `hint_bar` in the
preview tool now measures with `main/fonts/sans_hint11.bin` (the device's own advance values) and mirrors the
`compact` flag; `setrow` draws a row hint in sans at the Meta size, which is the face the panel uses.

### What was read
`main/main.cpp` (`app_main`'s `nvs_flash_init`, `clearLauncherAutostart`, the General sections, the row-handler
dispatch, `settingsKey`/`generalToggle*`), `core/settings.h` (General, `kAppVersion`), `core/settings_model.h`
(the hint/dim row fields), `main/settings_page.cpp` (how a row hint is drawn, with the Body label + Meta hint and
the right-aligned value), `main/mc_link.cpp` (the `mcset`/`mcid` NVS patterns), `main/chrome.{h,cpp}`
(`drawHintBar`'s exact spacing and the six-sigil cluster), `main/nodes_page.cpp` (the `hints_list` array),
`tools/preview_tanmatsu.py` (`hint_bar`, `setrow`, `settings_general`, `nodes_views`), `main/fonts/*.bin` and
`main/font.cpp` (the VLW advance field), `test/host/run.sh`, `test/host/test_volume_popup.cpp` (the house test
shape).

### The implementation
1. **Rule** (`core/wifi_boot.h`): `Mode{Off,On}` with default Off; `modeFromStored` (only 1 is On);
   `startAction(mode, present, ntp)` = Off + present + ntp≠0 → `Zero`, otherwise `Nothing`;
   `switchAction(next)` = On → `One`, Off → `Zero`. The NVS names and keys are constants here so main.cpp and the
   tests cannot disagree.
2. **Shell** (`main/main.cpp`): `wifiBootReadStored`/`wifiBootWriteStored` (`mcset`/`wifiboot`),
   `launcherNtpRead`/`launcherNtpWrite` (`system`/`ntp`, u8, the launcher's own access pattern),
   `wifiBootAtStart()` right after `nvs_flash_init`, and the General row's getter/hint/handler. The handler
   applies the launcher key first, then stores our choice, so a failed launcher write never leaves our UI
   claiming a mode the key did not get.
3. **Row**: new section **WI-FI** after ADVERT, one Toggle row `kSetRowWifiBoot` = 40. The hint is shown only
   while Off (it starts with "Off:"); On shows no hint.
4. **Host test** (`test/host/test_wifi_boot.cpp`): the ticket's five cases plus the missing-key, default and
   stored-mapping pins; `run.sh` compiles and runs it with `-Wall -Wextra -Werror -O2`.


**Claude's review of EST-480 (S516, 2026-11-20 22:5x):** the rule (`core/wifi_boot.h`) writes system/ntp only when it is present and 1 (Off at start), 1 once on switching On, 0 once on switching Off; our choice lives in mcset/wifiboot; no other `system` key is opened. Hints: preview checked, all six hint chips fit, the page keys are symbols-only on Nodes. Accepted. Suite exit 0, build exit 0.
## S516 (EST-479) message LED colour preview

> Written by **DeepSeek** for ticket **EST-479**: fenced Paperclip run, no session start or end, nothing
> installed, flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point
> **v74 `ef99625`**, tree clean and INSTALLED. **RESULT: version 75; nothing installed, flashed, pushed or
> committed.**

Filip (22:2x, on v74): *"when changing the new MSG LED colour, it should 'demo' the colour for a few seconds on
change/'scroll'"*. Every step of Settings › General › LED › **Message LED colour** now holds LED 3 (index 2)
**steady in the NEW colour for 3 s**, then hands it back to whatever the normal rule says (off, or the unread
fade). A further step restarts the 3 s. Both modes: automatic writes register 146 with the new colour A and **no
fade bit**; manual lights the app pixel at full brightness. Locked has no Settings, so the rule ignores the
deadline while locked; leaving Settings clears it in `publish()`; `restoreAutomatic()` clears it on exit. Power
LED / Radio LED already show their On→Off→On effect on the next 25 ms step (automatic ↔ manual, emulated pixel),
so no change was needed there.

**Numbers:** `sh test/host/run.sh` **exit 0**; `message-led` **351 checks, 0 failures** (the new section 10 is the
preview). `make build` **exit 0**, `application.bin` **1,750,816 B (0x1AB720)**, **+1,856 B** over v73's
recorded 1,748,960 B (v74's own bin size was not recorded); total image **1,750,408 B**; DIRAM **303,276 B used /
269,092 B free (52.99 %)**. One host test armed: taking the preview out of the automatic register branch gave
`FAIL test_message_led.cpp:407  f.message == 0x01 (got 0, want 1)` and `message-led: 1/290 checks FAILED`
(exit 1); reverted, suite re-run exit 0. No SSH needed (the device is on Filip's USB).

### What was read
`core/message_led_rule.h` (the whole rule, the register-146 bits and the manual fade), `main/message_led.h/.cpp`
(the NVS setters, the 25 ms task and `restoreAutomatic()`), `main/main.cpp` (`generalCycleMessageColour`, the
Settings key branch, the `LeaveSettings` event, `openSettings`, `leaveToLauncher` and the 250 ms `publish()`
call), `core/settings.h` (`kAppVersion`), `test/host/test_message_led.cpp`, `test/host/run.sh`.

### The implementation
1. **The pure rule** (`core/message_led_rule.h`): `kMessageLedPreviewMs = 3000`; `ledPreviewActive(now,
   preview_until_ms)` is a wrap-safe `int32_t` difference; `LedIn::preview_until_ms` (0 = none);
   `messageRegisterSteady(c)` = colour A bits only; the step lights the preview when it is active and the screen
   is NOT locked, overriding unread-off and unread-fade in automatic mode and overriding the breath in manual;
   the manual preview is full 255, and the breath is reset so the normal fade starts fresh afterwards.
2. **The shell** (`main/message_led.h/.cpp`): `previewColour()` sets one deadline (`esp_timer now + 3000 ms`);
   the task publishes it in `LedIn`; `publish()` clears it whenever the Settings page is not current, so any way
   out of Settings ends it; `setScreenOff(true)` and `restoreAutomatic()` clear it as well.
3. **The row** (`main/main.cpp::generalCycleMessageColour`): after a successful `setColour(next)` it calls
   `om::led::previewColour()`; a failure still shows the "not saved" notice and starts no preview. The main-loop
   `publish()` now also passes whether the current page is Settings.
4. **Host tests** (`test/host/test_message_led.cpp`): the steady register bytes, preview over unread-off and
   over unread-fade (automatic), preview over the manual fade and over manual-off, restart on a second step,
   the exact 3 s end, locked (blink, never the steady preview) and `restore()` ending it.


**Claude's review of EST-479 (S516, 2026-11-20 22:3x):** accepted (steady colour A without the fade bit in automatic mode, full pixel in manual, deadline restarted on each step, cleared on leaving Settings, on lock and at exit). **One defect fixed by Claude:** `ledPreviewActive()` read the "no preview" value 0 as a deadline AHEAD once the ms clock passed 2^31 (24.8 days up), so LED 3 would have lit steady for no reason; 0 is now explicit. Test armed: `FAIL test_message_led.cpp:519 !om::ui::ledPreviewActive(0x80000010u, 0)` without the fix. Suite exit 0, build exit 0.
## S516 (Claude) v74: Esc on our Launcher asks before leaving

Filip (21:4x): *"ESC on our launcher restarts Tanmatsu.. So pls add confirmation pop-up when pressing ESC on the Launcher page.."*, then *"no, this time 'OK' can be default"*. Esc on the Launcher now opens the shared confirm sheet (`drawConfirmSheet`, a 96 B SettingsView on the stack) "Leave OM Lite?" with **Leave focused** (the one sheet that does not start on Cancel); Left/Right move, Enter answers, a second Esc cancels, every other key is swallowed while the sheet is up, and the sheet closes on any page change (a page key). `leaveToLauncher()` itself is unchanged. Suite exit 0, build exit 0.

## 2026-11-20 · S516 (EST-478) volume keys

> Written by **DeepSeek** for ticket **EST-478**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point **v72 `927bef4`**,
> tree clean, v72 **not installed** (v71 is what the device runs). **RESULT: version 73; nothing installed, flashed,
> pushed or committed.** Part 1 is the volume keys + on-screen volume pop-up; part 2 moves the function keys one
> up (X = Launcher … diamond = Settings, `,` retired as the Settings key).
>
> **Numbers:** `sh test/host/run.sh` **exit 0** (the new `volume-popup` suite is **67 checks, 0 failures**;
> `test_core` is **312 checks, 0 failures**). `make build` **exit 0**, `application.bin`
> **1,748,960 B (0x1AB3E0)**, **+656 B** over v72's 1,748,304 B; total image **1,749,586 B**; DIRAM
> **303,268 B used / 269,100 B free (52.98 %)**. Two host tests armed, exact FAIL lines below. `.git` untouched,
> no commit; no SSH needed (the device is on Filip's USB).

### What was read
`core/keys.h`, `core/sound_events.h`, `core/settings.h` (the SetKey rules at 296/411/499), `core/msg_popup.h`,
`core/screen_power.h`; `main/main.cpp` (mapKey, the input loop, `onTop`, `openSettings`, the Settings branch and
every text field), `main/chrome.h/.cpp`, `main/sound_store.h/.cpp`, `main/audio_out.h/.cpp`, and the hint arrays
in `main/launcher_page.cpp`, `main/nodes_page.cpp`, `main/chat_page.cpp`, `main/map_page.cpp`,
`main/settings_page.cpp`, `main/rptr_page.cpp`. BSP: `badge-bsp/bsp/input.h:154-200` (the enum counted in order:
NONE 0 … SPACE_R 19, F1 20 … F12 31, GAMEPAD_A 32 … JOYSTICK_PRESS 36, VOLUME_UP 37, VOLUME_DOWN 38) and
`targets/tanmatsu/badge_bsp_input.c` (the volume handlers at 57-71/263-265, the repeat thread commented out at
447-477). Tests: `test/host/run.sh`, `test_core.cpp`; preview `tools/preview_tanmatsu.py`; docs `PLAN.md`,
`BRIEF.md`, `DEVICE.md`; the S516 entries at the top of this file.

### The implementation, rule by rule
**Part 1 — volume keys.**
1. **The keys.** `Key::VolUp`/`Key::VolDown` were added to `core/keys.h` with codes **37/38** (the BSP enum's
   values, counted in order — not guesses; the ticket's "nav key=37" log is VOLUME_UP, VOLUME_DOWN 38 has not
   been seen on the glass yet). They are NOT page keys. `main.cpp` handles them immediately after the screen
   lock and before any page branch, so they work on every page and on the Launcher and no page can swallow
   them. One press = `soundcfg::stepVolume(current, ±10)`, clamped 0..100 — the Settings › General › Volume
   row's own function and the same `soundstore` value it shows. `StepGate` refuses repeats faster than
   ~6/s. **The BSP currently sends one edge per press** (its key-repeat thread is commented out), so a held key
   gives one step; no repeat was invented. Reported as a fact, not hidden.
2. **The pop-up.** `core/volume_popup.h` owns the text and the 1.5 s clock; `main/chrome.cpp::drawHintBar`
   draws it, so every page and the Launcher get it. Text: `Volume 70 %` (with a bar), `Muted` at 0, `Sound off`
   while the master switch is Off; the bar still shows the level while Sound is off. Every press re-arms the
   1.5 s and redraws, even at a clamp. The plate is 420×54 at x=190, y=390 on the 800×480 glass; the message
   pop-up is centred with its lowest edge at ~315, so **both fit and neither hides the other** (no need to
   suppress one). The plate is NOT modal: it takes no key; the message pop-up keeps its own existing rule
   (any key dismisses it and is swallowed).
3. **The tick.** `audioout::volumeTick(new_level)` applies the L2's codec curve first, then enqueues ONE fixed
   **Soft Tick** (the L2's Sent default) through the existing audio task/queue. Nothing is enqueued at 0 %
   (Muted) or while Sound is Off; the level still changes. Its own 100 ms floor is separate from the
   notification events' 400 ms gate.
4. **NVS.** `sound_store` now exposes `setVolumeLive(level, now)` (RAM only) and `volumeTick(now)` (one write
   when due). `core/volume_popup.h::VolumeSave` measures **2 s from the LAST press** (a clamp press calls
   `volumeTouch`, which moves the window without scheduling a redundant write); the main loop calls
   `volumeTick` once per pass. Immediate Settings writes (+/-, Enter) still write at once and clear the pending
   flag, because their blob already carries the live value.
5. **Locked.** The existing `ScreenPower` key-lock runs before `mapKey`/the volume handler, so a locked screen
   ignores the volume keys; only the power button acts.

**Part 2 — the function keys one up.**
- **The map** is `core/keys.h::page_for_symbol`: X/F1 (20) → Launcher, triangle (21) → Nodes, square (22) →
  Chat, circle (23) → Map, cloud (24) → RPTR, diamond (25) → Settings. `is_symbol` now includes `SymX`, so the
  router treats X as a page key; `is_own_symbol` follows the map (X on the Launcher, diamond on Settings).
- **Settings' own key.** The shell's Settings branch maps the diamond to `SetKey::Settings` (the exact code the
  comma fed until now) in every sub-page/list/root handler; text fields close to the root; the module's
  `onTop(Settings)` does the same for the core route through a pointer to the frame. At the root it changes
  nothing. `,` no longer opens Settings anywhere: the old `if (!handled)` comma branch is gone. In text fields
  it is ordinary text wherever that field's own rules allow it (chat composer and Nodes search: any byte;
  RPTR password and RPTR settings/position: 0x20..0x7E incl. comma; channel Tag: yes, then normalised;
  KeyB64: no (base64 alphabet); ScopeRegion and the node name: no (their own character rules)).
- **The hint bar** is six sigils, X first, in Page order, labels LAUNCHER/NODES/CHAT/MAP/RPTR/SETTINGS, with
  tighter 18/14 gaps. The two red dots stay bits 1/2, which are now the triangle (Nodes) and the square
  (Chat) — their NEW keys. The `, settings` chip was removed from the Launcher, Settings (root/sub/volume),
  Chat (list/open) and Map; Nodes and RPTR never had one. Page-key symbols are drawn as vectors, including
  the new X.
- **Docs.** `docs/PLAN.md` § 5, § 2 rule 3 and the open question now carry the new map/codes and the volume
  keys; `BRIEF.md` § 5 has a dated `made by DeepSeek` note (Filip's text left intact). `DEVICE.md` lists no
  key map (grep: no change needed). All comments in `core/keys.h`, `core/router.h`, `core/settings.h`,
  `main/chrome.*`, `main/settings_page.cpp` and the shell were updated.

### Host tests
`test/host/test_volume_popup.cpp` (new, **67 checks**): the 10 % step at 0/10/90/95/100 and both clamps
(no wrap), the Settings-row equivalence, `Volume 0/50/100 %` / `Muted` / `Sound off` (Sound off wins), the bar
percent and the 420×54 plate at (190,390) with its lowest edge below the message pop-up's 315, **10 presses
coalesced into ONE write exactly 2000 ms after the last** (and not before), a clamp press moving the window
without a redundant write, the 1.5 s plate across the 32-bit wrap and a press re-arming it, the ~6/s gate, and
`ScreenPower` swallowing keys while locked (only the power button wakes).
`test_core.cpp` (updated, **312 checks**): the six-key table/codes/names (X 20, 21..25, volume 37/38), the full
map, the own-symbol rule for every page **including Settings via the diamond**, a typed comma not being a
Settings key, and the existing route/own-top tests moved onto the new keys.

### Arm evidence
The step/clamp rule (clamp removed, `return (uint8_t)(current + delta)`):
```
FAIL test_volume_popup.cpp:61  tm::volume::step(100, tm::volume::kStep) == 100 (got 110, want 100)
volume-popup: 1/59 checks FAILED
```
The coalescing rule (`VolumeSave::due` made true while pending, i.e. a write per press):
```
FAIL test_volume_popup.cpp:115  !s.due(2001)
FAIL test_volume_popup.cpp:132  !s.due(3999)
volume-popup: 2/56 checks FAILED
```
Both were restored; no `ARM` marker remains (grep clean) and the final suite is 0 failures.

### Files
Core: `core/keys.h` (six-key map, X/volume keys, `is_settings_key`), `core/volume_popup.h` (**new**: step,
text, plate geometry, 1.5 s clock, 2 s coalescing, 6/s gate), `core/settings.h` (v73 + comments),
`core/router.h` (comments). Main: `main/main.cpp` (volume handler, diamond/Settings branch and `onTop`, comma
removal, loop ticks), `main/chrome.h/.cpp` (six sigils X-first, volume plate, dots comment),
`main/sound_store.h/.cpp` (live volume + coalesced write), `main/audio_out.h/.cpp` (volume tick),
`main/launcher_page.cpp`, `main/settings_page.cpp`, `main/chat_page.cpp`, `main/map_page.cpp` (hint chips).
Tests: `test/host/test_volume_popup.cpp` (**new**), `test/host/test_core.cpp`, `test/host/run.sh`.
Tool/docs: `tools/preview_tanmatsu.py` (six sigils, volume plate, two renderers), `docs/PLAN.md`,
`docs/PROGRESS.md`, `BRIEF.md`. Shots: `shots/2026-11-20-volume-popup.png`,
`shots/2026-11-20-hintbar-keys.png` (both 800×480 host renders; older shots were not regenerated).

### Could NOT be checked
Not on the glass: host renders only, nothing installed or flashed. **VOLUME_DOWN (38) has never appeared in a
log**, and **X/F1 (20) was seen once but not yet tied to a labelled press by Filip** — both are the BSP enum's
values, not guesses, but neither is a confirmed physical press yet. The tick's exact loudness/feel at the new
level, the plate's look on the MIPI panel, and the keys' real repeat (the BSP sends none; no repeat could be
observed) need Filip's eyes/ears. `DEVICE.md` was checked and lists no key map; `PROGRESS.md`'s older entries
are history and still describe the `,` of their day (not rewritten).

### Next step
Claude reviews this diff; if accepted, Filip installs v73 and checks: volume up/down steps the Settings Volume
row by 10 and the plate appears on any page; a tick at the new level (none at Muted/Sound off); one save after
~2 s (no per-press NVS churn); the keys do nothing while locked; X opens the Launcher, diamond opens Settings
and returns to its root from a sub-page; `,` does nothing outside a field and types inside it; the hint bar
shows six keys with the dots on triangle/square. Then the usual commit/push decision.


**Claude's review of EST-478 (S516, 2026-11-20 22:0x):** key map (`core/keys.h` page_for_symbol: X Launcher … diamond Settings), `is_own_symbol` follows it; the volume keys are handled before any page and after the key-lock; `,` is no longer turned into SetKey::Settings at any of the four sites; previews checked (six sigils fit with the dots on Nodes/Chat; the volume plate sits above the hint bar). Accepted. DeepSeek's finding stands: the BSP's key-repeat thread is commented out, so a held volume key gives one step. On the glass still to pin: X = 20 and VOLUME_DOWN = 38.
## 2026-11-20 · S516 (EST-477) LEDs: system defaults, off switches, message colour

> Written by **DeepSeek** for ticket **EST-477**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point **v71 `15829ab`**,
> tree clean, v71 INSTALLED and running on the device. **RESULT: version 72; nothing installed, flashed, pushed or
> committed.**
>
> **Numbers:** `sh test/host/run.sh` **exit 0**; the rewritten `message-led` suite is **266 checks, 0 failures**.
> `make build` **exit 0**, `application.bin` **1,748,304 B (0x1AAD50)**, **+2,528 B** over v71's 1,745,776 B
> (total image **1,747,892 B**); DIRAM **303,228 B used / 269,140 B free (52.98 %)**. Two host tests armed, exact
> FAIL lines below. `.git` untouched, no commit; no SSH needed (the device is on Filip's USB).

### What was read
`core/message_led_rule.h`, `main/message_led.*`, `main/main.cpp` (the General LED rows and the 250 ms publish
site), `core/settings_model.h` + `main/settings_page.cpp` (the existing hint/dim hook), `core/settings.h`; our BSP
`managed_components/nicolaielectronics__tanmatsu_coprocessor/tanmatsu_coprocessor.{h,c}` (the wrappers and the
I2C concurrency semaphore), `badge-bsp/targets/tanmatsu/badge_bsp_led.c`, `bsp/led.h`, `bsp/power.h`,
`bsp/tanmatsu.h`; `test/host/run.sh`, the old `test_message_led.cpp` and `tools/preview_tanmatsu.py`. The
coprocessor firmware facts (`main.c:709-800`, `leds.c:37-44`) are cited from Claude's read in the ticket: the
clone at `/home/slofi/dev/tanmatsu-coprocessor-firmware` is **not visible from the sandbox**, so it was not
re-read here.

### The implementation, rule by rule
1. **Settings.** General › LED now has three rows: **Power LED** (Toggle, default **On**, `omled/pwled`),
   **Radio LED** (Toggle, default **On**, `omled/rdled`, dim hint **"Wi-Fi + LoRa chip"**), **Message LED colour**
   (Cycle: Red / Green / Blue / Yellow / Cyan / Magenta / White, default **Cyan** — v63..v71's soft cyan, so there
   is no "Blue if none" fallback to use — `omled/msgcol`). The old `msgled` (Off/1..6) is read, logged, erased
   and committed in `begin()`; its index is deliberately not carried over, because the message LED is now always
   the system's LED 3 (index 2).
2. **Unlocked, both switches On: AUTOMATIC, always.** Unread > 0 writes register 146 as colour A = the chosen
   colour (`(r<<0)|(g<<1)|(b<<2)`), colour B = off, `fade` = 1 (bit 3), `fade hold` = 0; unread == 0 writes all
   false. No pixel is written: `LedFrame.rgb` is all zero and the shell only applies the register change.
3. **Unlocked, one or both Off (or no register 146): MANUAL while OM Lite runs.** The app writes all six pixels:
   index 0 = the power emulation (**no battery magenta 0xFF00FF / charging yellow 0xFFFF00 / battery green
   0x00FF00**, "no battery" checked before charging, `main.c:715-732`); index 1 = **green while our C6 link is up**
   (`om::radio::started()`), dark otherwise; index 2 = the message LED in the chosen colour, breathing on a
   plain **8-step 0..255 ramp, one step every 200 ms = at most 5 I2C frames/s**, and only on a step change; index
   3-5 dark. A 25 ms poll with no frame change writes nothing, so the rate is 0 at rest.
4. **Locked (screen off).** Unchanged from S515: MANUAL, every index dark, index 2 gives **three 120 ms/180 ms
   blinks every 20 s** while unread > 0, in the chosen colour at 20 % (v71 used a 15 % global brightness over
   full cyan; this keeps the same gentle look for every colour). The switches cannot light anything while locked.
5. **Every exit path.** `restoreAutomatic()` writes the dark pixels, writes register 146 = 0 (all false), then
   `bsp_led_set_mode(true)`, before the launcher handover — no stale message fade is left behind.
6. **Older coprocessor firmware.** `begin()` probes `set_message(all false)` once; if it fails it logs the
   coprocessor firmware version (`tanmatsu_coprocessor_get_firmware_version`, one line) and falls back to rule 3's
   app-driven message LED. A runtime refusal flips the same flag and logs once.

The six pixels go through `bsp_led_*`; register 146 through `tanmatsu_coprocessor_set_message`
(`tanmatsu_coprocessor.h:262-270`). I2C is serialised by the coprocessor driver itself:
`tanmatsu_coprocessor.c:163-197` takes `configuration.concurrency_semaphore` around every transfer; the glue's
own mutex only keeps `restoreAutomatic()` and the task from writing at the same time.

### Host tests (rewritten `test/host/test_message_led.cpp`)
Pins: the seven colours' exact register bytes (Red 0x09, Green 0x0A, Blue 0x0C, Yellow 0x0B, Cyan 0x0E,
Magenta 0x0D, White 0x0F — all with colour B and fade-hold clear) and "no change, no I2C"; the MODE for all
2×2×2×2 locked/power/radio/unread combinations plus the no-register fallback; the three emulated power colours
and the radio on/off rule; the locked blink at 0/119/120/300/420/600/720/900/19999/20000/20300 ms with indices
0-1 and 3-5 dark; the 8-step manual fade with EXACTLY 16 changed frames over 3.2 s (5/s) and one mode-change
frame; the 32-bit wrap; and `restore()` returning automatic + `message = 0` + dark pixels.

### Arm evidence (deliberately wrong code, then reverted)
```
FAIL test_message_led.cpp:168  f.automatic ? 1 : 0 == want_auto ? 1 : 0 (got 1, want 0)
FAIL test_message_led.cpp:196  !f.automatic
FAIL test_message_led.cpp:255  !first.automatic
message-led: 3/195 checks FAILED
```
(the first removed `in.power_on` from the automatic-condition; the same wrong mode then broke the fallback and
fade tests).
```
FAIL test_message_led.cpp:129  f.message == row.reg (got 129, want 9)
message-led: 1/170 checks FAILED
```
(the second set bit 7, fade hold, instead of bit 3, fade, in `messageRegister`). Both are restored; no `ARM`
marker remains.

### Files
Core: `core/message_led_rule.h` (rewritten: colours, register 146, power emulation, modes, manual pixels,
restore), `core/settings.h` (**v72**).
Main: `main/message_led.h/.cpp` (three NVS settings + `msgled` migration, register 146 probe/fallback, manual
frame writes, extended `publish`), `main/main.cpp` (three General › LED rows, handlers, hint, publish call).
Tests: `test/host/test_message_led.cpp`.
Docs/tool: `docs/PROGRESS.md`, `tools/preview_tanmatsu.py` (`settings_message_led` -> `settings_led`).
Shots: `shots/2026-11-20-settings-led.png` (800×480; the old `2026-11-19-settings-message-led.png` is history
and was not regenerated).

### Could NOT be checked
Not on the glass: host renders only (desktop DejaVu), no install, flash or serial. The exact panel width of the
"Wi-Fi + LoRa chip" hint is checked in the preview at 800 px, not with the device's generated faces. The
coprocessor firmware clone is not visible from the sandbox, so the register/firmware facts are as Claude read
them. The device's real fade look and the C6 LED during a real LoRa session need Filip's eyes.

### Next step
Claude reviews this diff; if accepted, Filip installs v72 and checks: General › LED has three rows; both On =
the system's power/radio LEDs as before and the message LED breathes in Cyan; either Off = manual (that LED dark,
the other still right, no flicker between fade steps); locked = three blinks every 20 s; Esc to the launcher =
its own LEDs back and no message fade left behind.


**Claude's review of EST-477 (S516, 2026-11-20 21:4x):** read the rule (automatic only when unlocked AND both switches On AND register 146 works; manual otherwise) and the apply/exit code: leaving manual writes the app pixels dark, then the message register, then automatic mode; `restoreAutomatic()` does dark → message 0 → automatic under the output mutex before the handover; the coprocessor I2C is serialised by the BSP. The radio emulation uses our radio's started state, which matches the coprocessor's green (C6 application) while OM Lite runs. Accepted. Suite exit 0, build exit 0 (1,748,304 B). Not on the device yet.
## S516 (Claude) v71: the Chat page crashed the device (main task stack overflow), fixed

**Found by Filip on the glass, 2026-11-20 20:5x (v69):** opening Chat (circle, key 23) showed a light-blue screen and the device was stuck; the console showed `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)` in task `main`, twice (20:5x and after a restart at 20:55). Decoded against a v69 rebuild in a separate worktree (same size; differs only in the build timestamp): MEPC/RA in `drawTextClipped` / `drawChatCard` / `clipInto` / `Font::textWidth`, i.e. the leaves of the chat list draw. **Cause:** `app_main`'s `drawChatPage` lambda had a **5,632 B** stack frame because it built `om::ui::ChatView cv` (**4,836 B**: the v64 chat menu added `menu[5]` + `info[14]`) on the stack; with `app_main`'s own 2,192 B that is 7,824 of the **8,192 B** main task stack. Nobody opened Chat on the device between v64 and tonight (v65 and v68 logs: 0 presses of key 23), and the host suite cannot see a device stack.
**Fix:** the view is a static, rebuilt in place every frame (`new (&cv_storage) ChatView()`, `static_assert` trivially destructible); the main task stack is **16,384 B** (`sdkconfigs/tanmatsu`, plus the generated `sdkconfig_tanmatsu`, which is untracked and does not pick up a new default by itself). After: the lambda is no longer among the large frames (largest left on the path: `app_main` 2,192, `chatInfoRows` 1,936). A boot-time line now logs the main stack's low-water mark after the first Chat draw. Suite exit 0, build exit 0 (1,745,776 B).
**Lesson (Claude):** a new page-sized view struct on the main task needs a device check of the stack, not only host tests; the largest-frames listing (objdump `addi sp,sp,-N` per function) is a cheap gate.

## 2026-11-20 · S516 (EST-476) Save heard nodes to SD

> Written by **DeepSeek** for ticket **EST-476**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point **v69 `e619fde`**,
> tree clean, v69 INSTALLED on the device since 20:43. **RESULT: version 70; nothing installed, flashed, pushed or
> committed.**
>
> The ask (Filip, 20:4x): a Settings › OM Lite › NODES toggle **"Save heard nodes to SD"**, On by default, so he
> can choose that NEW heard nodes live only for this session. Off must still let a node be used (Nodes, DM, Ping,
> Trace, Map) but never write it to `nodes.bin`, `radio.bin` or the NVS fallback, and it must also suspend
> "Add heard nodes to the radio". The user's own Add to radio / Favourite still save the node; nodes already saved
> keep updating; switching back On saves only nodes heard from then on.
>
> **Numbers:** `sh test/host/run.sh` **exit 0**. The only suites whose check count moved: `node_table` **6,832**,
> `node_store` **256** in BOTH `-O2` and ASan (was 228 → **+28 each, +56**), `node_view` **767** (**+17**); every
> other suite unchanged, no sanitizer noise beyond the vendored Ed25519 warning already recorded. `make build`
> **exit 0**, `application.bin` **1,745,552 B (0x1AA290)**, **+3,424 B** over v69's 1,742,128 B (total image
> **1,745,146 B**, +3,422); DIRAM **298,380 B used / 273,988 B free (52.13 %)**, +1,176 B over v69. Two host
> tests armed, exact FAIL lines below. `.git` untouched, no commit; no SSH needed (the device is on Filip's USB).

### What was read
`core/mc_store.*`, `contact_blob.*`, `node_store.*`, `node_table.*`, `node_view.h`, `contact_admin.h`,
`settings_model.h`, `settings.h`; `main/mc_link.*` (the `heardto` load/set/read path, the advert call site,
`nodeStoreCounts`, `contactsSetFavourite`), `main/main.cpp` (the Settings row table and the Nodes view build),
`main/nodes_page.*`, `main/settings_page.cpp`; the three host tests and `test/host/run.sh`; the one preview tool.

### The implementation, rule by rule
1. **Setting.** `Save heard nodes to SD` is a `Toggle` in NODES, **DEFAULT On**, NVS `mcset/saveheard` (only an
   explicit 0 is Off; a missing/unreadable entry keeps On). `loadSaveHeardToSd()` sits beside `loadHeardToRadio()`
   in `begin()`; `saveHeardToSdSetting()` / `setSaveHeardToSdSetting()` follow the same failed-write rule.
2. **Off = session-only.** `Contact::session_only` is a new RAM field and is deliberately **not** in any file
   format (`contact_blob.h` unchanged). `advertInArchive(..., radio_cap, save_heard)` marks a NEW row session-only
   when `save_heard == false` and returns before the radio rule, so even `HeardToRadio::All` marks nothing. The row
   is still inserted, usable (Nodes, DM, Ping, Trace, Map), `fresh` is set as before, and its Nodes second line
   says `· not saved` (dim); the 800 px preview shows it fits inside the 320 px list.
3. **Off suspends Add heard to radio.** The `heardto` row stays visible and keeps its value; the frame's row model
   gained an optional dim hint (`read_hint`/`read_dim`), and the row draws dim with `needs Save heard nodes On`.
   It can still be changed and stored — it simply has no effect while saving is Off.
4. **User acts save.** `addToRadio()` clears `session_only` on `Ok` (a refused `RadioFull` leaves it session-only),
   and `setFavourite()` / `contactSetFavourite()` clear it without implying `in_radio` (Favourite stays
   archive-only, as EST-455 pinned). `Delete everywhere` already removes the row; no special case.
5. **Saved rows stay saved.** A REFRESH never touches `session_only` or `in_radio`; a repeat advert from a
   session-only node keeps it session-only, and a saved row keeps updating its last-heard/position while Off.
6. **Back On.** No bulk rewrite: nothing ever clears session-only rows, so rows already marked stay marked until
   the user acts as in rule 4; nodes heard after the switch are saved normally.
7. **Counts.** `radioCount()` counts saved in-radio rows only; `sessionOnlyCount()` is new. `nodeStoreCounts()`
   returns `sd = count - sessionOnly`, and `nodeStoreCountsText()`/`nodeStoreInfoText()` take an optional
   `unsaved` argument: `Radio 2 / 300 · SD 17 · +3 unsaved` (also with `SD -` when there is no card). The Nodes
   head and the Settings info row both pass it.
8. **Fresh dot.** Unchanged: `fresh` is RAM state, drawn for session-only rows too, and never encoded.
9. **No SD card.** Unchanged: heard nodes were already this-boot-only; the setting is still shown and stored.

### Encoding skip (the "gone after a restart" promise)
`node_store.cpp::selected()` now requires `!session_only` for BOTH `nodeFileEncode()` modes; `contactsEncodeRadio()`
(the NVS fallback) skips it; `contactsEncode()` (the full v4 blob) skips it too, so there is no path that can write
a session-only row. The test also encodes the contrived case `session_only && in_radio` to prove the radio file
skips it even then.

### Host tests added
- `test_node_table.cpp::testSessionOnlyOnAdvertWithSaveOff` — Off + All: session-only, never `in_radio`, fresh;
  repeat keeps both flags; Add to radio and Favourite clear; a previously saved row stays saved while Off; Delete
  everywhere removes a session-only row.
- `test_node_store.cpp::testSessionOnlyRowsAreNeverEncoded` — radio.bin count 1 (not the contrived 2), nodes.bin
  count 2 of 3, NVS fallback count 1, full v4 blob count 2; every decode finds the session-only key absent.
- `test_node_view.cpp` — the exact counts texts (`+3 unsaved`, `SD - · +2 unsaved`, unchanged wording at 0), the
  On/Off value, the toggle and the hint string.

### Arm evidence (deliberately wrong code, then reverted)
```
FAIL test_node_store.cpp:395  buf[8] | (buf[9] << 8) == 1 (got 2, want 1)
235 checks, 1 failure(s)
```
```
FAIL test_node_table.cpp:334  s.c[0].session_only
6799 checks, 1 failure(s)
```
The first removed `!c.session_only` from `selected()`; the second disabled the `if (is_new && !save_heard)` branch.
Both are restored in the tree; no `ARM` marker remains.

### Files
Core: `core/mc_store.h` (+`session_only`), `core/mc_store.cpp` (reset on insert), `core/node_table.h/.cpp` (rule,
counts, Add/Favourite clear), `core/contact_blob.h/.cpp` (full-blob skip), `core/contact_admin.h` (Favourite clear),
`core/node_store.h/.cpp` (`selected()` skip), `core/node_view.h` (counts + setting helpers), `core/settings_model.h`
(hint/dim hook), `core/settings.h` (v70).
Main: `main/mc_link.cpp/.h` (NVS setting, advert argument, counts), `main/main.cpp` (row + callbacks, view counts),
`main/nodes_page.cpp/.h` (`+N unsaved`, `· not saved`, `unsaved_count`), `main/settings_page.cpp` (dim/hint draw).
Tests: `test/host/test_node_table.cpp`, `test_node_store.cpp`, `test_node_view.cpp`.
Docs/tool: `docs/PROGRESS.md`, `tools/preview_tanmatsu.py`.
Shots: `shots/2026-11-20-nodes-settings-save.png`, `shots/2026-11-20-nodes-head-unsaved.png` (800×480; also
regenerated `2026-11-19-settings-nodes.png` so the NODES section preview is not stale).

### Could NOT be checked
Not on glass (host renders only, desktop DejaVu); no install, flash, serial or server read was needed. The exact
panel width of the hint and the row tag is checked in the preview at 800 px, not with the device's generated faces.

### Next step
Claude reviews this diff; if it is accepted, Filip installs v70 and checks the two NODES rows (Off state: the
Add-heard row dim with its reason) and a heard node's `not saved` tag and `+N unsaved` head.


**Claude's review of EST-476 (S516, 2026-11-20 21:0x):** the file format is unchanged (no version or record-size change; session-only rows are only skipped by the three encoders), the rules match the ticket (Off + HeardToRadio::All still never marks in_radio; Add to radio / Favourite clear the flag; a repeat advert keeps it). Accepted. Suite exit 0. Not on the device yet.
## 2026-11-20 · S516 (EST-475) sweep round 3: tonight's fixes against all their callers, and the archive write at full size

> Written by **DeepSeek** for ticket **EST-475**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed (`.git` read only; Claude reviews and commits). Starting point **v68 `f59609e`**,
> tree clean, INSTALLED on the device since 20:09. This entry is written FIRST and filled in as the work goes.
> **Version 69** (`core/settings.h`) because the code changed. Round 3 covers (A) every fix of EST-472/473/474 and
> Claude's review fixes at ALL their callers, and (B) the SD archive write at 2,000 rows.
>
> **RESULT:** version **69**. `sh test/host/run.sh` **exit 0**, suites summed **34,238** (this ticket's own sum;
> the only suite whose count changed is `node_store` **205 → 228**, +23 in BOTH the `-O2` and the ASan stage = **+46**,
> exactly the difference from the EST-474 entry's 34,192; the only sanitizer noise is still the vendored Ed25519's
> pre-existing `left shift of negative value`). `make build` **exit 0**, `application.bin` **1,742,128 B
> (0x1A9530)**, **+560 B** over v68's 1,741,568 B (total image 1,741,724 B); DIRAM **297,204 B used / 275,164 B
> free (51.93 %)**, +8 B over v68 (one `bool` + padding). **Five defects found and fixed** (A1, A2, A3, A5, A6 — A1/A2/A3 are regressions from tonight's fixes),
> **one fix by design in B** (the archive's structural/soft floor), two items reported and not fixed (A4, A7),
> one checked clean (A8). Two new host tests, both armed; the four device-glue fixes say so. Nothing installed or
> seen on glass; `.git` untouched, no commit; **no SSH** was needed (the device is on Filip's USB, not on a server —
> its timing is not visible from this sandbox).

### The eight-item checklist (one line each, severity where something was wrong)

1. **defect (fixed, no host test — file syscalls in `main/`):** a failed `rename` removed the temp after the OLD
   file had already been removed, leaving NEITHER name → the next boot fell back to the 32-row NVS store. Severity:
   **lose data** (rare: the rename must fail). Every caller's reaction to `false` was read: radio (10 s retry +
   dirty re-raised), archive (30 s retry + dirty), boot migration (one warning, retried at exit), shutdown (one log;
   the change is lost) — **no path shows the failure to the user**: the counts are RAM counts. Pre-existing, reported.
2. **defect (fixed, test armed):** yes — `nodeFileCheck` answers `BadVersion`/`BadRecordSize`/`BadCount`/`BadSize`/
   `BadMagic` for a structurally sound file this build merely cannot read, and the EST-472 fix deleted the temp for
   every non-`Ok`. After an app-version change plus a power cut in the write gap, the other version deleted the only
   copy. Now only `Truncated`/`BadCrc` are removed; the rest are KEPT and logged. Severity: **lose data** (rare).
3. **defect (fixed, no host test — boot state):** YES, on the no-`/int` fallback the NVS blob was rewritten on
   **every exit** even with nothing changed, because `g_radio_have_signature` was never seeded at boot (the forced
   `structural` pass in `nodeStorageShutdown` always found "no signature yet"). The boot now encodes the same bytes
   and seeds the signature; a failed encode leaves it false, so the exit still retries. Severity: **wear** (16 KB
   NVS partition shared with the launcher; no user-visible effect).
4. **checked, nothing material:** the wake discard only drains the pop-up slot and dismisses the live pop-up; it
   never touches unread counters, the seen point or the Chat dot (those are set by `mc_link` when the message is
   pushed). One **microsecond-wide race** remains: a message offered after the power-press event flipped
   `screen_off` to false but before the very next `msgPopupPoll(..., true, ...)` is discarded. Nothing is lost
   (thread, sound, unread dot and LED stay) and closing it needs an arrival timestamp in the pending slot, so it is
   reported, not fixed. Severity: **cosmetic**.
5. **defect (fixed, no host test — the rule is `main`'s):** the focus, the menu index and the dots after the remap
   are right (the box lands on the row that slid up, `node_menu_index = -1`, dots come from the remapped store
   rows), but the **ack dwell** (`nodes_ack`) holds a SHOWN row and its 1.5 s timer is not restarted by the
   delete: the node that slid into the deleted row could be acked (red dot cleared) on the very next pass without
   the box ever resting on it. Now `nodesAckEnter()` re-seats the dwell on the new row (no ack). Severity: **mislead**.
6. **defect (fixed, no host test — `main/` state machine):** status requests are independent (a RESPONSE, its own
   `s_statusPending`; checked, nothing), the read ladder is held by `s_cli.used` (checked), logout is safe
   (`s_cli.clear()` + `s_haveTarget=false`, so a late answer is refused and the v66 second door drops it —
   checked) — but **`settingsReadStart()` ('r') cleared the pending line**, which re-opened the exact EST-473 §6a
   misattribution the v68 window was built to close: a late `get tx` answer could land on the newly sent `get name`.
   'r' is what a user presses while the page says "waiting…". Fixed: only the LADDER restarts; the slot is left to
   the answer/late-window/timeout. Cost: the re-read starts up to 8 s (16 s in a late window) later, visibly
   "waiting". Severity: **mislead**.
7. **report only (not a crash, no data loss):** a `kTxtTypeSignedPlain` (2) DM is no longer dropped as repeater
   traffic (the corrected constant), but it is **decoded with the wrong text offset**: MeshCore's signed layout is
   `ts(4) | flags | sender_prefix(4) | text` (`BaseChatMesh.cpp:290-297`, text from `data[9]`), while `dmOpen` starts
   the text at `plain[5]`. A room server's signed message therefore shows up in Chat as up to 4 binary bytes (often
   empty, cut at the first NUL) instead of the text, and no ACK is sent (the ACK branch is `PLAIN`-only; signed wants
   a 4-byte ack). Signed **channel** text is refused whole by `parsePlain` (`mc_wire.cpp:151`) and only logged.
   Severity: **mislead** (a person's message shown as junk). Not fixed — the ticket says report only.
8. **checked, nothing:** `DmSummary` is only ever DEFAULT-constructed in arrays (`main.cpp:2495`, `:3287`,
   `test_store.cpp:267/287/348`), assigned via `out[n++] = dmSummaryForPeer(...)` (copy assignment re-points
   `name`), copied in the test, never `memset`/`memcpy`/sorted/aggregate-initialised (the user-declared copy ctor
   makes it a non-aggregate, so any brace-init would not compile). `name` may point into the DM RING
   (`m->peer_name`) — the summaries are filled and drawn/copied inside one `StoreLock`, so no ring wrap can move
   them mid-use (same rule as `ChannelSummary`).

### A. WHAT WAS READ, AND THE FIXES IN DETAIL

**1. `writeFileAtomic` (`main/mc_link.cpp:618-659`) and every caller.** The write/flush/fsync/fclose checks of
EST-472 are right: a failure removes the temp and keeps the old file, and the callers retry (radio: 10 s +
`g_contacts_write_failed`, dirty re-raised under the lock; archive: 30 s, `g_archive_dirty = true`) or report once
(boot migration: one warning, retried on the forced exit write because `have_signature` stays false). `stat` +
`remove` of the old file is the gap the temp covers. **The regression:** after `remove(path)` succeeds, a failed
`rename` did `remove(tmp)` too — both names gone; midnight's own comment ("a power cut in that gap leaves only the
temp file") was no longer true for the non-power failure path. **Fix:** keep the finished temp (`:649-657`); the
next boot's `readNodeFileOrTemp` CRC-checks it, renames it into place and loads it. **The user-visible counts:** no
path tells the user a write failed — the Nodes head, "Stored nodes" and the radio counter are RAM counts, so a
card/flash failure is log-only. That is pre-existing and was left alone. Severity: **lose data**, rare.

**2. `readNodeFileOrTemp` (`:686-707`) and the version question.** The check-before-trust is right, but "not Ok"
was treated as "damaged". `nodeFileCheck` returns `BadVersion`, `BadRecordSize`, `BadCount`, `BadSize` and
`BadMagic` for files whose bytes are coherent and whose CRC may be perfectly good — a file from another app
version. Deleting those destroys the one copy that the other version can read. **Fix:** `nodeFileTempIsTorn()`
(`core/node_store.h:72-79`) is true only for `Truncated` and `BadCrc`; everything else is logged as "from another
format; KEPT" and the store falls back for this boot. **What the other version's file does now, honestly:** a
newer/older **real** file is still `nodeFileDecode`-refused whole (unchanged); the radio boot then migrates from
NVS and rewrites `radio.bin` at once, and the archive is rewritten by the first dirty write, so a downgrade still
REPLACES the newer file in the end. Not deleting it at boot is the minimum the regression needs; a "saw a newer
version, do not touch this store this session" rule would be a new feature, and the ticket did not ask for one.

**3. `nodeStorageShutdown` on the NVS fallback (`:1020-1038`, boot seed at `:797-817`).** Yes, it wrote the blob
on every exit: `g_radio_have_signature` is only set by a successful write (`:939-940`) or by an `/int`
load/migration (`:769-770`, `:789-790`), and the no-`/int` boot path set neither, so the forced `structural` pass
always found "no signature yet". **Fix:** the boot encodes the NVS blob with the writer's own call/args and seeds
`g_radio_signature`/`g_radio_have_signature`, but ONLY when nothing is pending: `restoreContacts()` raises the
structural flag when it dropped a record, and seeding then would make the writer's signature check skip the write
that must remove it. A failed encode leaves the flag false, so the exit still tries. NVS
bytes: 32 rows × 159 B + 6 B header ≈ 5.1 KB per rewrite; the partition is 16 KB and shared with the launcher, so
one unnecessary blob per app exit is real wear. Severity: **wear** (no user-visible symptom).

**4. The wake-path discard (`main.cpp:3036-3058`, `chrome.cpp:172-215`, `core/msg_popup.h:66-80`).** The discard
calls `msgPopupPoll(now, true, ...)`, which drains `g_pending_popup` into `offer(..., screen_off=true)`: that only
dismisses the live pop-up and clears the count. Unread/dot/seen are set where the message is pushed
(`mc_link.cpp`), not here, so they cannot be dropped. A message that arrives AFTER the wake is safe **except** in
the window between the power-press `onEvent` (which clears `off`) and the very next statement, the discard poll —
a few microseconds. It needs an arrival timestamp in the pending slot (and a cutoff) to close, and nothing but the
pop-up is lost; reported, not fixed. Severity: **cosmetic**.

**5. `nodesFrozenAfterRemove` (`main.cpp:4030-4064`, `core/nodes_fresh.h:77-103`).** The remap itself is right
and `refocusAfterDelete` (`:2190-2200`) puts the box on the row that slid up (`contactsFind` after the delete is
-1 → keep `old_focus`, clamped), the menu is closed and `node_menu_index = -1`, and the dots come from the
remapped store rows. **The regression:** the ack dwell is a SHOWN-position timer (`nodes_ack`) and the delete did
not restart it, so a rest that began on the deleted node could be past 1.5 s and ack the NEXT node on the same
pass. Fix: `nodesAckEnter(nodes_ack, nodes_focus, now)` after the refocus. Severity: **mislead** (a red dot gone
without a look).

**6. The v68 late window (`main/rptr_link.cpp`).** Status: `statusRequest()` is independent (its reply is a
RESPONSE parsed by `handleReply`; nothing shares the CLI slot) — checked. The login ladder and the read ladder are
both held by `!s_cli.used` (checked, `:285`). Logout: `logout()` clears the slot AND `s_haveTarget`, so a late
answer is refused at `onCliAnswer`'s first test and the v66 second door drops the CLI-typed text — checked. **'r':
the defect.** `settingsReadStart()` (`:585`) cleared `s_cli` and restarted the ladder, so the exact old
misattribution came back (a `get tx` answer on the new `get name`). Fix: only `s_readStep/s_readStop` are reset;
the slot stays until the answer / late-window end / timeout. Cost: the full re-read can start up to 8 s (16 s in a
late window) later — the page already says "waiting for the repeater…". Side effect, documented: if the pending
line was a `set`, its own one-row re-read still narrows the full re-read to that row — the user can press 'r'
again. Severity: **mislead**.

**7. `kTxtTypeSignedPlain` (report only).** See the checklist: not dropped (good), but the text offset is wrong
(`plain[5]` vs `plain[9]`), so a signed room message appears as up to four binary bytes (usually empty, cut at the
first NUL) and gets no ACK; signed channel text is refused whole by `parsePlain` and only logged. No crash, no
existing data lost. Severity: **mislead**, reported.

**8. `DmSummary`.** See the checklist: no `memset`/`memcpy`/sort/aggregate init anywhere; every copy goes through
the re-pointing ctor/assignment; the ring pointer lives only inside one `StoreLock`. Checked, nothing.

### B. THE ARCHIVE WRITE AT FULL SIZE (2,000 rows)

**(1) The cost.** The file is `12 + rows × 159 + 4` bytes — **159 B per record**, not `sizeof(Contact)` = 164; the
ticket's 328,000 B is the struct, the file is **318,016 B** (500 rows: **79,516 B**). Measured on the host with
`-O2` (`sweep/est475_bench.cpp`, same `nodeFileEncode` + CRC the device runs):

| rows | file | encode incl. CRC (host) | on the P4 (est. 5-10×) | fixed FatFs cost (device, 17-row log) | 318 KB at 1-5 MB/s | total estimate |
|---|---|---|---|---|---|---|
| 17 | 2,719 B | 0.040 ms | ~0.2-0.4 ms | **25-39 ms measured** | — | 25-39 ms |
| 500 | 79,516 B | 0.74 ms | 4-7 ms | 25-39 ms | 16-80 ms | **~50-145 ms** |
| 2,000 | 318,016 B | 2.90 ms | 15-30 ms | 25-39 ms | 64-318 ms | **~110-400 ms** |

The device figures are an ESTIMATE: the one real device datapoint (17 rows, 25-39 ms) is almost all fixed cost
(open + fsync + remove + rename), and the marginal SD throughput of this card is not measured anywhere in the
repo. 2,000 rows is above ~150 ms across most of the plausible range; 500 rows is borderline. One `archive: … ms`
line on the glass at 2,000 rows would settle it. **While it runs** the MAIN task is inside `contactsPersistTick`:
no key is read (the BSP still queues them) and no frame is drawn — the screen and input freeze for that time; the
pop-up's 10 s expiry, the clock row and the auto-repeat are checked on the same task, so they are late by the same
amount; the LED (`om-led`, separate task), the sound and the radio keep running. One more wait: the radio task's
`dm-*.log` append takes the same FatFs volume lock, so an RX that wants to log a DM waits for the write too — true
today and unchanged by this ticket.

**(2) Every trigger and its floor.** (Only `contactsPersistTick` writes; the radio task only raises flags.)

| trigger | call | archive floor before | now |
|---|---|---|---|
| new node (first verified advert) | `advertInArchive` + `markContactsDirty(true)` | 30 s | 30 s |
| rename / type change / path_len change | same, `structural` | 30 s | 30 s |
| route learned (PATH) or cleared (failed direct DM) | `markContactsDirty(true)` | 30 s | 30 s |
| repeat advert (last-heard/SNR/position only) | `markContactsDirty(false)` | **30 s** | **10 min** |
| new-dot ack | `markContactsAck` (5 s radio floor) | 30 s | 30 s |
| favourite toggle | `contactsSetFavourite` → structural | 30 s | 30 s |
| add to radio / remove from radio / delete everywhere / clear radio | forced | immediate | immediate |
| leaving Nodes with anything dirty | `contactsAckLeave` → forced | immediate | immediate |
| Esc to the launcher | `nodeStorageShutdown` → forced | immediate | immediate |
| a failed archive write | — | 30 s retry | 30 s retry |

The 10-minute floor did **not** apply to the archive at all before this ticket — only to `radio.bin`'s soft
changes. **Worst case per hour** (30 new nodes/h + 300 known nodes re-advertising every 3 h ≈ 100 adverts/h): the
dirty stream is 130 events/h, one every ~28 s, so the 30 s floor is always expired → **120 writes/h = 38.2 MB/h**
of card writes, and 120 × (0.11-0.4 s) = **13-48 s/h of frozen main task** (1.3 % of the time), a hiccup every
30 s. With the fix: structural events (30/h) keep 30 s, soft-only gets 6/h → **~36 writes/h (11.4 MB/h, 4-14 s/h,
-70 %)**; on a mesh with re-adverts and no new nodes it is **120 → 6/h (-95 %)**. User actions are still unbounded
(each forced action is one full write), by design — that is the power-cut guarantee after a delete.

**(3) The fix, and why this one.** Implemented: the archive's floor is now **structural = 30 s, soft = 10 min**
(`core/node_store.h:81-92` `archiveWriteDue()`, used at `main/mc_link.cpp:959-1011`; the structural flag is raised
in `markContactsDirty`/`markContactsAck`, `mc_link.cpp:216-233`; a failed write re-raises both so it retries on
30 s, never 10 min). Chosen over the low-priority writer task because it is the **smallest** change that keeps
every power-cut guarantee: no new task, no second 318 KB PSRAM buffer, no shutdown wait, the same
`writeFileAtomic`, the same file format — and the trigger table shows about three quarters of the events on a
busy mesh (100 of 130/h) are repeat adverts that change nothing a reboot could lose (the row is already on the card; only last-heard/SNR/position lag,
exactly the fields `radio.bin` already lets lag 10 minutes). **The freeze is made rarer, not shorter** (up to
20× on a re-advert-only mesh, ~3× in the 30-new-nodes/h worst case): a
single 2,000-row write still costs the estimate above. If that is visible on the glass at 30 structural writes/h,
the next step is the writer task (second PSRAM buffer + priority-1 task + a shutdown wait), which I did not do
blind — it cannot be measured from this sandbox and it is a much bigger change to a data-integrity path.

### Armed tests (fix reverted → the FAIL line, then restored byte-identical)

| rule | armed line | FAIL observed (with the rule reverted) |
|---|---|---|
| A2 torn-temp rule → "delete every refused temp" (EST-472's behaviour) | `test_node_store.cpp:195` | `FAIL test_node_store.cpp:195  !om::mc::nodeFileTempIsTorn(om::mc::NodeFileResult::BadVersion)` (222 checks, 1 failure) |
| B archive floor → one 30 s floor (v68's behaviour) | `test_node_store.cpp:213` | `FAIL test_node_store.cpp:213  !mc::archiveWriteDue(31000, 1000, false, true)` (216 checks, 1 failure) |

Both restored: `cmp` identical to the saved copies; the whole suite then 228 checks / 0 failures in both stages.
The other four fixes are device glue with no host seam (file syscalls, boot state, the screen-off event order, a
main-loop timer) and are marked as such rather than given a test that could not fail.

### Honest limits of this run

- **Nothing was installed, flashed or seen on glass.** Every device claim is from reading the code and the host
  model; the archive timings are the estimate above, **not** a measurement at 500/2,000 rows.
- **The kept temp is still reused:** the next write opens `nodes.tmp` with `"wb"`, so a downgraded app overwrites
  the newer file on its first write (see A2). The fix stops the boot from destroying it, not the format change
  from doing so eventually.
- **The archive freeze is reduced in frequency, not duration** (B3); the async writer is named as the next step.
- **A4's µs race** and **A7's signed-plain text offset** are reported, not fixed (the ticket says report only for
  A7; A4 loses no data).
- **No SSH and no server access** were needed or made; the Tanmatsu is on Filip's USB and is not visible from this
  sandbox. `.git` untouched, no commit.

### Next step

Claude reviews and commits v69; the next install is Filip's. If he wants the freeze gone rather than rare, the
priority-1 archive writer task is the follow-up (EST-476): one extra PSRAM buffer, `xSemaphoreCreateBinary` +
`xTaskCreate` in `nodeStorageBegin`, the shutdown waiting for `busy`, and the same `writeFileAtomic`.


**Claude's review of EST-475 (S516, 2026-11-20 20:2x):** read the diff against its callers. Accepted: a failed rename keeps the finished temp (the next boot recovers it; a later write rewrites the temp, which is torn only if the power is cut during that write, and then the CRC rejects it); only a torn temp (Truncated/BadCrc) is deleted, another format's temp is kept; the boot seeds the radio signature so the NVS fallback is not rewritten on every exit; the ack dwell restarts on the row that slid up after a delete; 'r' no longer clears a pending CLI line; the archive's soft changes (last heard, SNR, position) wait 10 min while structural changes, acks and failures keep 30 s and user actions still force. Trade-off accepted: a node's new position can be lost if the power is cut within 10 min of the advert. **Open, recorded for Filip's list:** item 7, a signed-plain DM (room servers) is decoded at the wrong text offset (MeshCore's signed text starts at plain[9], ours at plain[5]), shows garbage and gets no ACK: not fixed tonight (rooms are not supported yet). The freeze at 2,000 rows is an ESTIMATE (110-400 ms); measure on the device before building a writer task. Suite exit 0, build exit 0 (1,742,128 B).
## 2026-11-20 · S516 (EST-474) late CLI answers

> Written by **DeepSeek** for ticket **EST-474**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed. Starting point **v67 `52a36d8`, tree clean** (this entry is written FIRST and filled
> in as the work goes). The defect is the one EST-473 § 6a found and did not fix: `get tx` misses its deadline, the
> page says "no answer", `get lat` goes out — and the slow `> 14` then lands in the **Position** row.
>
> **DECIDED (Claude): option 2.** After a CLI line times out, the NEXT line is held for **one more deadline** (the
> same estimate the timed-out line used). An answer inside that window belongs to the **timed-out** line: a `get`
> fills its value, a `set` answer shows its OK/error notice and re-reads as usual — then the held line is sent.
> With no answer the timed-out line stays **"no answer"** and the held line is sent at the window's end. One window
> per timed-out line, never stacked; logout / leaving the repeater clears it; `set lat` → `set lon` keeps its rule
> (the second half is dropped on a timeout). The rule lives in the **pure model** (`core/rptr_model.*`); `rptr_link`
> only feeds it time and answers.
>
> **RESULT:** version **68** (`core/settings.h`). `sh test/host/run.sh` **exit 0**, suites summed **34,046 → 34,192**
> (+146: **rptr 294 → 367 in BOTH the -O2 and the ASan stage**, +73 each; every stage included, the only sanitizer
> noise is still the vendored Ed25519's pre-existing `left shift of negative value` in `fe.c`). `make build` **exit
> 0**, `application.bin` **1,741,568 B (0x1A9300)**, **+16 B** over v67's 1,741,552 B; DIRAM **297,196 B used /
> 275,172 B free (51.92 %)** — unchanged, the new state is one `bool` inside an already padded struct (total image
> 1,741,160 B by `idf.py size`). One defect fixed, its whole rule host-tested; the two required cases armed and their
> FAIL lines pasted below. Nothing installed, flashed or seen on glass; `.git` untouched, no commit.

### The model change

- **`core/rptr_model.h`** — `CliPending` gains `bool late` and the state machine `CliEvent tick(uint32_t nowMs)`
  (`Idle` / `Waiting` / `TimedOut` / `Closed`) plus `canSend()`. The doc comment states the rule in one place.
- **`core/rptr_model.cpp`** — `begin()` resets `late`; `tick()` is the only place time moves the line. At its own
  deadline it does **not** clear: it keeps `line`/`row`/`isRead`, moves `sentMs` to the old deadline and the deadline
  on by exactly the estimate it was sent with (`deadlineMs - sentMs`), and reports `TimedOut`. A second call can only
  report `Waiting` (still inside the window) or `Closed` (window over → `clear()`). Never two windows: the slot is
  released before the next line is sent.
- **`main/rptr_link.cpp` `tick()`** — the old `timedOut → clear → noteAnswer("no answer")` block is replaced by
  `s_cli.tick(now)`:
  - `TimedOut` → one WARN and **`s_second[0] = '\0'`** (the `set lat` → `set lon` rule: a late OK to the first half
    can never release the second).
  - `Closed` → `noteAnswer("no answer", true)`. The page keeps showing "waiting for the repeater…" during the
    window (`cliBusy()` is `used`) and only then says "no answer" — the honest order.
  - Because the slot stays `used`, the **existing** `!s_cli.used` guards hold `clock sync` and the read ladder with no
    extra state; on the `Closed` pass the slot is already free, so the ladder sends the **held line in the same
    pass**. After a late answer, `onCliAnswer`'s own `s_cli.clear()` ends the window and the held line follows on the
    next pass.
- **`main/rptr_link.cpp` `onCliAnswer()` — not changed.** It already reads `s_cli.line/row/isRead`, so a late answer
  inside the window routes to the row that asked: a `get` fills its value, a `set` shows its OK/error notice and
  schedules the usual re-read. Outside the window (slot already cleared and the held line in it) it is the held
  line's, exactly as before.
- **Logout / leaving the repeater** already calls `s_cli.clear()` (`logout()`, `loginStart()`, `settingsReadStart()`),
  which now also drops the window. No window can survive into the next session.

### Tests

All in `test/host/test_rptr.cpp` (`testCliPending` extended, new `testLateAnswerWindow`), driving the real
`om::rptr::CliPending` and the real `settingsApplyRead()` the shell calls — 73 new checks per stage:

1. **The exact EST-473 § 6a case:** `get tx` times out at 9 s; `> 14` arrives at 12 s **inside** the window →
   `settingsApplyRead(&s, p.line, "> 14")` sets **TX = 14** and **Position stays unread**; `canSend()` is false until
   then, and the held **`get lat`** goes only after the answer (its own deadline starts at its send, 12 s + 8 s).
   1b: a late answer to a **change** still knows it is a change (`!isRead`, `set tx 20`, TX row) so the notice +
   re-read path is the same.
2. **Window closes with no answer:** TimedOut at 9 s, Waiting at 12 s and 16.999 s, **Closed at exactly 17 s** (one
   more deadline); the row was never applied (stays "no answer"/unread), `canSend()` true, held `get lat` goes.
3. **Answer after the window** while the held line is in flight is the held line's (unchanged): `get lat` + `> 46.25`
   fills Position, TX stays unread.
4. **Logout clears the window:** `clear()` gives `!used && !late`, `tick` → Idle, and a fresh line gets a fresh
   deadline.
5. **Two timeouts in a row:** after the first window the held `get lat` goes and times out at **its own** deadline;
   it gets its **own single** window (17 s + 8 s = 25 s, deadline 33 s, not stacked), keeps `get lat`/Position, and
   its late answer fills Position while TX stays the untrusted row.
   The 32-bit wrap is covered too (deadline 0x1E40, window end 0x3D80).

**ARM.** The model's `tick()` was temporarily put back to **v67's rule** (clear at the deadline, nothing held; the
API kept so the suite compiles) — the rest of the tree unchanged — then restored and `cmp` says the file is identical
again. Whole rptr stage: **367 checks, 44 failure(s)**; the two required cases:

```
FAIL test_rptr.cpp:713  p.tick(9000) == CliEvent::TimedOut
FAIL test_rptr.cpp:714  !p.canSend()
FAIL test_rptr.cpp:715  p.used
FAIL test_rptr.cpp:716  p.late
FAIL test_rptr.cpp:717  got "" want "get tx"
FAIL test_rptr.cpp:718  p.row == (uint8_t)SetRow::TxPower (got 255, want 5)
FAIL test_rptr.cpp:719  p.isRead
FAIL test_rptr.cpp:721  om::rptr::settingsApplyRead(&s, p.line, "> 14")
FAIL test_rptr.cpp:722  s.txOk
FAIL test_rptr.cpp:723  s.tx == 14 (got 0, want 14)
FAIL test_rptr.cpp:726  p.tick(12000) == CliEvent::Waiting
```
```
FAIL test_rptr.cpp:751  p.tick(9000) == CliEvent::TimedOut
FAIL test_rptr.cpp:752  p.tick(12000) == CliEvent::Waiting
FAIL test_rptr.cpp:753  p.tick(16999) == CliEvent::Waiting
FAIL test_rptr.cpp:754  !p.canSend()
FAIL test_rptr.cpp:755  p.tick(17000) == CliEvent::Closed
```

**Not done / not visible here:** nothing on the glass (no install, as the ticket says), so the real 8 s/8 s timing
and the page's "waiting → value" sequence are untested on the device; no repeater with a > 8 s CLI answer was
available in this sandbox. One judgement call for review: a user's new `set`/`get` pressed **during** the window is
refused like any other line ("Not sent: a line is still waiting for its answer") — the ticket did not say otherwise.


**Claude's review of EST-474 (S516, 2026-11-20 20:0x):** read the model (`CliPending::tick`: one window per line, deadline moved by the same estimate, `clear()` on close; the int32 compare handles the wrap) and the link (`TimedOut` drops the paired `set lon`, `Closed` notes "no answer" as before; the existing `!s_cli.used` guards hold the clock sync and the read ladder through the window; `onCliAnswer` unchanged, so a late answer fills the row that asked and its `clear()` releases the held line). Accepted, including DeepSeek's call that a key press during the window is refused like any busy line. Suite exit 0, build exit 0 (1,741,568 B).
## 2026-11-20 · S516 (EST-473) bug sweep round 2

> Written by **DeepSeek** for ticket **EST-473**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed (`.git` was read only; Claude reviews and commits). Starting point **v66 `a7da1c3`**
> — round 1 (EST-472) plus Claude's two review fixes. **Round 2 covers the user-facing features round 1 did not
> touch:** chat selection + message menu (v64), time row (v63), map nodes (v63), message LED (v63), new-node dots
> and their ack (v62), repeater part 2a (v65), the new-message pop-up (v62). **Version: 67 only if the code
> changes.** This entry is written FIRST and filled in as the sweep goes.
>
> **RESULT:** version **67** (`core/settings.h`). `sh test/host/run.sh` **exit 0**, suites summed
> **33,971 → 34,046** (+75 checks: nodes_fresh 85 → 111 in the -O2 and the ASan stage, msg_popup 51 → 60, rptr
> 287 → 294 in both; every stage included, the only sanitizer noise is still the vendored Ed25519's pre-existing
> `left shift of negative value` in `fe.c`). Both numbers come from the same script run in THIS ticket, on v66 and
> on v67 — the round-1 entry's 33,244 is its own count and I did not try to reproduce it.
> `make build` **exit 0**, `application.bin` **1,741,552 B (0x1A92F0)**, +160 B over v66's 1,741,392 B (total image
> 1,741,148 B; flash image 1,630,930 B); DIRAM **297,196 B used / 275,172 B free (51.92 %)** — unchanged from v66,
> because every fix is code and strings and adds no RAM. **Four defects found: three fixed, each with an armed host
> test, one NOT fixed** and argued in § 6a (a protocol limit, not an oversight). Nothing was installed or seen on
> glass.

### The eight-item checklist (one line each)

0. **checked, nothing new** in (a); one NEW instance of (b) — found and fixed in § 5; (c) every MeshCore constant
   verified against the vendored source (§ 0's table), one comment-only mismatch (`kMaxPlain`'s cited formula).
1. **checked, nothing** — the box is kept by seq (`main/main.cpp:2574`) and a dim row runs nothing
   (`main/main.cpp:3102`); the reply prefix is bounded (`core/chat_select.cpp:122`, `main/main.cpp:3114`).
2. **checked, nothing** — ONE bump site (`main/mc_link.cpp:1471`), reached only from the seen-ring drop
   (`:1510`); the "heard = copies+1" pairing holds on every path (`core/chat_meta.cpp:83-101`).
3. **checked, nothing** — `has_position` is tested before projecting (`main/map_page.cpp:150`), the cull is
   half-open (`core/map_nodes.cpp:30`), the lock is held for the marker pass only (`main/map_page.cpp:270`).
4. **checked, nothing** — the 20 s group is modular (`core/message_led_rule.h:115`), I2C only on a changed frame
   (`main/message_led.cpp:115`), automatic mode restored on every stop path (`:59-65`, `:196`).
5. **defect (fixed, test armed)** — the frozen Nodes order holds STORE INDEXES and a delete shifts the rows
   (`core/nodes_fresh.h:87`, `main/main.cpp:4048`). Severity: mislead.
6. **defect (not fixed, why)** — a CLI answer that arrives after the deadline can be applied to the NEXT line
   (`main/rptr_link.cpp:259` then `:648`); **defect (fixed, test armed)** — the action rows' confirm sheet said
   "The repeater says nothing now" (`core/rptr_model.cpp:682`). Severity: mislead / cosmetic.
7. **defect (fixed, test armed)** — a batch of messages arriving with no pop-up up lost its "+N more"
   (`core/msg_popup.h:80`). Severity: mislead.

### 0. THE CLASS round 1 found — searched for again, all over the tree

**(a) a struct holding a pointer into its OWN buffer, returned by value or copied.** Exactly one exists and v66
already fixed it: `DmSummary` (`core/mc_store.h:268-296`) — `name` may point at its own `short_name`, and the copy
constructor/assignment re-point it (`copyFrom`), tested at `test/host/test_store.cpp:299`. I re-read the copy path
and then swept every header in `core/` and `main/` for a struct that holds BOTH a char array and a
`char*`/`const char*` member: the only two are `ChatView` (`main/chat_page.h:60`) and `SettingsView`
(`main/settings_page.h`), whose pointers are set by the caller for one draw and never point into their own storage.
**checked, nothing new.**

**(b) a pointer taken from a ring, the node table or a static buffer and kept after the data can move.**

- `DmSummary.name` / `ChannelSummary.sender|text` point into the rings; the Chat list and the open thread are
  filled AND drawn inside one `StoreLock` (`main/main.cpp:2456-2557`), so a wrap cannot move them mid-draw.
  **checked.**
- A static-buffer getter called twice inside one `snprintf`: the Settings getters each own their buffer
  (`nodesStoresValue` `main/main.cpp:625`, `mapStartZoomValue` `:659`, the pack rows `:694-704`) and
  `tm::settingsRowValue()` is called ONCE per row (`main/settings_page.cpp:99`), immediately drawn. **checked,
  nothing.**
- **One NEW instance: the Nodes frozen order** — an array of STORE INDICES kept across a DELETE, which shifts the
  rows down one. Fixed in § 5.
- ⚠️ **Latent, NOT a defect today (named, not changed):** `main/main.cpp:2539/2572/2587-2588` size the chat draw's
  `srcs[]`, `seqs[]`, `line_counts[]` and `wraps[]` by `om::mc::kMaxDms` while they are filled with CHANNEL
  messages (`messagesForChannel(..., kMaxMessages)`). Today `kMaxMessages == kMaxDms == 64` (`core/mc_store.h:39`,
  `:214`), so nothing overflows; the *constant* is the wrong one and a future ring resize would overflow the stack.
  No fix here — there is no defect to fix, and the ticket's rule is not to invent one.

**(c) protocol constants copied by hand — EVERY MeshCore constant in `core/`, against the vendored source.** The
tree was read at `firmware/lib/meshcore` (v1.18.0) and `firmware/reference/meshcore-repeater-v1.18.0`:

| our constant (`core/…`) | value | vendored source | ok |
|---|---|---|---|
| `kMaxPayload` `mc_wire.h:32` | 184 | `MeshCore.h:18` MAX_PACKET_PAYLOAD 184 | ✓ |
| `kMaxPath` `mc_wire.h:33` | 64 | `MeshCore.h:20` MAX_PATH_SIZE 64 | ✓ |
| `kSecretBytes` `mc_wire.h:34` | 32 | `MeshCore.h:6` PUB_KEY_SIZE 32 (GroupChannel::secret) | ✓ |
| `kKeyBytes` `mc_wire.h:35` | 16 | `MeshCore.h:10` CIPHER_KEY_SIZE 16 | ✓ |
| `kMacBytes` `mc_wire.h:36` | 2 | `MeshCore.h:15` CIPHER_MAC_SIZE 2 | ✓ |
| `kBlockBytes` `mc_wire.h:37` | 16 | `MeshCore.h:11` CIPHER_BLOCK_SIZE 16 | ✓ |
| `kMaxText` `mc_wire.h:38` | 160 | `helpers/BaseChatMesh.h:8` MAX_TEXT_LEN = 10×16 | ✓ |
| `kMaxPlain` `mc_wire.h:37` | 168 | `Mesh.cpp:544` `data_len + 1 + CIPHER_BLOCK_SIZE-1 > MAX_PACKET_PAYLOAD` → 168 | ✓ value |
| `kMaxNameChars` `mc_wire.h:39` | 31 | `NodePrefs::node_name[32]` (L2 `settings.h`) | ✓ |
| `kFloodPathHashSize` `mc_wire.h:44` | 2 | app choice; `path_len = ((hs-1)<<6)|n`, `Packet.h:80-81` | ✓ |
| `kRouteTransportFlood/Flood` `mc_wire.h:46-47` | 0x00/0x01 | `Packet.h:14-15` | ✓ |
| `kTypeAdvert/GrpTxt/Response/Control` `mc_wire.h:48-51` | 04/05/01/0B | `Packet.h:21-30` | ✓ |
| `kTxtTypePlain` `mc_wire.h:52` | 0 | `helpers/TxtDataHelpers.h:6` | ✓ |
| `kTypeTxtMsg/Ack/Path` `mc_dm.h:32-34` | 02/03/08 | `Packet.h:22,23,27` | ✓ |
| `kRouteDirect` `mc_dm.h:35` | 0x02 | `Packet.h:16` | ✓ |
| `kTxtTypeCliData/SignedPlain/CliCommand` `mc_dm.h:42-44` | 1/2/3 | `helpers/TxtDataHelpers.h:7-9` | ✓ (v66 fix) |
| `kAckBytes` `mc_dm.h:51` | 6 | `helpers/BaseChatMesh.cpp:244-250` `uint8_t ack_hash[6]` | ✓ |
| `kPathHashSize` `mc_dm.h:52` | 1 | `MeshCore.h:18` PATH_HASH_SIZE 1 | ✓ |
| `kOutPathUnknown` `mc_store.h:43` | 0xFF | `helpers/ClientACL.h:13` | ✓ |
| `kAdvTypeChat` `mc_identity.h:28` | 1 | `helpers/AdvertDataHelpers.h:8` | ✓ |
| `kAdvNameMask/LatLon/Feat1/Feat2` `mc_identity.h:29-34` | 80/10/20/40 | `helpers/AdvertDataHelpers.h:12-16` | ✓ |
| advert field ORDER flags→lat,lon→feat1→feat2→name | — | `helpers/AdvertDataHelpers.cpp:4-29` | ✓ |
| `kTypeReq/TxtMsg/AnonReq/Control` `rptr_wire.h:38-42` | 00/02/07/0B | `Packet.h:20-30` | ✓ |
| `kReqTypeGetStatus` `rptr_wire.h:46` | 0x01 | repeater `MyMesh.cpp:46` | ✓ |
| `kCtlDiscoverReq/Resp` `rptr_wire.h:47-48` | 0x80/0x90 | repeater `MyMesh.cpp:787/809`, `Mesh.cpp:70` | ✓ |
| `kAdvTypeRepeater` `rptr_wire.h:49` | 2 | `helpers/AdvertDataHelpers.h:9` | ✓ |
| `kDmRouteFlood` `dm_send.h:43` | 0x01 | `Packet.h:15` | ✓ |

**One comment-only mismatch:** `kMaxPlain`'s comment cites `MAX_PACKET_PAYLOAD - CIPHER_BLOCK_SIZE - 3`
(= 165, `MeshCore.h:19` `MAX_GROUP_DATA_LENGTH`) but the **value 168** is the limit `createGroupDatagram`
actually enforces (`Mesh.cpp:544`, `+1+16 > 184`). The value is right and every buffer that uses it
(`mc_wire.cpp:179`, `channel_scope.cpp:153`) is right; the formula in the comment is the wrong one. Not changed —
a code change here would be a comment edit inside a constant whose value is already correct, and I would rather
Claude decide whether to touch it.

### 1. CHAT SELECTION + MESSAGE MENU — checked, nothing

Read: `core/chat_select.{h,cpp}` and every call site in `main/main.cpp` (2,221-2,400, 3,060-3,260, 2,520-2,660).

- **A message arriving while selecting:** the box keeps a SEQ, never a slot, and the index is recomputed on every
  draw from the frame's own list (`main.cpp:2574`); `chatSelScroll` (`core/chat_select.cpp:116`) then scrolls so the
  SAME line stays framed and the window grows above it. A key press re-derives the index from the same list in one
  lock (`chatSelStep`, `main.cpp:2264-2292`), so a message arriving mid-press cannot move it by two.
- **Ring wrap:** if the selected seq was evicted, `main.cpp:2576` CLOSES the box instead of framing whatever took
  the slot. (One edge: if the eviction happens between two draws and a key lands before the repaint,
  `chatSelMove(-1, delta, n)` treats "not found" as "no box yet" (`core/chat_select.cpp:96-104`) and moves from the
  newest. That needs a 64-message burst inside one frame's gap; cosmetic, not reachable in practice.)
- **An empty chat:** Up from the composer does nothing (`chatSeqAtFromNewest(0) == 0`, `main.cpp:3244-3250`), so no
  box is opened on a channel with no messages.
- **DM vs channel:** the same three lambdas read the DM ring when `chat_dm_open` (`main.cpp:2237/2269/2300`), and
  `chatMsgCount`/`chatSeqAtFromNewest`/`chatSelStep` agree on which ring they are in.
- **Reply "@Name ":** the prefix buffer is 40 bytes and the name is at most 23 (`kNameBytes` 24), so "@" + 23 + " "
  always fits; UTF-8 is copied byte for byte (`snprintf %s`, no truncation); an EMPTY sender writes nothing
  (`core/chat_select.cpp:122-128`), so Reply simply hands the composer back. If the prefix would overrun the
  120-byte composer the insert is skipped and the draft is left exactly as it was (`main.cpp:3114`) — the EST-469
  rule Claude accepted, unchanged.
- **A sender not in the node table (or deleted):** the node is resolved under the lock at the moment Enter is
  pressed (`chatMenuBuild`, `main.cpp:2330-2360`), a row that could not be resolved is DIM with its reason, and a
  dim row runs nothing (`main.cpp:3102`). The DM row's label, and every row's right-hand reason, are COPIES
  (`ChatMsgRow::label/value`, `core/chat_select.h:43-44`), never a `const char*` into a buffer that could die.
- **Esc:** `chatCloseSelection()` (`main.cpp:2221-2228`) touches only the box/menu/sheet. Composer and
  `composer_len` are untouched, so the draft comes back byte for byte; every typed key while a sheet is up is
  swallowed (`main.cpp:3078-3082`), so nothing can reach the composer.

### 2. TIME ROW — checked, nothing

- **Nothing bumps the counters twice.** There is exactly ONE increment site (two loops, one function):
  `noteDroppedCopy` (`main/mc_link.cpp:1471-1484`), called only from the seen-ring drop branch (`:1510`), once per
  dropped frame. Our own echo and the ACK are different frames (different payload → different fingerprint), so an
  ACK cannot add a copy; a duplicate flood copy is one drop and so one count. `heard N×` = `copies + 1`
  (`core/chat_meta.cpp:83-88`) and `repeated N×` = `copies` (`:100-104`) therefore hold on every path.
- **"via <name>":** 0 matches → the hash itself, exactly 1 → that contact's name, 2 or more → **nobody's name**
  (`core/chat_meta.cpp:31-55`); a prefix-only row is not a node (`contactKeyIncomplete`). Same rule as the Info
  sheet's hop rows (`chat_select.cpp:210`).
- **Bytes:** set from the frame length for a channel (`main/mc_link.cpp:1898` via `takeWirePath`) and for an
  incoming DM (`:1711`), and from the built frame for our own DM (`:2155/2310`); 0 means unknown and prints
  nothing.
- **Older / restored records show nothing false:** `mc_history` writes `repeats = 0` and restores `copies = 0`,
  `frame_len = 0`, no route (`core/mc_history.cpp:69/176/291`), and `pathFromRecord` refuses the v57 fake shape
  (hops 1 with pathCount 0) so `hops_known` stays false (`:29-47`). The dim row then shows no hops, no via, no
  bytes and no count, and the Info sheet prints "not kept after a restart" instead of a zero.

### 3. MAP NODES — checked, nothing (with the lock number)

- **0,0 and out-of-range:** `positionDrawable` refuses 0,0 and outside ±90/±180 (`core/map_nodes.h:29-37`), and the
  marker loop tests `has_position` BEFORE projecting (`main/map_page.cpp:150`), so a row with stale lat/lon but no
  position flag can never be drawn.
- **A node exactly on a tile edge:** the projection is view-local subtraction of the pack's own world pixel
  (`core/map_nodes.cpp:12-35`) and the cull is half-open, so a point on the right/bottom edge belongs to the next
  view and one on the left/top edge to this one. `test_map_nodes` pins a tile corner at z9..z17.
- **Label overlap at every zoom:** the greedy is zoom-independent and runs only from z13 (`core/map_nodes.cpp:82-146`);
  a plate that would leave the view or overlap an already-placed one is skipped and the MARKER stays.
- **The node menu's Map at z15 outside the pack:** `centreOnNode` clamps the zoom and the world pixel
  (`core/map_nodes.cpp:160-172`), and the page then draws the void, the marker and the label — there are simply no
  tiles there. The page has no "no map data here" sentence; **noted, not a defect** (the void colour is documented
  as exactly that, `main/map_page.cpp:25`).
- **Cost per frame with 2,000 rows, under the lock:** the marker+label pass measured on the HOST (-O2,
  `test_map_nodes`) is **405-458 µs** for 2,000 rows with **1,450 markers in view and 16 labels** (300 rows: 70 µs).
  The lock (`om::mc::StoreLock` at `main/map_page.cpp:270`) is held for that pass and released before the panel
  blit, and the radio task takes the same recursive mutex for an advert insert (`main/mc_link.cpp:1816`), so it
  **does wait** — for that pass, on the pass a repaint happens. The device's own figure is NOT measured here (the
  host adds its share of disc/label drawing, ~1,450 discs, which the test does not do); a few ms is the honest
  estimate and no RX packet is at risk at that scale (the frame sits in the driver's queue, and a LoRa text is
  >100 ms of airtime).

### 4. MESSAGE LED — checked, nothing

- **The 20 s group across the `esp_timer` wrap and long gaps:** the phase is `(now - start) % 20000` on a uint32
  monotonic clock (`core/message_led_rule.h:115`), i.e. pure modular arithmetic; a wrap or a task blocked for
  seconds changes nothing. The 10 s pop-up timeout uses a signed difference for the same reason
  (`core/msg_popup.h:108-113`).
- **Unread cleared while locked:** nothing can clear it while the screen is off — every navigation/key event is
  swallowed (`main/main.cpp:3060-3065`) — and the moment it does reach 0 the next 25 ms tick leaves manual mode
  (`main/message_led.cpp:115` → `applyFrame`'s `!manual` branch, `:59-65`), so the blinking stops.
- **The choice changing in Settings:** `s_choice` is read on every tick and a different index restarts the burst
  (`core/message_led_rule.h:109-113`); the frame only reports `changed` when `manual`, `lit`, or `led` differs
  (`:126`), so a re-read of the same value costs no I2C.
- **Pixels 0-1:** in manual mode `applyFrame` writes all six dark and then lights only the chosen one
  (`main/message_led.cpp:79-82`). 0/1 are only ever turned OFF, which is the ticket's rule ("all LEDs would be off").
- **Automatic mode back on every path:** the screen coming on (`setScreenOff(false)`, `main.cpp:3050`), the unread
  reaching 0, the choice going Off (all three through the `!manual` branch), and Esc to the launcher
  (`restoreAutomatic`, `main/message_led.cpp:196-209`, called FIRST in `leaveToLauncher`, `main.cpp:217`).
  `begin()` also forces automatic (`:139`). The panic case stays unfixable in-process (round 1 § 3).
- **I2C traffic while locked:** three blinks = 6 changed frames (on at 0/300/600 ms, off at 120/420/720), then
  nothing until 20 s → **0.3 frames/s average**, each frame 6 pixel writes + one `bsp_led_send()`.

### 5. NEW-NODE DOTS + ACK — defect (fixed, test armed)

**The defect.** `nodesFrozenSync()` (`core/nodes_fresh.h:47-75`) keeps the page's frozen order true to the table by
dropping entries whose INDEX is out of range. But `Delete everywhere` (`main/main.cpp:4045` →
`core/node_table.cpp:87-96`, i.e. `contactRemoveAt`, `core/contact_admin.h:40-45`) **shifts every row after the
deleted one down one**. After the shift every frozen index is still in range, so nothing is dropped and every entry
above the deleted row now names the NEXT node: the list shows one node twice, the last node silently disappears,
and the box (re-found by key, `refocusAfterDelete`, `main.cpp:2196`) stays where it was while the row under it now
names another node. The snapshot also feeds the node menu and the ack, so both inherit the wrong mapping.

**The fix.** `nodesFrozenAfterRemove(f, removed)` (`core/nodes_fresh.h:77-103`) re-MAPS instead of re-sorting — the
ORDER the user is looking at is the whole point of the freeze, so the deleted node's entry goes and every entry
above it moves down one with its node. `main.cpp:4035-4048` takes the deleted row's index by KEY under the lock
BEFORE the delete and calls the re-map after it (an index of -1, the node already gone, remaps nothing). Every
other removal path is already safe: `RemoveContact`/`RemoveFromRadio` close the page (the snapshot is rebuilt on
entry, `main.cpp:4594`), and `Clear all` leaves every index out of range, which the sync drops.

**Armed** (`test/host/test_nodes_fresh.cpp:287-345`, a scrambled order [N3,N1,N4,N2] with N2 deleted).

**The rest of item 5, checked:**
- **Landing vs resting:** a key that LANDS on a row acks at once (`nodesAckKeyMoved`, `core/nodes_fresh.h:132`),
  a 1.5 s REST acks (`nodesAckTick`, `:140`), and opening the page acks nothing (`nodesAckEnter`, `:125`).
- **Persisted without a write per key press:** `contactsAckFresh` (`main/mc_link.cpp:1029`) only raises flags; the
  one writer applies a **5 s** floor (`kContactsAckWriteMs`, `:148`, used at `:858`) and forces the write on
  leaving the page (`contactsAckLeave`, `:1041-1049`).
- **A node deleted while fresh:** the row is gone, so `contactsFreshCount` drops and the dot clears
  (`hintDotMask`, `core/nodes_fresh.h:112-117`) — the delete path itself was the defect above.
- **The dot clears when the last fresh node is acked:** same function, from the same counter the badges use.
- **The order stays until leaving:** the snapshot is re-sorted only on page entry (`main.cpp:4592-4602`).

### 6. RPTR PART 2a — one defect NOT fixed, one cosmetic fixed

**6a. DEFECT (not fixed, why): an answer that arrives after the deadline can be applied to the NEXT line.**
`sendCliLocked` keeps one pending line (`main/rptr_link.cpp:146-171`, `if (s_cli.used) return false`), `tick()`
clears it at the deadline (`:259-268`) and the read ladder sends the NEXT `get` in the very same pass (`:280-287`).
`onCliAnswer` then applies whatever CLI text arrives to the line that is pending NOW (`:646-661`), and
`settingsApplyRead` validates it against **that line's** format (`core/rptr_model.cpp:424`). The CLI has no request
id, so a late answer is indistinguishable from the new line's answer. Concrete worst case: the answer to `get tx`
(`"> 14"`) is a legal coordinate, so the Position row would show **14.00000** as "the repeater says"; a late
`get name` answer is a legal owner info. Severity: **mislead** (a plausible wrong value presented as the repeater's
own; a change typed on top of it goes to the confirm sheet, which does name the line, so the user can catch it).
The `set lat`/`set lon` pair is safe (`s_second` is dropped on a timeout, `:265-268`), so a HALF position is never
applied.
**Why not fixed:** without a request id there is no sound way to attribute A's answer once B is on the air. The two
honest options both change the timing EST-470 chose: (1) after a timeout, drop the FIRST answer that arrives —
if the timed-out line never answered, that eats a good answer once; or (2) after a timeout wait one more deadline
(8 s) before the next line, and treat an answer inside that window as the old line's. This needs Claude/Filip's
call, not my guess at 20:30.

**6b. Two quick changes (queue of one): checked, nothing.** A second change while a line is pending is refused by
`sendCliLocked` and the page says "Not sent: a line is still waiting for its answer" (`main/main.cpp:3416-3419`);
nothing is replaced, and the row keeps the value it was READ as.

**6c. DEFECT (fixed, test armed): the confirm sheet's text for a row with nothing to compare against.**
`confirmWriteText` (`core/rptr_model.cpp:682-697`) ended every question with *"The repeater says nothing now."*
when the row had no value — which is every ACTION row (`advert.zerohop`, `advert`, `clock sync`,
`setRowActionCmd`) and every SETTING row not read yet. "Says nothing" is a claim about an answer that was never
asked, and for a not-yet-read row it reads like the repeater had answered. Now the sentence carries the value
clause only when there IS a value: `Send "advert"?`. Severity: **cosmetic** (it cannot act wrongly — only
mis-describe). Armed at `test/host/test_rptr.cpp:641-645`.
The other rows were read at their call sites (`main.cpp:3466-3490`, `rptrConfirm` `:2989`) and do name both the
exact line and the value: Name/Owner/Position/TX/Repeat/Power save all come from `rptrRowValue()`, i.e. the
repeater's own last answer. Reboot uses its own sentence (`confirmRebootText`, `:698`).

**6d. Position input: checked, nothing.** `cliParseTypedPosition` (`core/rptr_model.cpp:643-669`) accepts
"46.25, 14.5", "46.25,14.5", "  46.25 ,  14.5  " (the leading spaces are skipped by `cliStripPrefix`'s loop,
`:573-574`, the trailing ones trimmed at `:656-657`), and a leading `-`/`+` on either half (`:614-621`). Too many
decimals are rounded at the 7th (`:629-637`). Out of range is refused (lat ±90, lon ±180, `:664-665`) as are an
empty half, a missing comma, "abc", and a second comma (`parseUnsigned` rejects any non-digit, `:297`). Tests:
`test/host/test_rptr.cpp:609-628` (the 7-decimal rounding was pinned in this ticket).

**6e. TX power clamp: checked, nothing.** The stepper clamps its draft to 1..22 (`main/main.cpp:3524-3527`) and
`cliTx` clamps again (`core/rptr_model.cpp:558-565`), so no path can build `set tx 0` or `set tx 30`. Enter on an
unread row starts the draft at 14 (`main.cpp:3562`).

**6f. The password never in a log line or on screen: checked, nothing.** Every `ESP_LOG` in `rptr_*` was read
(21 in `main/rptr_link.cpp`; `rptr_page.cpp`, `core/rptr_model.cpp`, `core/rptr_wire.cpp` have none). The only
mention is *"password remembered for this repeater (never printed)"* (`rptr_link.cpp:142`). The CLI lines that ARE
logged (`:159/163/167/260`) never carry it. The login screen draws one dot per character
(`main/rptr_page.cpp:149-151`).

### 7. NEW-MESSAGE POP-UP — defect (fixed, test armed)

**The defect.** The radio task offers EVERY arrival; `main/chrome.cpp:172-186` keeps the newest head+text and a
COUNT, and `msgPopupPoll` passes that count as `arrived` (`:201-202`). `offer()` set `extra = 0` whenever no
pop-up was visible (`core/msg_popup.h:74-75` old), so **two messages inside one 250 ms poll** showed the newest and
said nothing about the other — the user never learned a second message arrived. (With a pop-up already up the count
was right: the message it was showing joins the not-shown ones, so `extra += arrived` is exactly right and is
unchanged.)
**The fix.** A brand-new pop-up counts everything but the newest: `extra = arrived - 1` (`core/msg_popup.h:80`).
**Armed** (`test/host/test_msg_popup.cpp:73-91`).

**The other paths, checked:**
- **Never our own:** `offer(..., own=false, ...)` is only reached after the seen-ring check, which drops our own
  echo before the DM/channel branch (`main/mc_link.cpp:1494-1512`).
- **Not for the open chat, channel AND DM:** `msgPopupPoll` builds the open chat's identity from the page state
  (`main.cpp:4420-4435`), and `sameChat` compares the KIND first, then the channel index or the peer key
  (`core/msg_popup.h:52-57`, `main/chrome.cpp:196-200`). The Chat LIST (`chat_open == false`) suppresses nothing.
- **Not for a CLI text:** `rptrlink::onCliAnswer` consumes a CLI_DATA/CLI_COMMAND text before the DM path
  (`main/rptr_link.cpp:619`), and the v66 second door drops one that missed it (`main/mc_link.cpp:1685`).
- **Not while locked:** `offer` with `screen_off` dismisses and queues nothing (`core/msg_popup.h:67-71`); the
  every-pass poll passes `g_screen_power.off` (`main.cpp:4435`), and BOTH power-button edges poll with
  `screen_off = true` (`:3040`, `:3048`) so a message that landed before the poll is discarded on the wake press
  (the v66 fix) and one that lands while off is never kept.
- **The 10 s timeout:** a signed difference, so the 32-bit wrap is safe (`core/msg_popup.h:108-114`), and a newer
  message RESTARTS the 10 s (`:88`). A key while it is up dismisses and is swallowed (`:100-105`, `main.cpp:3070-3077`).

### Armed tests (the fix reverted → the FAIL line, then restored)

| fix | armed line | FAIL observed (with the fix reverted) |
|---|---|---|
| § 5 frozen order after a delete | `test_nodes_fresh.cpp:315` | `FAIL …:315 om::ui::nodesFrozenAfterRemove(f, removed)` — and then, in the same run, `FAIL …:320 f.order[0] == 1 (got 2, want 1)`, `FAIL …:323 strcmp(s.c[f.order[0]].name, "N3") == 0` (the first row now shows N4) and 10 more |
| § 6c rptr confirm text | `test_rptr.cpp:641` | `FAIL …:641 got "Send \"set tx 20\"? The repeater says nothing now." want "Send \"set tx 20\"?"` (+ `:643` and `:645` for `advert` and `clock sync`) |
| § 7 pop-up batch count | `test_msg_popup.cpp:77` | `FAIL …:77 p.extra == 1 (got 0, want 1)` |

All three were re-run restored: 111 / 294 / 60 checks, 0 failures.

### Honest limits of this run

- **Nothing was installed, flashed or seen on glass.** The device-only claims (the lock's real duration, the I2C
  frames the coprocessor sees, the pop-up on the panel) are from reading the code and from the host model.
- **§ 6a is NOT fixed** and needs a decision (see the two options). Until then a repeater that answers later than
  8 s can put a wrong value in a row.
- **The map's device cost is an estimate.** The host figure (405-458 µs / 2,000 rows / 1,450 in view) is measured;
  the disc-and-label drawing on the P4 is not.
- **Not swept in this ticket** (round 1's scope, not repeated): locking, persistence/power cut, exit paths,
  no-`/int` caps, memory. Round 2 covered the seven user-facing features above only.
- **A latent trap was named and not changed** (the chat draw's `kMaxDms`-sized arrays holding channel messages, § 0b).
- **No SSH was made for this ticket**; `~/Projects` was read in place and the tree is left uncommitted for Claude.



**Claude's review of EST-473 (S516, 2026-11-20 19:5x):** read the code diff against its call sites. Accepted all three fixes. Frozen order: checked that Delete everywhere is the only path that shifts rows while Nodes is open (the table never evicts: `node_table.cpp:72`; Settings' Remove closes the Nodes page), and the index is taken by key under the lock before the delete. Pop-up batch count and the confirm text: correct. **§ 6a decided by Claude (a lower-level call, Filip not needed):** option 2, after a CLI timeout hold the next line for one more deadline and give an answer inside that window to the timed-out line; it never throws away a good read and costs 8 s only after a timeout. Filed as EST-474. Suite exit 0, build exit 0 (1,741,552 B). Not seen on the device yet.
## 2026-11-20 · S516 (EST-472) bug sweep of v57-v65

> Written by **DeepSeek** for ticket **EST-472**: fenced Paperclip run, no session start or end, nothing installed,
> flashed, pushed or committed. Starting point **v65 `c81fb4c`** (the tree Claude installed on the device on
> 2026-11-19). Sweep of the nine versions v57 → v65 (`22a1e86..c81fb4c`). **Version: 66 only if the code changes.**
> This entry is written FIRST and filled in as the sweep goes (the ticket's rule): an item is done not because its
> code looks plausible, but because its CALL SITES were read and, where a defect was found, a host test was armed.
>
> **RESULT:** version **66** (`core/settings.h`). `sh test/host/run.sh` **exit 0**, suites summed
> **33,084 → 33,244** (+160 checks; the ASan/UBSan stages included; the only sanitizer noise is the vendored
> Ed25519's pre-existing `left shift of negative value` in `fe.c`, unchanged). `make build` **exit 0**,
> `application.bin` **1,741,328 B (0x1A9210)**, 1,700 KiB = **53 % of 3,200 KiB**, 17 % of the 2 MiB app
> partition free. Nine defects found and fixed: **four have an armed host test** (five FAIL lines — item 6 has two
> tests), **five are device glue** with no host test and say so. Item 8 added evidence, not a defect. Nothing was
> installed or seen on glass.

### The ten-item checklist (one line each, filled in as the sweep goes)

1. LOCKING — every node-table access site and whether it holds `om::mc::StoreLock`.
2. PERSISTENCE + POWER CUT — `writeFileAtomic` / `readNodeFileOrTemp`, every cut point; write frequency.
3. EXIT PATHS — Esc per page, key-lock, pop-up, manual LED, backlights, final writes, `/int`, panic reset.
4. KEY-LOCK + POP-UP + LED interplay.
5. RPTR CLI answers — no pop-up, no Chat dot, no unread, no DM thread.
6. NO `/int` — "Radio full" and the headers use the cap of the store actually in use.
7. PARSERS + BOUNDS — advert app_data, CLI answers, time-row path, pop-up text, node file load; ASan stage.
8. MEMORY — internal RAM after the larger rings; FatFs-writing tasks' stack high-water.
9. KNOWN FIX A — a deleted node's DM thread stays listed, named from the log or the short key.
10. KNOWN FIX B — `initNodeTable()` one init, one log line.

### The ten-item checklist — one line each

1. **defect (fixed, no host test possible — a cross-task race):** `main.cpp:4108` (old) took
   `&contacts().c[idx]` with **no StoreLock** and kept the POINTER into the 2,000-row table for the Node menu's
   Map action. Every other access site holds the lock (the full list is in § 1 below); the fix copies lat/lon under
   the lock. Severity: **mislead/crash** (a row can move under a delete; a torn read is UB).
2. **defect (fixed, test armed):** `readNodeFileOrTemp()` renamed a power-cut temp file into place **before any
   CRC check** (`mc_link.cpp:667` old) and `writeFileAtomic()` ignored `fflush`/`fsync`/`fclose` failures and then
   removed the old file (`mc_link.cpp:611` old). Both now refuse; the CRC check is `nodeFileCheck()`
   (`core/node_store.cpp:90`). Severity: **lose data** (the radio list silently falls back to the 32-row NVS copy).
3. **defect (fixed, no host test possible — device glue):** `nodeStorageShutdown()` forced the archive and the ack
   but **not the radio file** (`mc_link.cpp:946` old), and the 10 s retry floor could swallow the last attempt.
   Now a structural pass + cleared retry floor, with the signature check still preventing a needless write.
   Esc per page, key-lock, pop-up, manual LED, backlights and `/int` were read and are correct; the panic case is
   **not fixable in-process** and is written down in § 3. Severity: **lose data** (last radio change).
4. **defect (fixed, no host test possible — device ordering):** a message that arrived while the screen was LOCKED
   and had not been polled yet popped up **on wake**, because the power press `continue`s past the 250 ms poll
   (`main.cpp:3043` old). The wake path now discards the pending pop-up with `screen_off` still true. The
   dismissal rule itself (any key swallows, Esc/Enter/arrows included) was already right. Severity: **mislead**.
5. **defect (fixed, test armed):** the logged-in repeater's CLI answer was handled by `rptr_link`, but a CLI-typed
   TXT_MSG that missed it (not logged in, a late answer, a repeater that is also a contact) fell into the DM path:
   thread + pop-up + unread dot (`mc_link.cpp:1655` old). A second door now drops it (`isRepeaterCliText`).
   Severity: **mislead** (repeater admin text presented as a person's DM).
6. **defect (fixed, test armed):** the cap of the store in use. Without `/int` the writer keeps **32** rows while
   the rule marked up to **300** and the sentence said "Radio full (300 / 300)" (`node_table.cpp:74`,
   `node_menu.h:169`, `node_view.h:74/85` old): a node shown as "in the radio" was gone after a reboot. The cap is
   now passed in (`radioCapFor(int_mounted)`) and every sentence/header uses it. Severity: **lose data/mislead**.
7. **defect (fixed, no separate device test):** the pop-up's own byte-by-byte wrapper could end a 95-byte line
   **inside a UTF-8 sequence** (`chrome.cpp:58` old). It now reuses `mc::wrapText`, the host-tested UTF-8-safe
   wrapper the chat page already draws with. Advert app_data, CLI answers, the time-row path, and the node-file
   count/record-size fields were read at their call sites and are bounds-checked (`§ 7`). Severity: **cosmetic**.
8. **defect (fixed, no host test possible — device glue):** the two FatFs writers had no measured stack line, and
   the boot log had no internal-RAM figure. Added: one heap line at boot + one stack low-water line per writer
   task (`mc_link.cpp:819`). Measured headroom is in § 8: **DIRAM 297,196 used / 275,172 free**. Severity: evidence.
9. **defect (fixed, test armed):** a deleted node's thread **was** already listed by `dmThreadSummaries()` (the
   "leaves the chat list" part of the note is NOT in the model — see § 9), but when its own message carried no
   name the core returned `name = ""` (`mc_store.cpp:424` old). It now names it from the short key. Severity:
   **mislead** (a thread with no label).
10. **defect (fixed, no host test possible — device log):** `initNodeTable()`'s banner printed on all three calls
    ("node table: 2000 rows" ×3). One static, one line (`mc_link.cpp:190`). Severity: **cosmetic**.

### 1. LOCKING — every access site and its lock

`main/mc_link.h:257` `StoreLock` is a recursive mutex created in `begin()`; the radio task writes the table
(`advertInArchive`, PATH routes, DM cursors, archive merges at boot), the main task reads it. Sites:

| site | lock? |
|---|---|
| `main.cpp:392` Settings › General "contacts" row | yes (`:390`) |
| `main.cpp:944` `buildNodesSubRows` (whole function) | yes (`:943`) |
| `main.cpp:1182` `chatMetaFor()` → `pathHopLabel` (time row "via <name>") | yes — called at `:2544`/`:2557` inside `drawChatPage`'s lock |
| `main.cpp:1885` `nodesSubAction` name/key copy | yes |
| `main.cpp:1911` `raiseClearContacts` count | yes |
| `main.cpp:1952` `nodeMenuPub` key copy | yes |
| `main.cpp:1963` `nodesOrderForPage` | yes |
| `main.cpp:1978` `nodesApplyFilters` (contract: caller holds) | yes at its two callers (`drawNodesPage`, `nodesShownCount`) |
| `main.cpp:1987` `nodesViewCount` | yes |
| `main.cpp:2001–2043` `drawNodesPage` (store pointer + order + counts) | yes, whole fill AND draw; released before the confirm sheet |
| `main.cpp:2092` `nodesShownOrder` | yes |
| `main.cpp:2121` `toggleFavourite` read | yes |
| `main.cpp:2151` `openDmThread` key/name copy | yes |
| `main.cpp:2174` `raiseRemoveContactConfirm` | yes |
| `main.cpp:2196` `refocusAfterDelete` re-find by key | yes |
| `main.cpp:2204` `nodeMenuRowCount` | yes |
| `main.cpp:2235–2316` `chatMsgCount`, `chatSeqAtFromNewest`, `chatSelStep`, `chatSelLoad` | yes (each takes the lock) |
| `main.cpp:2344–2389` `chatMenuBuild`, `chatInfoBuild` | yes |
| `main.cpp:2456–2557` `drawChatPage` (messages, read state, DM ring, summaries) | yes |
| `main.cpp:2712` `drawMapPage` store pointer | the pointer is taken outside, but `main/map_page.cpp:270` takes `StoreLock` around **every** row read (`drawMapNodes`; the tile compose never holds it) |
| `main.cpp:3291` Enter on a DM card (`dmThreadSummaries`) | yes |
| `main.cpp:4125` Node menu → Map (S516 fix) | yes, now: lookup + lat/lon copied under the lock |
| `main.cpp:4555–4558` hint dots (`unreadTotals`, `contactsFreshCount`) | yes |
| `main.cpp:4581` Nodes entry re-sort | yes |
| `main/rptr_link.cpp:188` `mergeContacts` | yes |
| `main/rptr_link.cpp:229` `contactForKey` in `loginStart` | yes (all three callers wrap it) |
| `main/rptr_link.cpp:338/461` `contactForKey` in scan/login | yes |
| `main/mc_link.cpp:1737` message pop-up head | **no table read at all**: the DM head is the message's own `peer_name` (captured on RX), the channel head the channel name, so nothing is pointed at |

No UI keeps a `Contact*`/reference after its lock is released, and no page holds the lock across a panel blit.
The only violation was the Map action (item 1's fix). Severity: a data race is **UB/crash** on a delete; the
symptom would be a wrong marker or a torn name.

### 2. PERSISTENCE + POWER CUT

`writeFileAtomic(path,tmp)`: write temp → `fflush`/`fsync`/`fclose` → remove old → rename. Cut points and what
the next boot loads:

| cut | next boot |
|---|---|
| during the temp write | `path` (old file) intact → old bytes; the partial `*.tmp` is ignored (stat of `path` succeeds) |
| after the old file is removed, before the rename | `path` absent, `*.tmp` complete + fsynced → `readNodeFileOrTemp` CRC-checks it (`nodeFileCheck`), renames it, loads it. **Before S516 it renamed first and trusted afterwards** |
| during the rename | FAT's directory entry is the atomic step: either the old/new name resolves; the CRC still decides |
| temp corrupt (bad medium) | old file already gone → the temp is **removed**, the log says which check failed, and the radio list falls back to NVS (max 32) / the archive is skipped. Nothing half-fills the table (`nodeFileDecode`'s checks all precede its first write) |
| write reported short, or `fflush`/`fsync`/`fclose` failed | **old file kept**, temp removed (S516). Before, the old file was removed even when the data may not have been durable |

`nodes.bin` on the SD is written the same way; the NVS fallback is one `nvs_set_blob` + `commit` (atomic in NVS).
Write frequency: a **new/renamed/route-changed advert** raises the structural flag and the radio file is encoded on
the next main-loop pass, but written only if the **signature** changed — an archive-only new node costs no internal
flash write. **Repeat adverts** (last-heard/SNR) wait the 10-minute soft floor. The **SD archive** is written at
most once per 30 s while anything is dirty (a busy mesh therefore rewrites `nodes.bin` on a steady 30 s cadence,
not per packet), plus forced by a user action (add/move/delete/clear radio) and on leaving Nodes/Esc. **No path
writes per received packet.** Per key press: only a deliberate favourite toggle (structural, immediate) and a
"new dot" ack (5 s floor, forced on leaving) — both user actions, both stated in the confirm/log lines. Severity
of the CRC gap: **lose data**; of the fsync gap: **lose data**.

### 3. EXIT PATHS

- **Esc to the Launcher** from every page: Chat closes Info → menu → selection → the open chat → the list, then the
  shared `core.handleKey(Esc)` (`main.cpp:4382`) goes to the Launcher; Nodes closes trace result → confirm/menu →
  list; Map/RPTR/Settings do the same. Only Launcher-Esc calls `leaveToLauncher()` (`main.cpp:4307`).
- **Key-lock:** while `g_screen_power.off`, every navigation/keyboard/scancode event is swallowed
  (`main.cpp:3052`), Esc included; the power button is the only wake. Correct: keys in a pocket do nothing.
- **Pop-up up:** the pop-up check is before every page branch and sets `k = None` (`main.cpp:3071`), so Esc/Enter/
  arrows only dismiss. The power button is handled as an ACTION before that (it turns the screen off and the
  screen-off poll drops the pop-up — no stale pop-up on wake).
- **Message LED in manual mode:** `leaveToLauncher()` calls `om::led::restoreAutomatic()` **first**
  (`main.cpp:217`): all six pixels dark, the remembered system brightness restored, `bsp_led_set_mode(true)`, and
  the LED task stopped. The system's own indicators 0–1 come back. The manual-mode `applyFrame` re-checks `s_stop`
  under the output mutex, so a frame in flight cannot re-darken them after the handover.
- **Backlights:** if the screen was off on exit, `screenLightsOn()` restores display and keyboard brightness.
- **Final writes:** `nodeStorageShutdown()` now forces **radio.bin + the archive + the ack** past their floors and
  clears the retry floor (S516); the signature check means an unchanged radio list is not rewritten. `/int` is
  unmounted after the writes (`unmountInt`, `mc_link.cpp:701`). The SD card is **not** unmounted (the BSP keeps the
  card handle; documented since S503; every history record is opened/written/closed per record).
- **After a panic reset:** no code of ours runs, so the CH32 can be left in **manual LED mode** (all pixels dark,
  including the system indicators) and any node/radio write in flight is lost at the write's cut point (the temp
  rules above bound that loss). `/int` is released by the reset itself (the mount lives in RAM), so the launcher
  mounts `locfd` normally; our `message_led::begin()` restores automatic mode the next time OM Lite starts. This
  is **not fixable in-process**; it is the price of owning the LEDs while the screen is dark.

### 4. KEY-LOCK + POP-UP + LED

- A message while locked: no pop-up (the poll discards it, and the consume is now also done on wake), no
  backlight, no panel; the sound plays (`audioout::playEvent` runs on the radio task) and the LED rule sees
  `screen_off && unread > 0` and blinks three times, then every 20 s.
- A message that arrives while locked but **before** the 250 ms poll is consumed on the wake press with
  `screen_off=true` (S516 fix) — it is not queued for later.
- Pop-up dismissal swallows the key (Esc included) before any page branch; Enter and arrows do nothing else.
- Power while a pop-up is up: the screen goes off and the pop-up is dropped with it; nothing is shown on wake.
- The LED stops the moment the screen is on, the unread count is 0, or the choice returns to Off.

### 5. RPTR CLI ANSWERS

Path: `mc_link::onPacket` TXT_MSG branch → `rptrlink::onCliAnswer` **before** the DM code. It only accepts
`TXT_TYPE_CLI_DATA`, only the login target's key, and only while logged in (`rptr_link.cpp:619`); it returns
before `dmPush`, so no thread, no pop-up, no Chat dot, no unread. A **plain** text from the same repeater still
falls through. The second door added in S516 (`mc_link.cpp:1659`) drops a CLI-typed text that missed the first
one, so it can never become a DM either. Host test: `test_dm.cpp:184` (the classifier over a real CLI frame).

### 6. NO `/int`

`radioCapacity()` (`mc_link.cpp:1047`) returns `radioCapFor(g_int_mounted)`: 300 over `/int`, **32** on the NVS
fallback. It is passed to `addToRadio`, to `advertInArchive`'s automatic-add test, to `nodeRadioFullNotice`, to
`nodeStoreCountsText`/`nodeStoreInfoText` (Nodes head + Settings' Stored nodes row) and to the `NodesView`
(`radio_cap`). A full radio refuses and changes nothing; the sentence names the real cap. Tests:
`test_node_table.cpp:296`, `test_node_view.cpp:221/290–295`.

### 7. PARSERS + BOUNDS

- **Advert app_data** (`core/mc_identity.cpp:94`): `app_len` clamped to `kMaxAdvertData` before the copy and the
  verify; `flags = app[0]` is safe because the length check guarantees ≥1 app byte; lat/lon only when 8 bytes are
  present; feat1/feat2 skipped only when present; the name is copied with `name_size-1` and always NUL-terminated.
  ASan: `test_mc` (junk lengths included) — suite totals below.
- **CLI answers** (`core/rptr_wire.cpp:221/237`): dest/src hash checked, `plain_len >= 6`, the text stops at the
  first NUL within `len-5`, clamped to `kMaxText` (buffer is `kMaxText+1`). ASan: `test_rptr` ASan stage.
- **Time-row path** (`main/mc_link.cpp:1450`): `hash_size` 1..3 and `bytes <= kMaxPath` before the copy; the row
  only resolves when `path_bytes >= hash_size` (`main.cpp:1178`); `pathHopLabel` refuses `len > 3`.
- **Message pop-up text:** the UTF-8 cut is fixed (item 7) by using `mc::wrapText`; `msgPopupOffer` copies 191
  bytes into a 192-byte slot with `snprintf`.
- **Node file load** (`core/node_store.cpp:90`): magic, version, record width, `count > kMaxNodes`, exact length
  (`len < need` truncated / `len > need` bad size), CRC over header+records, and `count > out.cap`; all before the
  first write to the table. ASan: `test_node_store` ASan stage.

### 8. MEMORY

`g_messages = 0x490C = 18,700 B` and `g_dms = 0x510C = 20,748 B` (`nm` on `build/tanmatsu/*.elf`) — **39,448 B**
of internal `.bss` for the rings, plus `ReadState` 32 B. After the build, `idf.py size`:
**DIRAM 297,196 B used, 275,172 B remain (51.92 % of 572,368)**; flash image 1,630,712 B, `application.bin`
**1,741,328 B** (0x1A9210; the 32 B over the first S516 build are the wake-path pop-up discard). The app now
logs that headroom once at boot
(`heap: … internal free, largest internal block …`) and logs each FatFs-writing task's low-water mark once, after
its first write (main task: radio.bin/nodes.bin; radio task: dm-*.log).

### 9. KNOWN FIX A — the deleted node's DM thread

The "leaves the chat list" part **could not be reproduced in the model**: `dmThreadSummaries()` already lists a
thread whose key is not in the contact table (`tests` pass on the old code for the "listed" check). What WAS wrong
at the model level: `dmSummaryForPeer()` returned `name = ""` when the last message carried no name and the
contact was gone. Fixed: the summary owns an 8-hex short-key fallback (`mc_store.h:276`, `mc_store.cpp:428`).
`sums[0]` is still named from its own message when it has one. No DM/card history is deleted by
`deleteEverywhere` — the confirm sentence stays true. Test: `test_store.cpp:266–289`.

### 10. KNOWN FIX B

`initNodeTable()` is called from `begin()`, `nodeStorageBegin()` and `restoreContacts()`. The allocations were
already idempotent; the banner now has a one-shot static, so the boot log prints `node table: 2000 rows, …` once
(`mc_link.cpp:189–209`).

### Armed tests (revert the fix → the FAIL line, then restored)

| fix | armed line | FAIL observed |
|---|---|---|
| item 2 CRC before trust | `test_node_store.cpp:170` | `FAIL test_node_store.cpp:170 om::mc::nodeFileCheck(buf, one_record) == Truncated (got 0, want 2)` |
| item 5 CLI second door | `test_dm.cpp:184` | `FAIL test_dm.cpp:184 meshcore::isRepeaterCliText(m.txt_type)` (and `:186` for CLI_COMMAND) |
| item 6 cap of the store | `test_node_table.cpp:296` | `FAIL test_node_table.cpp:296 addToRadio(..., radioCapFor(false)) == RadioFull (got 0, want 2)` |
| item 6 "Radio full" sentence | `test_node_view.cpp:221` | `FAIL test_node_view.cpp:221 "Radio full (32 / 32)…"` |
| item 9 short-key name | `test_store.cpp:289` | `FAIL test_store.cpp:289 "" != "88000000"` |

The device-glue fixes (1, 3, 4, 7, 8, 10) have no host test: they change ordering/syscalls in `main/`, which the
host suite does not build. They are marked as such above rather than given a test that would not fail.

### Honest limits of this run

- Nothing was installed, flashed or seen on glass: the live-device claims in this entry are from reading the code
  and the model-level tests, not from a running Tanmatsu.
- The 30 s archive cadence under a continuously chatty mesh is stated, not changed (it is a wear/data-loss
  trade-off; a 10-minute floor would widen the power-cut window).
- After a panic, manual LED mode is not recoverable in-process (§ 3).
- `~/Projects` and the servers were not needed; no SSH was made for this ticket.

---


**Claude's review of EST-472 (S516, 2026-11-20 19:3x):** read the whole code diff (21 files) against the call sites. Accepted: the Map-action copy under the lock, the CRC check before a temp file is trusted, flush/fsync/fclose checked before the old file goes, the radio file forced at exit (signature-gated), the wake-path discard, the cap of the store in use, the UTF-8-safe pop-up wrap (its ellipsis trim drops whole characters), the one-shot banner. **Two defects found in this round and fixed by Claude:** (1) `kTxtTypeCliCommand` was **2**, which is `TXT_TYPE_SIGNED_PLAIN` (a room server's message; `helpers/TxtDataHelpers.h:6-9`: PLAIN 0, CLI_DATA 1, SIGNED_PLAIN 2, CLI_COMMAND 3), and the test asserted 3 = SIGNED_PLAIN. Unused before, so harmless until item 5's new filter, which would have dropped every signed message as repeater traffic. Fixed (+ `kTxtTypeSignedPlain`), test armed: 3 FAIL lines on the old constant (`test_dm.cpp:187/190/191`). (2) Item 9's short-key name made `DmSummary::name` point into the summary's own `short_name`, but summaries are returned by value and copied (`out[n++] = dmSummaryForPeer(...)`), so the copy pointed into a dead temporary (the test passed on luck: the stack was not reused yet). Fixed with a copy constructor + assignment that re-point `name`; test armed (`test_store.cpp:299` `"XXXXXXXX" != "88000000"` without them). Suite exit 0, build exit 0 (1,741,392 B; the `unused variable 'order'` warning at `main.cpp:1997` was already in v65's build). Not seen on the device yet.
## 2026-11-19 · S515 (EST-470) RPTR part 2a: the safe settings

> Written by **DeepSeek** for ticket **EST-470** (re-filed from EST-458): fenced Paperclip run, no session start or
> end, nothing installed, flashed, pushed or committed. Starting point **v64 `28dc1b1`** (the tree Claude installed on
> the device on 2026-11-19 20:56). **Version: 65** (`core/settings.h` kAppVersion). `firmware/` was read only and is
> unchanged.
>
> **ITEMS DONE: 1, 2, 3, 4, 5, 6.** `sh test/host/run.sh` **exit 0**: the `rptr` suite **101 → 287 checks** (+186,
> run twice: `-O2` and the ASan/UBSan stage), the whole log's suite totals summed **32,712 → 33,084** (+372).
> `make build` **exit 0**, `application.bin` **0x1A8C70 = 1,739,888 B** (**+15,856 B** over v64's 1,724,032 B;
> 1,698 KiB, i.e. **53 % of the ticket's 3,200 KiB budget**, 17 % of the 2 MiB app partition still free). Previews:
> `shots/2026-11-19-rptr-settings.png`, `shots/2026-11-19-rptr-confirm.png`. **Nothing was installed, so no line here
> was ever on the air.**

### What it builds (2a only; radio/path hash/loop detect/advert timers/flood.max/regions/silent/free-form = 2b)

1. **The CLI path, ported line by line.** `core/rptr_wire.cpp::buildCli` is upstream
   `BaseChatMesh::sendCommandData` (`firmware/lib/meshcore/helpers/BaseChatMesh.cpp:492-516`): a **TXT_MSG** whose
   plaintext is `ts(4 LE) | flags | line`, `flags = (attempt & 3) | (TXT_TYPE_CLI_DATA << 2)` with
   `TXT_TYPE_CLI_DATA = 1` (`helpers/TxtDataHelpers.h:7`, so `0x04` at attempt 0), sealed as
   `dest_hash(1) | src_hash(1) | encryptThenMAC(secret, plaintext)` (`Mesh.cpp:490-509`), sent DIRECT down the route
   learned at login (`s_targetPath`, v55) or as a flood when there is none, which is `sendCommandData`'s
   `out_path_len == OUT_PATH_UNKNOWN` branch. The timestamp is v55's `uniqueTimestamp()` (epoch seconds, strictly
   increasing) because the repeater drops anything not newer:
   `sender_timestamp >= client->last_timestamp` (`reference/meshcore-repeater-v1.18.0/MyMesh.cpp:714`), and an EQUAL
   one is a RETRY that deliberately answers nothing (`:727-731`).
2. **The answer is a TXT_MSG of the same CLI type, and it goes to RPTR, not to the DMs.** The repeater builds it as
   `ts(4 LE) | (TXT_TYPE_CLI_DATA << 2) | text` (`MyMesh.cpp:743-757`). `rptrlink::onCliAnswer` is called from
   `mc_link.cpp::onPacket` **before the DM code**, at the top of the TXT_MSG branch: it tries the **login target's
   key** (a node usually NOT in the contact table, the same shape as v55's dropped login reply), and
   `rptr::parseCliReply` accepts **only** CLI_DATA - so a PLAIN text from that same repeater still reaches Chat,
   while a CLI answer returns before `dmPush`, which is why it raises **no new-message pop-up**
   (`core/msg_popup.h`) and **no Chat dot**, and is never dropped as "no contact opens it".
3. **READ BEFORE WRITE.** After a successful login (and on `r`) the link runs the seven reads in the order
   `rptr::setReadCmd()` fixes: `get name`, `get owner.info`, `get lat`, `get lon`, `get repeat`, `powersaving`,
   `get tx` - the exact `get` names from `CommonCLI::handleGetCmd` (`firmware/lib/meshcore/helpers/CommonCLI.cpp:701-790`)
   and `CommonRadioPrefs::handleCommand` (`:135-139`), never guessed. Every answer is keyed by the **line that
   asked** (`settingsApplyRead`), not by the row, so `get lat` and `get lon` can never be swapped; a row that has
   not been answered yet draws **"…"**. NOTE: `powersaving` is a TOP-LEVEL command (`CommonCLI.cpp:439-462`) - the
   L2's own `firmware/src/rptr_model.cpp:1146` sends `set powersaving on`, which would reach `handleSetCmd` and come
   back `unknown config: powersaving on`. This port sends `powersaving on|off`.
4. **SETTINGS in the status view**: ten rows (Name, Owner info, Position, Repeat, Power save, TX power, Advert local,
   Advert flood, Clock sync, Reboot), label left and the repeater's own value right; Enter edits (one shared text
   field for name/owner/position, toggles for on/off, `l/r` or `+`/`-` for TX power 1-22 dBm, one action per action
   row). **Every** change goes through the shared confirm sheet (EST-422/EST-467, Cancel focused) whose text NAMES
   the exact line and the value the row shows now (`confirmWriteText`); Reboot's is the ticket's own sentence.
   **Position is ONE row and TWO commands** (`set lat` then `set lon`, `CommonCLI.cpp:557-563`): the sheet names
   both, the link sends the first and releases the second only on its OK, so a refused half never leaves the
   repeater with a half-applied position (`settingsSend(..., const char* second)`). The
   answer ("OK" / the repeater's error text / "no answer") is drawn on the page and raised as the shared notice, and
   the changed row is read again - **a value only ever comes from a read**, so nothing is shown as done on our word.
   `clock sync` is ALSO sent by itself once per login, as OM does (`CommonCLI.cpp:211-221`; skipped while this
   device's own clock is before 2025-01-01, because it would only be refused).
5. **One request at a time**: `rptr::CliPending` plus `sendCliLocked()` refuse a second line while one is in flight;
   the deadline is the v55 floor (`requestDeadlineMs(8000, 0, 8000)`); a timeout says "no answer" and leaves every
   row as it was READ. `reboot` is the one line that can never answer (`CommonCLI.cpp:199-200` calls
   `board->reboot()`, which does not return), so its send is its answer and the page returns to the list. The
   password is never logged, drawn or returned by any new code path.
6. **Host tests** (`test/host/test_rptr.cpp`, +186 checks): the CLI frame byte for byte against the Python oracle
   (flood and direct), three reads, eleven answers, per-command parsing, the 23-byte name cap with a shared
   position, the TX clamp, the confirm wording, the ONE-line queue with the 32-bit wrap, and the routing rule
   (`route()` in the test: CLI answer → RPTR, PLAIN text from the same key → DM, our own frame → dropped). The parsers
   also run in the ASan/UBSan stage with the 4,000-round junk pass extended to `openCliAnswer`/`parseCliReply`.

### Arms (two, each run, each reverted)

* **A - a CLI parser that also takes a PLAIN text** (`rptr_wire.cpp`, `txt_type != kTxtTypeCliData && txt_type != 0`):
  `FAIL test/host/test_rptr.cpp:408  route(kAnsPlain) == Where::Dm` - **1 failure**, i.e. a DM would have been
  swallowed by RPTR.
* **B - the 23-byte cap dropped** (`rptr_model.cpp`, `max = kNameMax` always): two lines,
  `FAIL test/host/test_rptr.cpp:552  om::rptr::cliName(line, sizeof(line), "ABCDEFGHIJKLMNOPQRSTUVWX", true) == 0 (got 33, want 0)`
  and `:553  got "set name ABCDEFGHIJKLMNOPQRSTUVWX" want ""`.

### Where this came from (EST-458's saved partial patch)

Started **from** `../wip/EST-458-rptr-part2a-partial-2026-11-19.patch`, then re-read everything it touches at v64:
its `core/rptr_model.*` block (the labels, the line builders, the parsers, the confirm words, `CliPending`) is kept,
with one real change: the patch applied a `get` answer by ROW (`settingsApplyAnswer(Settings*, int row, ...)`), and
its Position case had to guess which half of "lat, lon" an answer was ("the half that has not arrived yet is the
one filled here"). Answers are keyed by the **asked line** now. The patch contained nothing else - no wire frame, no
link, no page, no keys, no tests, no previews; those are new here.

### Files

`core/rptr_model.h/.cpp` (the settings model, the seven reads, the line builders, the answer parsers, the confirm
words, `CliPending`), `core/rptr_wire.h/.cpp` (`buildCli`, `openCliAnswer`, `parseCliReply`, `kTypeTxtMsg`),
`main/rptr_link.h/.cpp` (one-line queue, the read ladder, the timeout, `onCliAnswer`), `main/mc_link.cpp` (the RPTR
hook before the DM code), `main/rptr_page.h/.cpp` (the SETTINGS section, the field, the answer line, the hints),
`main/main.cpp` (the section's keys, the confirm wiring, the login hook, the notice),
`test/host/test_rptr.cpp` + `test/host/run.sh` (links `mc_dm.cpp` into the RPTR suite),
`tools/preview_tanmatsu.py`, `core/settings.h` (v65).

### What could NOT be checked

Everything on the air: no login, no read, no change, no reboot was sent (the next install needs Filip at the
device). The SI-LJ-Trnovo II run is Claude's with Filip, starting with Clock sync and Owner info. Two things to
watch on the first real session: whether that repeater's firmware answers `get tx` at all (a repeater older than
1.18 may answer `??: tx`, which shows as an error line and leaves the row "…"), and whether `powersaving on` is
accepted by the version actually deployed.


**Claude's review of EST-470 (S515, 2026-11-19 21:2x):** DeepSeek's fence could write only `paperclip-pilot/sweep/` in this run, so the work came as `sweep/est470-EST-470.patch` (15 files, +1991/−36) + 2 PNGs; Claude dry-ran it (`git apply --check`, clean on `28dc1b1`), key-scanned it (0 hits), checked no new log line touches the repeater password, applied it, and re-ran the suite here (exit 0; `test_rptr` 287 checks in both O2 and ASan) and the build (exit 0, 1,739,888 B). Read: `onCliAnswer` sits at the top of the TXT_MSG branch before the DM code and returns at once without a login (`s_haveTarget && loggedIn()`, then the source hash). Accepted: DeepSeek's `powersaving on|off` as a top-level command (`CommonCLI.cpp:439-462`), which also means **the L2's `set powersaving on` (`firmware/src/rptr_model.cpp:1146`) is a real L2 bug** (the repeater answers `unknown config`); not fixed on the L2 tonight. Not on the air yet: watch `get tx` and `powersaving` against SI-LJ-Trnovo II.
## 2026-11-19 · S515 (EST-469) message selection + menu

> Written by **DeepSeek** for ticket **EST-469**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v63 `e240663`** (tree clean, before work, committed by Claude).
> **Version: 64** (`core/settings.h` kAppVersion). `firmware/` was read only and is unchanged.
>
> **Filip's words (quoted, S515):** *"when 'scrolling' up in the chat, add a selection box, and pressing enter opens
> up a 'pop-up menu' for reply, map, DM, info, etc.. pressing Esc closes the selection box and enables to type
> again"*.
>
> **ITEMS DONE: 1, 2, 3, 4, 5.** `sh test/host/run.sh` **exit 0**: **33,431 counted checks** (+138 over v63's
> 33,293: the new `chat-select` suite is **125**, `test_layout` 62 → **75**). `make build` **exit 0**,
> `application.bin` **0x1A5680 = 1,724,032 B** (**+7,504 B** over v63's 1,716,528 B; 18 % of the app partition
> free). Three new previews (`shots/2026-11-19-chat-select.png`, `-chat-msg-menu.png`, `-chat-msg-info.png`);
> every preview re-rendered. Nothing is installed, flashed, pushed or committed.

### 1. The selection box (item 1)

- **Up from the composer now ENTERS the selection box on the newest message** instead of scrolling, and Down with
  nothing selected does nothing. **Scrolling without selecting no longer exists** — that is the ticket's own
  change, and this entry says it because the old hint (`up/dn scroll`, S503) is gone: `up/dn` now means "move the
  box". The draft is never touched by a move.
- The frame is the Nodes list's focus look — `kSelBg` fill + the 2 px gold `kAcc` ring — around the **whole
  message**: its head line (time + who) and every body line, drawn UNDER the text so no glyph is covered.
- Up/Down move it message by message; **Up at the oldest stops**, **Down past the newest leaves the box** (the
  ticket's second way out), **Esc leaves the box and the composer takes keys again**, draft intact. A typed key
  while the box is up is swallowed: `handled = true` at the top of the branch, so nothing reaches `composer[]`.
- **The window follows the box** and `chat_layout` decides it: `chatSelScroll(index, count)` = `count-1-index`
  (the selected message is the bottom of the window), and the new **`chatWindowAt`** is the same `chatWindow`
  with the scroll clamped only to `0..count-1`. It is needed because `chatWindow`'s `max_scroll` stops at the
  topmost FULL window, so the oldest messages would otherwise be unreachable — the box must reach all of them.
- **The box is kept by SEQ, not by index**: a message that arrives while it is up does not move the frame (the
  window grows above it). If the ring evicts the selected message (64 messages) the box closes by itself.
- State cleared wherever the chat changes under it: Esc out of the chat, the own CHAT symbol key (`onTop`), a
  channel/DM card opened from the list, and the node menu's DM (`openDmThread`).

### 2. Enter = the message menu (item 2)

- A sheet in the **node menu's house style** — dither scrim, panel, gold 2 px outline, two-line head, 42 px rows,
  `kSelBg` + gold ring on the focused row — drawn by ONE function (`drawChatSheet` in `main/chat_page.cpp`) that
  the Info sheet also uses, so the app keeps one menu look.
- Rows per message kind, from `core/chat_select.h::chatMsgRows`:
  - **incoming channel**: `Reply`, `DM <sender>`, `Map`, `Node`, `Info`;
  - **incoming DM**: `Reply`, `Map`, `Node`, `Info` (no DM row: this IS the DM);
  - **our own line**: `Info` only.
- `Reply` puts **`@<sender> `** in front of the draft on a channel. The form and its source:
  **`firmware/src/main.cpp:1175`** — `snprintf(wire, sizeof(wire), "@%s %s", g_replyName, g_kbText);` — with
  `firmware/src/ui/message_sheet.h:26` naming the same thing (*"the `@Name text` draft, as REPLY did"*) and
  `firmware/src/main.cpp:1332` using the identical `"@%s "` tag for a mention. A DM's reply adds **nothing**: the
  thread already says who it is for (the L2's own rule, `firmware/src/main.cpp:1145-1147`).
- **A channel message carries only the sender's NAME**, resolved by `chatNodeByName`: only full-key contacts
  count (a prefix-only row is not a node), and only **exactly one** match names a node. Zero → the row is dim
  with **`unknown node`**; two or more → dim with **`2 nodes share this name`** (`N nodes` for N > 2) and
  **Enter does nothing at all** — the same "never guess" rule `pathHopLabel` follows for a path hash. A DM thread
  resolves by the peer's KEY (`contactsFind`), since it has one.
- `Map` is dim with **`no position`** when the resolved node has none (`positionDrawable`); otherwise it takes the
  **node menu's own path** (`centreOnNode` at z15, the `map_highlight_pub` gold ring, `goPage(Map)`), so the map
  cannot tell which page asked.
- `Node` is the Nodes page with the box on that node (`nodeMenuFocusFollow`); if the node is outside the current
  filter/search the filter goes to **All** and the query is dropped first, otherwise the box would land on
  somebody else.
- `DM` opens the thread through **`openDmThread`**, the same function the node menu's DM row uses (created empty
  if it has no messages).
- `Info` opens a sheet: **channel or DM and the sender are its head**, then `Sent` (the frame's timestamp),
  `Received` (our clock, local), `Route` (`flood` / `scoped` / `direct`), `Hops`, **one row per hop with its hash
  and its name only where exactly one node matches** (`pathHopLabel`), `Bytes`, `Heard` / `Repeated` N×.
  **No RSSI and no SNR** (docs/DEVICE.md § 5b).
- **A restored message shows only what the record carries**: `Received`, `Route`, `Bytes` and the copy count say
  **`not kept after a restart`**. This needed ONE new store field: `Message::restored` / `DmMessage::restored`,
  set by `core/mc_history.cpp`'s two restore paths (the alternative — inferring it from `frame_len == 0` — would
  guess). Hops and the path ARE stored (v58), so they are shown when `hops_known`.
- Esc closes the Info sheet back to the box; Esc closes the menu back to the box; Esc closes the box and hands
  the keys back (S502's layer-up rule, unchanged).

### 3. Pure logic in core + host tests (item 3)

- `core/chat_select.h` / `.cpp` (new): `chatSelEnter`, `chatSelIndexForSeq`, `chatSelMove`, `chatSelScroll`,
  `chatMsgRows`, `chatNodeByName`, `chatNodeNote`, `chatReplyPrefix`, `chatInfoHead`, `chatInfoRows`.
  `core/chat_layout.*`: `chatWindowAt`. Registered in `core/CMakeLists.txt`.
- `test/host/test_chat_select.cpp` (new, **125 checks**) pins: enter at the newest; Up at the oldest stops; Down
  past the newest LEAVES; the frame stays on the same seq when a message arrives; the rows per kind and the dim
  reasons; a name shared by two nodes resolves to nobody; a prefix-only row never resolves; the reply prefix
  (including the empty sender and a short buffer); the Info rows for a live channel message, a DM, our own line,
  and a restored record; and that a path too long for the sheet ends in `+N more` with the TRUE count (a 64-hop
  path folds, it is not silently cut).
- `test/host/test_layout.cpp` +13: `chatWindowAt` reaches the oldest message, agrees with `chatWindow` where the
  clamp does not bite, clamps at both ends, and answers the empty case.
- **ARMED, two (FAIL lines pasted, then restored — `cmp` says the file is identical again):**
  1. `chatSelMove` clamping instead of leaving at the newest:
     `FAIL test_chat_select.cpp:89  m.active == false (got 1, want 0)` — **121 checks, 1 failure(s)**.
  2. `chatNodeByName` taking the first of two matches:
     `FAIL test_chat_select.cpp:234  m.matches == 2 (got 1, want 2)` — **123 checks, 1 failure(s)**.

### 4. The hint bar (item 5)

- In selection mode: **`up/dn move`, `Enter menu`, `esc type`** — three chips, and the preview renders the same
  arithmetic `chrome.cpp` uses: they end well short of the five symbol keys, nothing is dropped.
- The menu shows `up/dn move`, `Enter choose`, `esc close`; the Info sheet `esc close`; the plain open chat lost
  `up/dn scroll` and is now `Enter send`, `esc back`, `type write`, `, settings`.

### 5. Files

- **New:** `core/chat_select.h`, `core/chat_select.cpp`, `test/host/test_chat_select.cpp`,
  `shots/2026-11-19-chat-select.png`, `shots/2026-11-19-chat-msg-menu.png`,
  `shots/2026-11-19-chat-msg-info.png`.
- **Changed:** `core/CMakeLists.txt`, `core/chat_layout.h`, `core/chat_layout.cpp`, `core/mc_store.h` (the
  `restored` flag on both message structs), `core/mc_history.cpp` (sets it), `core/settings.h` (v64),
  `main/chat_page.h`, `main/chat_page.cpp`, `main/main.cpp`, `test/host/run.sh`, `test/host/test_layout.cpp`,
  `tools/preview_tanmatsu.py`, this entry.

### 6. Assumptions, and what was NOT checked

- **The reply prefix is `@Name ` with one space**, from the cited firmware line. The ticket wrote it
  `"@[<sender>] "`; I read the brackets as the placeholder for the name, not as characters to type — the L2 writes
  none, and a `[` would change what every reply-looking bot on the channel sees.
- **`Heard N×` keeps the EST-462 convention** (`copies` printed as-is, floored at 1) so the Info sheet and the
  time row cannot disagree; note that `mc_store` documents `copies` as the count of DUPLICATE frames, so the
  short line and the sheet are both one lower than "frames heard" for a repeated message. **Not changed here**
  (it would move an EST-462 test and its preview) — flagged for Filip, not fixed.
- **A `Reply` whose prefix would not fit** (draft longer than 93 of the composer's 120 bytes) leaves the draft
  alone and closes the box. The draft is never truncated; the box just does not add the mention. Not surfaced.
- **NOT checked:** anything on the glass (no install); whether the frame's 4 px side margin looks right on the
  real panel; whether a long path's `+N more` row is the best fold (it is honest, it is not pretty); the Map
  action's real z15 pack on the card; the DM/Node jumps across pages live.
- `make build` still prints the pre-existing `unused variable 'order'` warning in `main/main.cpp`
  (`drawNodesPage`, there in the v63 baseline); no new warnings.


**Claude's review of EST-469 (S515, 2026-11-19 20:5x):** DeepSeek's flag 2 was a real defect from v58, fixed: `copies` counts the EXTRA copies (a message arrives with 0; `mc_history.cpp:176`), but the time row printed it as "heard N×" and only from 2, so a message heard twice showed nothing and one heard three times said "heard 2×"; and our own message needed two echoes before "repeated" appeared. Now: incoming heard = copies + 1, shown from 2 (copies ≥ 1); own repeated = echoes, shown from 1. Four `test_chat_meta.cpp` expectations and one `test_chat_select.cpp` row moved with it (the v58 tests had pinned the one-too-low model). Flag 1 accepted: the reply prefix is the L2's `@Name ` (`firmware/src/main.cpp:1175`), not `@[Name]`, for one convention across Filip's two devices; to be confirmed by Filip. Flag 3 (a Reply that would not fit leaves the draft as it is) accepted. Previews checked (menu with a dim Map row).
## 2026-11-19 · S515 (EST-468) message LED + storage wording

> Written by **DeepSeek** for ticket **EST-468**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v62 `9ee566c`** (tree clean, before work). **Version: 63**
> (`core/settings.h` kAppVersion). `firmware/` was read only and is unchanged.
>
> **Filip's words (quoted, S515):** *"when node is saved on the radio, is it also on the SD, or is it moved to the
> radio? It is not clear from the UI.. I think it should be on both"* / *"when the Tanmatsu is 'locked', screen off,
> all LEDs would be off, but the new MSG one would blink, like 3-continuing blinks, every 20 secs"*.
>
> **ITEMS DONE: 1, 2, 3.** `sh test/host/run.sh` **exit 0**: **33,293 counted checks** (the sum of the run's own
> pass lines, plus the DM-guard suite's 169 and the emoji suite's own pass line; the new `message-led` suite is
> **99 checks**, `test_node_view` is now **746**). `make build` **exit 0**, `application.bin`
> **0x1A3130 = 1,716,528 B** (18 % of the app partition free; **+4,080 B** over v62's 1,712,448 B). Previews
> re-rendered, including `shots/2026-11-19-nodes-sd-wording.png` and
> `shots/2026-11-19-settings-message-led.png`. Nothing is installed, flashed, pushed or committed.

### 1. Node storage: the radio rows are ALSO on the SD (item 1)

It was already true in the store (one table, one row per node; `/sdcard/om-lite/nodes.bin` holds every row and
`/int/om-lite/radio.bin` holds the `in_radio` marks). The UI now says it:

- `core/node_menu.h`: the in-radio store row is **`Remove from radio (stays on SD)`** (was `Move to SD`), and its
  notice is **`Removed from the radio, kept on the SD card`** (`nodeMovedToArchiveNotice()` now returns the same
  sentence as Settings' per-node action). `Add to radio` is unchanged.
- `core/node_view.h`: the head is **`Radio N / 300 · SD M`** where **M = EVERY saved node, radio rows included**;
  without a card it is **`Radio N / 300 · SD -`**. The third chip is **`SD only`** and still filters to the rows
  NOT in the radio. Settings' info row follows the same rule (`Radio … · SD … · SD ok` / `… SD - … no SD card`).
- `main/mc_link.cpp`: `nodeStoreCounts(&radio, &sd)` now returns the whole table as `sd` (it used to return only
  the non-radio rows). `main/main.cpp` uses it for the Settings info row and fills the Nodes page's head with
  `contacts().count`.
- Settings › NODES': `Clear radio (SD keeps them)` is unchanged; its confirm now says
  `Remove all N nodes from the radio? They stay on the SD card.` and its notice stays
  `Radio cleared; the SD card keeps every node`. The per-node row is `Remove from radio` (EST-467).
- Host test: the head's SD number includes the radio rows (fixture 3 radio / 5 total → `Radio 3 / 300 · SD 5`)
  while the `SD only` view holds only the 2 rows NOT in the radio. The `R` tag is untouched and still marks
  radio rows.
- **Fit check (real `sans_hint11` advances, 800 px):** the chip row is `All` + `Radio` + `SD only`; the head's
  right text is measured and reserved exactly as before, so the wider `SD only` chip still ends well short of it
  (the preview shows no overlap). The hint bar is unchanged and still drops `f fav` / `a advert` at the
  documented break (EST-467); no chip starts off-screen or under the page margin.

### 2. Message LED (item 2)

- **Settings row: General › LED** (`Message LED`): **Off / 1 / 2 / 3 / 4 / 5 / 6, default Off**, NVS
  **`omled`/`msgled`** (new namespace, one byte). While the row is being changed the task blinks the chosen LED
  three times AT ONCE and then hands the LEDs back.
- **Rule**: `core/message_led_rule.h` (new, pure, time in / LED frame out). Active only while the screen is OFF
  (`core/screen_power.h` state) AND the hint bar's `unreadTotals` sum > 0 AND a LED is chosen. Then manual mode,
  all 6 LEDs dark, the chosen one gives **3 blinks of 120 ms on / 180 ms off** at once and then **every 20 s**.
  **Brightness 15 %**, **soft cyan** (`0,255,255` through the coprocessor's 15 % limit; a white would read the
  same but cyan is the choice, said here).
- **Stop**: all pixels dark, then `bsp_led_set_mode(true)` so the system's Wi-Fi/power indicators return; the
  previous automatic brightness is restored too. It stops the moment the screen goes on, the unread drops to 0,
  or the choice goes to Off. `leaveToLauncher()` calls `om::led::restoreAutomatic()` FIRST, before the
  backlight and before the handover.
- **Start**: `om::led::begin()` after `bsp_device_initialize` reads the NVS choice and calls
  `bsp_led_set_mode(true)`, so a previous run can never leave the system indicators dark.
- **Task**: one low-priority `om-led` task (priority 1, 3 KB, 25 ms period) drives the state machine and makes
  every `bsp_led_*` call from task context (the calls are I2C through the coprocessor). The main loop only
  publishes `(screen_off, unread)` and the power button publishes its edge, so the blink timing does not depend
  on the 250 ms poll and never blocks it. A frame is applied only when `changed` is true: one on-write and one
  off-write per blink, **no I2C traffic between the 20 s bursts**. Logs `[led] blink on (LED n)` / `[led] off`
  once per state change, not per blink.
- Which physical LED is "the message one" is still undocumented, so the row is the way Filip picks it.

### 3. Tests, ARM, build, previews (item 3)

- New host suite `test/host/test_message_led.cpp` (99 checks): the Off/1..6 setting and cycle, the three-way
  gate, the exact burst schedule (0/120/300/420/600/720/900 ms and the 20 s repeat), no-change in the dark gap,
  every stop path, choice-change restart, the Settings preview, poll-period independence, and a 32-bit wrap.
  `test_node_view` grew to 746 checks with the M-includes-radio / SD-only-excludes-radio assertions.
- **ARMED TWICE** (real FAIL lines, then reverted, no MUTANT left):
  - `SD only` view counted every row: `FAIL test_node_view.cpp:89 … == 2 (got 5, want 2)` and
    `FAIL test_node_view.cpp:306 …`.
  - the LED rule ignored the screen state: `FAIL test_message_led.cpp:75  !rule.step(0, in(false, 3, 2)).manual`
    and `FAIL test_message_led.cpp:122  !f.manual`.
- Previews: `shots/2026-11-19-nodes-sd-wording.png` (`Radio 3 / 300 · SD 7`, chip `SD only`, R tags on the
  radio rows, detail `radio + SD`) and `shots/2026-11-19-settings-message-led.png` (General › LED, `Message LED`
  focused, value `3`). The preview tool's Nodes head now uses the total for SD and its OM Lite fixture says
  `Remove from radio`.
- Files touched: `core/message_led_rule.h` (new), `core/node_menu.h`, `core/node_view.h`, `core/settings.h`,
  `main/message_led.h` (new), `main/message_led.cpp` (new), `main/CMakeLists.txt`, `main/main.cpp`,
  `main/mc_link.h`, `main/mc_link.cpp`, `main/nodes_page.h`, `main/nodes_page.cpp`, `test/host/run.sh`,
  `test/host/test_message_led.cpp` (new), `test/host/test_node_menu.cpp`, `test/host/test_node_view.cpp`,
  `tools/preview_tanmatsu.py`, this entry, and the two new `shots/*.png`.
- **NOT checked:** anything on the glass (no install); which LED index is the message LED; whether 15 % is
  visible in a lit room; the task's real I2C timing on the device.

## 2026-11-19 · S515 (EST-467) · test round 1: names, Enter, Cancel, the pop-up

> Written by **DeepSeek** for ticket **EST-467**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v61 `90a95cb`** (tree clean, before work). **Version: 62**
> (`core/settings.h` kAppVersion). `firmware/` was read only and is unchanged.
>
> **Filip's words (quoted, S515):** *"Rename the 'Archive' to 'Device' or 'SD'.. Also rename 'ret' ... to
> Enter.."* / *"Deleting works, but make a default selection 'Cancel' as an extra safety.."* / *"'Remov a
> contact' should specify it is removed from the radio."* / *"a new MSG should appear as a pop-up with info on
> what CH and a node name and a msg displayed. on any page, any button dismiss it and doesnt do anything else
> on that page."*
>
> **ITEMS DONE: 1, 2, 3, 4, 5, 6.** `sh test/host/run.sh` **exit 0** (**32,741 counted checks** across the
> suites, plus the DM-guard suite's 169 and the emoji suite's own pass line; the new pop-up test is 51,
> `test_node_view` is now 742). `make build` **exit 0**, `application.bin` **0x1A2140 = 1,712,448 B**
> (18 % of the app partition free; **+3,056 B** over v61's 1,709,392 B). Previews re-rendered, including
> `shots/2026-11-19-popup-msg.png`. Nothing is installed, flashed, pushed or committed; `firmware/` was read
> only and is unchanged.

### 1. "Archive" → "SD" (item 1)

User-facing strings changed (code identifiers and ESP_LOG lines stay `archive`):

- `core/node_view.h`: the All / Radio / **SD** view chip (`nodeViewText`), the page head
  `Radio 3 / 300 · SD 412`, and Settings' info row `Radio … · SD … · SD ok` (`nodeStoreInfoText`).
- `core/node_menu.h`: `Move to SD`, `Radio full (300 / 300): move a node to the SD card first`,
  `Moved to the SD card`, and the Delete sentence `Delete X from the radio and the SD card? Its DM history
  stays on the card.`
- `main/main.cpp`: Settings rows `Clear radio (SD keeps them)` and the counts row, the Clear-radio confirm
  `Move all N radio nodes to the SD card? The SD card keeps them all.`, the notices `Radio cleared; the SD
  card keeps every node`, `Already on the SD card`, `Deleted from the radio and the SD card; its DM history
  stays`.
- `tools/preview_tanmatsu.py`: the Nodes view chips and every preview caption that said Archive.
- **"No SD card" stays** (Filip's line).

### 2. "ret" → "Enter" (item 2)

Every hint bar's key cap now reads **Enter**: Launcher, Nodes list, node menu, Chat list, Chat open, RPTR
(scan and status), Settings root, Settings sub-page, Settings volume row, and the confirm sheet's Apply cap.
The cap is no longer a fixed 30 px: it is `max(30, textWidth("Enter") + 10)` in `drawHintBar`, so the word
fits; the bar's "no room" rule still stops rather than letting a chip run under the symbol cluster. The bars
were re-rendered and checked at 800 px. The preview tool's `hint_bar` was rewritten to mirror
`drawHintBar`'s arithmetic EXACTLY (one sans 11 face, `max(30, key+10)`, the 4/8/10 spacing, the 22/18
right-cluster gaps), because the old preview used a mono face and narrower gaps and therefore claimed room
the glass does not have. Measured with the real `sans_hint11.bin` advances, the left cluster has 371 px
before the symbol keys; a 30 px fixed cap plus the word "Enter" cannot keep five or six hints there, so the
bars that were silently losing a CRITICAL key were re-ordered, not shrunk below the edge:

| Bar | Hints now shown at 800 px | Not shown (the break) |
|---|---|---|
| Launcher | `, settings`, `l/r select`, `Enter open`, `esc quit` | `s stop` (the detail panel still draws the Stop button with its `s` cap) |
| Nodes list | `<> view`, `Enter menu`, `/ search`, `esc back` | `f fav`, `a advert` |
| Nodes search | `type search`, `dn list`, `esc clear/back` | — |
| Node menu | `up/dn move`, `Enter choose`, `esc close` | — |
| Chat list | `Enter open`, `esc back`, `up/dn move`, `l/r column` | `, settings` |
| Chat open | `Enter send`, `esc back`, `type write`, `up/dn scroll` | `, settings` |
| Map | `, settings`, `dpad pan`, `+/- zoom`, `esc back` | — |
| RPTR list | `up/dn move`, `Enter log in`, `s scan`, `esc back` | — |
| RPTR login / status | `up/dn move`, `Enter send` / `r refresh`, `esc back/log out` | — |
| Settings root | `Enter open/change`, `esc back`, `l/r tab`, `up/dn row` | `, settings` |
| Settings sub-page | `, settings`, `type write`, `Enter apply`, `esc back` | — |
| Settings volume | `, settings`, `+/- volume`, `Enter change`, `esc back` | — |
| Confirm sheet | Cancel `esc` + Apply `Enter` (no left cluster) | — |

In every one of the 32 re-rendered PNGs an automated check found **no ink in the left 17 px of the hint
bar**, i.e. no chip starts off-screen or under the page margin. The `,` settings key stays visible on the
Launcher bar; the page-specific critical keys (Enter and Esc) were moved to the front so they can never be
the ones the break drops.

### 3. Cancel first on every destructive confirm (item 3)

`core/settings.h` `SettingsFrame` now carries `confirm_cancel` (default **true**). The shared sheet opens with
**Cancel focused**; **Enter = Cancel**; Left/Right move the focus to Apply and Enter there applies; Esc always
cancels. The focus ring is drawn on the focused button. This covers every sheet that uses the one shared
confirm: Delete everywhere, Clear radio, Remove channel, Remove from radio, Clear all contacts, node name,
forget RPTR passwords, show key. Both shell mappings (Settings and the Nodes page) now pass Left/Right into
`settingsKey`; without that, the focus could open on Cancel but never be MOVED to Apply, which would have made
every destructive action impossible. Host test: `test_confirm_sheet` drives open → Enter = Cancel (nothing
applied), then Right + Enter = Apply, then Left twice back to Cancel.

### 4. Settings › NODES: "Remove from radio" (item 4)

**What it did today:** the Settings › NODES row was **"Remove a contact"**, and its confirm ran
`contactsRemove()`, i.e. it deleted the whole contact row from RAM/radio.bin/nodes.bin — the node did NOT
stay on the SD card.

**Now:** the row and the sub-page title are **"Remove from radio"**; the confirm says
`Remove X from the radio? It stays on the SD card.` and Apply calls `nodeRemoveFromRadio()` (the
EST-463/465 action), so the row stays in the table with `in_radio = false`; the notice is
`Removed from the radio, kept on the SD card`. The node is NOT deleted. Host test covers the action choice.

### 5. New-message pop-up (item 5)

- `core/msg_popup.h` (new, pure, host-tested): `MsgPopup` decides show / replace / `+N more` / dismiss /
  swallow / 10 s timeout / the same-chat rule / own-message refusal / screen-off refusal. Rules are in the
  header.
- `main/chrome.{h,cpp}`: one slot, published by the radio task and read by the main task under a critical
  section exactly like `noticeSet`; `drawHintBar` draws it (so every page gets it) and the pop-up wins over
  the one notice while it is up. Header line `#<channel> · <sender>` (or `DM · <sender>`), then the text
  wrapped to at most 3 lines with an ellipsis, then `+N more`.
- `main/mc_link.cpp`: an incoming channel text or DM publishes the pop-up (our own echo and duplicates are
  already dropped by the seen ring).
- `main/main.cpp`: the main loop applies the screen-off and same-chat rules and the timeout; **any key**
  dismisses it and is **swallowed** (checked before every page/symbol-key branch). Screen off = no pop-up,
  no queue for later.
- Host test: `test/host/test_msg_popup.cpp`.

### 6. Tests, build, previews (item 6)

- `sh test/host/run.sh` **exit 0**: **32,741 counted checks**, 0 failures (plus DM guard's 169-check pass
  line and the emoji suite's own pass line, both uncounted by the summariser). New/changed suites:
  `test_msg_popup` **51/0** (show/replace/`+N more`, key swallowed, exact 10 s + wrap, same-chat, own,
  screen-off, counter cap), `test_node_view` **742/0** (the new "Remove from radio" test asserts the row
  stays, the mark drops, the action is reversible, and the notice's exact words), `test_settings` **183/0**
  (open-on-Cancel, Enter = Cancel, Right+Enter = Apply, left toggles back).
- **ARMED TWICE** (real FAIL lines, then reverted, no MUTANT left):
  - settings opens on Apply: `FAIL test_settings.cpp:573  f.confirm_cancel`
  - popup key not swallowed: `FAIL test_msg_popup.cpp:74  p.key()`
- `make build` **exit 0**, `application.bin` **0x1A2140 = 1,712,448 B** (18 % free, **+3,056 B** over v61).
- Previews: all **32** host renders re-run (`python3 tools/preview_tanmatsu.py`, exit 0), plus the new
  **`shots/2026-11-19-popup-msg.png`** (a channel message over the Nodes page: `#Test Channel · TEST-A1`,
  three wrapped lines with `...`, `+1 more`) and the re-rendered confirm sheet (Cancel focused, gold ring).
- Files touched: `core/msg_popup.h` (new), `core/node_menu.h`, `core/node_view.h`, `core/settings.h`,
  `main/chrome.{h,cpp}`, `main/main.cpp`, `main/mc_link.cpp`, `main/{launcher,nodes,chat,rptr,settings}_page.cpp`,
  `main/settings_page.h`, `test/host/test_msg_popup.cpp` (new), `test/host/test_{settings,node_view,node_menu,
  node_store}.cpp`, `test/host/run.sh`, `tools/preview_tanmatsu.py`, `docs/PROGRESS.md`, and the 32 `shots/*.png`
  (the hint bar changed in all of them). `main/fonts/*` untouched.
- **Not visible from this sandbox / not done:** nothing installed or flashed; no on-glass check of the pop-up
  timing or the real key codes (the host drives the decisions, the panel is not in this run). The device font
  was measured from `main/fonts/sans_hint11.bin`, not from a live screen.

---

## 2026-11-19 · S515 (EST-466) · nodes on the map, names from z13, and the node menu's Map item

> Written by **DeepSeek** for ticket **EST-466**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v60 `1d553cd`** (tree clean). **Version: 61**
> (`core/settings.h` kAppVersion). `firmware/` was read only and is unchanged.
>
> **ITEMS DONE: 1, 2, 3, 4, 5, 6.** `sh test/host/run.sh` **exit 0** (33,092 checks reported across the suites,
> the new one 172), `make build` **exit 0**, `application.bin` **0x1A1550 = 1,709,392 B** (18 % of the app
> partition free; **+6,304 B** over v60's 1,703,088 B). Two previews. Nothing is installed.

### 1. Markers (item 1)

Positions were already stored (EST-461's `has_position` / `lat_e6` / `lon_e6`, EST-463's one table), so this
ticket only draws them. **`core/map_nodes.h` / `.cpp`** are new and pure:

- `positionDrawable()` — the refusal rule: no flag, lat outside ±90, lon outside ±180, **and (0,0)** (the shape of a
  wiped field; a node would sit in the Gulf of Guinea for it). A zero on ONE axis is a real place and is NOT refused.
- `projectForView()` — lat/lon → **view-local pixel**, using the pack's OWN `worldPxXForLon` / `worldPxYForLat`
  (core/map_pack.h), not a second copy of the Web-Mercator formula. Culling is half-open: a point on the left/top
  edge is in, one on the right/bottom edge belongs to the next view.
- The page (`main/map_page.cpp::drawMapNodes`) draws, for every such node: a **5 px near-black disc** (the 1 px
  outline) under a **4 px disc** (~8 px) in **`ui::nodeTypeColour()`** — the Nodes list's own switch, **moved** out
  of nodes_page.cpp's anonymous namespace into `main/nodes_page.h` so both pages share one home — plus a **1 px gold
  ring** for a favourite. Drawn AFTER the tile loop, so a marker lands on a decoded tile, on a stand-in and on the
  void alike; the tile path is untouched (no new call inside the blit).

### 2. Labels from z13 (item 2)

`collectLabelCandidates()` ranks the nodes **in view** (favourite first, then most recently heard, then the lower
row index) and keeps the best 32; `placeLabels()` offers each plate (7 px right of the marker, 15 px tall, 4 px of
air each side) and **SKIPS** one that overlaps a plate already placed or that would leave the view. Markers are
never skipped. The text is clipped to **16 characters** UTF-8-safely with an ellipsis (`mapLabelText`).

### 3. The node menu's Map item (item 3)

`core/node_menu.h`: `NodeAction::Map`, `NodeMenuContext::hasPosition`, `kMaxNodeMenuRows` 7 → **8**, and a `dim`
flag per row. The Map row sits **after Trace, before the store row**, for every node type (a repeater or a sensor can
carry a position too). **Without a position** the row is **dim** with the value **"no position"** and Enter only
shows the notice **"No position from this node yet"**. With one, Enter calls `centreOnNode()` at **z15**, which
clamps to the map's own zooms **9..17** — z15 is inside them and lives in the **`slo` pack (z9-15)** — sets the
view, copies the node's KEY into `map_highlight_pub` and goes to the Map page. The highlighted marker carries a
**2 px gold ring at r10** until the user **pans or zooms** (both clear it in main.cpp).

### 4. Cost (item 4)

ONE line per compose that drew markers, rate limited to **3 s** (and immediately on the first draw after a view with
none): `map: N nodes in view, M labels, X us`, timed with `esp_timer_get_time` around the marker+label pass only.
**Measured on the host** (test/host/test_map_nodes.cpp, -O2, best of three, 2,000-row table packed into one screen
so nearly every row is in view): **300 rows → 56-157 us (223 markers, 9 labels); 2,000 rows → 322-823 us (1,450
markers, 16 labels)** — the spread is TestBox's own scheduling noise over six runs. The budget (2 ms for 300) is
met with 13x of headroom at the worst reading, and the test FAILS if the best-of-three passes 2 ms.

### 5. Host tests (item 5)

`test/host/test_map_nodes.cpp`, **172 checks**: the tile-corner identity at **every zoom 9..17** (the node's pixel
IS `worldPx − tileCorner`, i.e. 0..2 away because of the micro-degree rounding), the centre, all four quadrants with
lon moving only x and lat only y, the four view edges, the refused positions, the label text (16 chars, UTF-8, blank
names), the collision (two plates → one label, **the favourite wins**), the candidate cap keeping the favourites and
the most recent, the Map row with/without a position (and on a keyless repeater), the centre-on-node view + its zoom
clamp + its world-edge clamp, and the timing above.

### 6. Previews (item 6)

`shots/2026-11-19-map-nodes.png` (z12: six markers, one favourite ring, no labels) and
`shots/2026-11-19-map-nodes-z14.png` (z14: names, TEST-C3's plate SKIPPED because it collides with TEST-B2's while
its marker stays, the favourite ring and the menu's highlight ring both on TEST-A1). Both rendered by
`tools/preview_tanmatsu.py` (`map_nodes_z12`, `map_nodes_z14`), aimed at the real page geometry (800x386 area under
a 44 px head, four hint chips).

### Files

New: `core/map_nodes.h`, `core/map_nodes.cpp`, `test/host/test_map_nodes.cpp`, the two PNGs. Changed:
`core/CMakeLists.txt` (map_nodes.cpp), `core/node_menu.h` (Map action, hasPosition, dim, 8 rows, the notice),
`core/settings.h` (version 61), `main/map_page.h` (store + highlight), `main/map_page.cpp` (drawMapNodes + the
cost line), `main/nodes_page.h` / `.cpp` (nodeTypeColour's one home, the dim row, hasPosition),
`main/main.cpp` (the Map action, the highlight state and its two clears, the store hand-over),
`test/host/run.sh`, `tools/preview_tanmatsu.py`, `test/host/test_node_menu.cpp` and `test_node_ops.cpp` and
`test_node_view.cpp` (their row counts and indices moved by exactly the new Map row — the ROW LIST is a pinned
interface, so the three tests had to learn it; no expectation was weakened).

### What is NOT proven, and the next step

Everything here is host-only: no install, no flash, no card. **To prove on the device:** a node with a position
appears as a dot at its place (the advert's position is already parsed, so any positioned contact will do), a
favourite shows its gold ring, z13+ shows names without overlaps, the node menu's Map row centres and rings the
node, and panning clears the ring. The `map: N nodes in view, M labels, X us` line gives the on-device cost — the
number to compare with the 54 us above is the same line at z16 with the archive full.


**Claude's review of EST-466 (S515, 2026-11-19 19:3x):** one fix before the commit: the Map page read the node table with NO lock while the radio task inserts rows (`advertInArchive`); the marker pass now runs under `StoreLock` (recursive, so no deadlock), only the marker pass, never the tile compose. The report had no armed failure, so Claude armed one: accepting an exact 0,0 position fails `test_map_nodes.cpp:193` and `:354` (2 FAIL). Preview z14 checked.
## 2026-11-19 · S515 (EST-465) · node storage UI: All / Radio / Archive, the node menu's store rows, Settings › NODES

> Written by **DeepSeek** for ticket **EST-465**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v59 `e4b1121`** (tree clean). **Version: 60**
> (`core/settings.h` kAppVersion). `firmware/` was read only and is unchanged.
>
> **ITEMS DONE: 1, 2, 3, 4, 5.** Host exit 0, build exit 0, three previews written. Nothing is installed.

### 1. The Nodes page: the view filter and the store tag (item 1)

- **Key: LEFT / RIGHT.** Both are FREE on the Nodes list today (main.cpp's Nodes branch uses Up/Down for the
  list, Enter for the menu, `/` for search, `f` favourite, `a` advert, Esc back; the own symbol key is the
  page-top hook). A letter would either steal a typed character from the search bar or collide with `f`/`a`, so
  the two arrows that do nothing are the honest choice. The hint bar's first chip is now `lt/rt view`.
- Three chips **All · Radio · Archive** (default **All**) in the head band, drawn where "N nodes heard" was:
  the active one is the gold ring + `kSelBg`. The head's right slot is the ticket's live line
  `Radio 10 / 300 · Archive 412` (`core/node_view.h::nodeStoreCountsText`). Chips and counts share the band with
  the counts' measured width reserved, so they cannot overlap.
- Every row has the SAME name x; a radio row adds a **14x14 gold "R" plate** (bg ink) between the type dot and
  the name; an archive row adds nothing. The favourite dot (-74), the v57 fresh dot (-46) and the age/SNR
  columns keep their EST-461 places — the first preview row carries all three (R + gold dot + red dot).
- The pipeline is frozen order (EST-461) → **view filter** → search (EST-449), one function used by the draw
  and every key, so "N of M" is the matched count over the VIEW's rows (`10 of 10`, not `10 of 412`).
- No SD card: one dim line under the header, "No SD card: heard nodes are kept until restart", and the list
  starts 18 px lower (the line occupies its own strip; nothing overlaps).
- A view change puts the box on the new view's first row and **re-seats the 1.5 s ack dwell without acking**
  (entry alone still acks nothing, EST-461's rule).

### 2. The node menu (item 2)

`core/node_menu.h` gains `AddToRadio` / `MoveToArchive` (exactly one of the two, by the row's `in_radio`) and
`DeleteEverywhere`, and `kMaxNodeMenuRows` goes 6 → 7. **"Delete everywhere" REPLACED "Remove contact"** in the
node menu: with ONE table (EST-463) they were the same operation, and offering both would have been two labels
for one destructive action. (`RemoveContact` stays in the enum for the Settings › NODES list's own EST-447
confirm path, which the ticket did not ask to change.)

- Add to radio → Ok: "Added to the radio"; **RadioFull: `Radio full (300 / 300): move a node to the archive
  first` and nothing changed** (the rule is core/node_table.h's; the sentence is built from `kMaxRadio`).
- Move to archive → "Moved to the archive".
- Delete everywhere → the shared EST-447/455 sheet, titled "Delete everywhere", with
  `Delete <name> from the radio and the archive? Its DM history stays on the card.`
  **That sentence is TRUE in the code:** `core/node_table.cpp::deleteEverywhere` removes the contact row only,
  and mc_link's remove/clear paths never touch the DM ring or `dm-*.log` (they say so in their own log lines).
- The delete captures the node's **public key** when the question opens and applies `deleteEverywhere(pub)`, so
  a shifted row cannot be hit. After the delete the shown order is rebuilt, the key is looked up again
  (`contactsFind`, -1 — it is gone) and `nodeMenuFocusAfterRemove` keeps the box on the row that slid into its
  place, clamped; the page stays open.

### 3. Settings › OM Lite › NODES (item 3)

New rows at the top of the section:
- **Add heard nodes to the radio** — Cycle Off / Clients / All, value from the live setting, written to NVS
  `mcset/heardto` through a new `setHeardToRadioSetting()` (a refused write keeps the old value + a notice).
- **Stored nodes** — `Radio 10 / 300 · Archive 412 · SD ok` (or `· no SD card`), read live on every draw.
- **Clear radio (archive kept)** — behind the shared sheet ("Move all N radio nodes to the archive? The archive
  keeps them all."); the apply path clears the `in_radio` mark on every row and never removes a row.
- Then the EST-447 rows as before: Remove a contact (list), Clear all contacts, New-node notice.
- **The contact list: BEFORE it listed the WHOLE table** (since v59 that means radio + archive rows), because
  `buildNodesSubRows` walked `contactsOrder()` unfiltered. **AFTER it lists ONLY `in_radio` rows** (empty →
  "No radio nodes yet"), which is what "Remove a contact" acts on.

### 4. Host tests (item 4)

`test/host/test_node_view.cpp` (new, 718 checks): counts + order per view over a frozen order, the frozen order
kept in every view, search on top of a view, the chip cycle, the menu's rows per state (archive → Add to radio,
radio → Move to archive, never both), the RadioFull path with the store snapshot unchanged and the exact
sentence, the delete confirm's exact words, the focus re-find after a real `deleteEverywhere` (gone → clamped;
still-present key → its new position), and the Settings row values.
`test_node_menu.cpp` and `test_node_ops.cpp` are updated to the new seven-row menu (the old six-row/Remove
expectations were the only failures).

**Armed twice (restored afterwards, suite green again):**

```
FAIL test_node_view.cpp:89  om::ui::nodeViewCount(f.s, om::ui::NodeView::Archive, radio) == 2 (got 5, want 2)
FAIL test_node_view.cpp:157  rows[4].action == om::ui::NodeAction::MoveToArchive
```

### 5. Measured

- `sh test/host/run.sh` → **exit 0**, **32,892 checks, 0 failures** (sum of every suite's count, ASan stages
  included; the new suite alone: 718).
- `make build` → **exit 0**, `application.bin` = **0x19FCB0 = 1,703,088 B**, 19 % of the app partition free
  (0x60350).
- Internal RAM: **not re-measured** (no idf.py size pass in this run). The only new static storage is
  `g_nodes_view_order[2000]` = **+8 KB .bss**; the chips/R plates allocate nothing.
- Previews: `shots/2026-11-19-nodes-views.png` (All view: R tags, an archive row, a fresh row, a favourite, one
  row with all three), `shots/2026-11-19-node-menu-radio.png` (menu on an archive node: Add to radio),
  `shots/2026-11-19-settings-nodes.png` (the three new rows).
- Files: new `core/node_view.h`, `test/host/test_node_view.cpp`; modified `core/node_menu.h`, `core/settings.h`,
  `main/{mc_link.h,mc_link.cpp,nodes_page.h,nodes_page.cpp,main.cpp}`, `tools/preview_tanmatsu.py`,
  `test/host/{run.sh,test_node_menu.cpp,test_node_ops.cpp}`, `docs/PROGRESS.md`, two new/changed shots.
- **Not verified:** no device run — the chips' fit with the real generated faces, the R plate on the glass,
  Left/Right on the keyboard, and the radio.bin/SD writes the actions trigger are code-read + host-tested only.


**Claude's review of EST-465 (S515, 2026-11-19 19:2x), two fixes before the commit:** (1) Add to radio / Move to archive / Delete everywhere / Clear radio now FORCE the SD archive write (`g_archive_force`): a structural change rewrote `radio.bin` at once but the archive waited its 30 s floor, so a power-off in that window brought a deleted node back from the card at the next boot. (2) The Nodes hint bar overflowed its left edge by ~50 px with six chips (visible in the preview): `lt/rt` → `<>`, `favourite` → `fav`; the preview re-rendered and fits. Noted, not changed: after Delete everywhere a DM thread with that node leaves the chat list until the node is heard again (its log stays on the card, as the confirm text says).
## 2026-11-19 · S515 (EST-463 part 2) · node storage: radio.bin on /int, nodes.bin on SD, boot merge

> Written by **DeepSeek** for ticket **EST-464**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Tree = the uncommitted EST-463 part 1 on **v58 `ffe0abf`**. **Version: 59**
> (`core/settings.h`). `firmware/` read only, unchanged.
>
> **ITEMS DONE: 0 (the blocker), 3a, 3b, 3c, 4 (read + test), 5 (tests + ASan), 6 (boot log).** The node storage
> core is complete; the UI (Nodes filter, Add to radio / Move to archive menu, Settings rows) stays EST-465.

### 0. The blocker fixed (EST-463 part 1 review)

- `main/mc_link.cpp`'s advert branch no longer calls `contactsUpsert` directly: it calls
  `advertInArchive(g_contacts, g_heard_to_radio, …)` (`node_table.*`). With the new setting
  `HeardToRadio g_heard_to_radio` (NVS `mcset`/`heardto`, default **Off**) a verified advert is **archive only**;
  nothing enters the radio automatically and a full radio still refuses.
- The NVS blob is now only the **fallback** (when `/int` does not mount) and `contactsEncodeRadio()` writes ONLY
  `in_radio` rows, at most **32**. Radio node 33+ while on the fallback stays in RAM and on the SD archive, and the
  log says exactly `radio list over the NVS fallback cap (N radio, 32 stored)`. A host test proves the encoder
  never writes a non-radio row, never more than 32, and writes an EMPTY blob when the radio list is empty.
- The radio writer compares the encoded bytes' CRC with the last write, so an archive-only advert causes **no**
  internal-flash write at all (no write storm on the 16 KB NVS partition, whatever the table size).

### 3a/3b/3c. Persistence and boot order

- `core/node_store.*` (new, pure, host-tested): one file format for both stores:
  `magic "OMN1" | version u16 | record_size u16 | count u16 | flags u16 | records | CRC-32` (CRC over header +
  records; the record is the v4 contact record). Bad magic/version/record width/count/size/CRC/truncation is
  refused WHOLE. `mergeArchive()` and `migrateNvsToRadio()` live beside the codec.
- `/int` is mounted exactly as the launcher mounts locfd (`esp_vfs_fat_spiflash_mount_rw_wl("/int", "locfd", …)`,
  `format_if_mount_failed = false`, `max_files 10`, `CONFIG_WL_SECTOR_SIZE`, `use_one_fat = false`), NEVER formatted,
  our own `/int/om-lite/`; unmounted in `nodeStorageShutdown()` before the launcher frame/`esp_restart`.
- `/int/om-lite/radio.bin`: in_radio rows (cap 300), temp + rename (`radio.tmp`), v57 floors (structural at once,
  soft 10 min, ack 5 s), never per key press.
- `/sdcard/om-lite/nodes.bin`: ALL rows, temp + rename (`nodes.tmp`), at most once per 30 s, plus leaving Nodes
  (`contactsAckLeave`) and Esc (`nodeStorageShutdown`). No SD: RAM only, logged once.
- Boot: radio.bin → if absent, the NVS blob is decoded and its rows become `in_radio` (v1-v3; v4 keeps its flags),
  radio.bin is written and `migrated N nodes from NVS` is logged — the NVS blob itself is left UNTOUCHED. Then the
  archive is merged by key: radio wins `in_radio` + route; the newer `heard_at` wins name/type/hops/heard/RSSI/SNR/
  position; `fresh` comes from the archive. `restoreContacts()` now calls `initNodeTable()` itself (the blob
  scratch was allocated only in `begin()`, which runs after the boot restore).
- Boot log lines (item 6), exactly: `/int mounted (free N KB)` or
  `/int not mounted (…): radio list stays on the NVS fallback (cap 32)`, `radio: N nodes from /int/om-lite/radio.bin`,
  `archive: N nodes from /sdcard/om-lite/nodes.bin`, `migrated N nodes from NVS`, the unchanged `nvs … used, free,
  total entries` line, and `node table: 2000 rows, 320000 B in PSRAM (…)`.

### 4. One lookup — read and cited

- DM receive: `main/mc_link.cpp:1514`, `:1551` (`contactsFind(g_contacts, peer_pub)` on the one table).
- DM send: `main/mc_link.cpp:1834` (`dmPeerKey`), `:1866`, `:1899`, `:1940`, `:2035` — full-key lookup + route from
  the SAME row; an archive-only peer is as usable as a radio one.
- Ping / Trace: `main/mc_link.cpp:2409` (`contactByPub` → `contactsFind`), used by `nodePing` `:2443` and
  `nodeTrace` `:2472`.
- RPTR target: `main/rptr_link.cpp:135`, `:176` (`mc::contacts()`).
- Time row: `core/chat_meta.cpp:26-33` (`pathHopLabel` walks `s.count`, no radio filter); called from
  `main/main.cpp:1093`.
- Hint dot / unread: `core/mc_store.cpp:334-345` (`unreadTotals` walks the whole table); `main/main.cpp:3493`.
- New host test: `test_node_table.cpp` `testUnreadTotalsArchiveOnlyPeer` — an archive-only DM peer raises
  `dm_unread == 1`.

### 5/6. Evidence

- `sh test/host/run.sh` → **exit 0**, **31,699 checks, 0 failures**. New `test_node_store.cpp` = **185 checks**
  (plain + ASan/UBSan): codec round trips, every refusal, merge rules, migration from v2/v3/v4, the step-0 encoder.
- Armed twice (restored afterwards, green again):
  - `FAIL test_node_store.cpp:159  blob[2] | (blob[3] << 8) == 32 (got 35, want 32)` (cap removed)
  - `FAIL test_node_store.cpp:204  live.c[i1].out_path_len == 0x41 (got 255, want 65)` (archive overwrote the
    radio route)
- `make build` → **exit 0**, `application.bin` = **0x19E6E0 = 1,697,504 B**, 19 % of the app partition free.
- Internal RAM (`esp_idf_size`, idf6.0 env): **DIRAM 281,764 / 572,368 B (49.23 %)**, 290,604 B free
  (.bss 126,032 + 49,304, .data 22,268). The table, the NVS scratch and the 318 KB node-file buffer are PSRAM.
- Files: new `core/node_store.{h,cpp}`, `test/host/test_node_store.cpp`; modified `core/contact_blob.{h,cpp}`,
  `core/CMakeLists.txt`, `main/{mc_link.cpp,mc_link.h,main.cpp,CMakeLists.txt}`, `test/host/run.sh`,
  `docs/PROGRESS.md`; plus the part-1 files still uncommitted (`core/node_table.*`, `test/host/test_node_table.cpp`,
  `core/{mc_store.*,nodes_fresh.h,settings.h}`, `main/rptr_link.cpp`, `test/host/{test_contact_blob,test_store}.cpp`).
- **Not verified:** no device run (nothing installed): the real `/int` mount, the two atomic renames on FATFS, and
  the PSRAM allocations are code-read + host-tested only, not seen on the glass.


**Claude's review of EST-463 part 1 + EST-464 (S515, 2026-11-19 19:0x), two device-only defects fixed before the commit:**
1. **FatFs rename refuses an existing target** (`ff.c` `f_rename` → `FR_EXIST`; `vfs_fat_rename` passes it through), unlike POSIX, so the host tests could not see it: every write of `radio.bin` / `nodes.bin` after the first would have failed and the files would never have changed again. `writeFileAtomic()` now removes the old file between the finished temp file and the rename, and the boot loads use `readNodeFileOrTemp()` (the temp file when the real one is missing, CRC still decides, then renamed into place), so a power cut in that gap loses nothing and does not fall back to the stale NVS copy.
2. **A failed archive write had no retry floor** (`g_archive_write_failed` was set but never read): a pulled card meant a 340 KB re-encode under the store lock on every loop pass. A failure now waits the same 30 s floor; every write logs its duration (`archive: N written … (B, X ms)`), because it runs on the main task and is a UI pause.
**To prove on the device:** `/int mounted`, `migrated 10 nodes from NVS` on the first boot, then a SECOND `radio:`/`archive: … written` line succeeding (the rename fix), and the archive write time.
## 2026-11-19 · S515 (EST-463) · node storage core: ONE table, the radio mark, the rules

> Written by **DeepSeek** for ticket **EST-463**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v58 `ffe0abf`** (tree clean). **Version: 59**
> (`core/settings.h` kAppVersion). `firmware/` was read only and is unchanged.
>
> **ITEMS DONE: 1 (one table) and 2 (the rules in a pure core file, host-tested), plus the parts of item 3 that a
> host can prove (the blob's v4 format).** Items 3a/3b/3c (the /int mount, radio.bin, nodes.bin, boot order,
> merge and migration), 4 (wiring the DM/ping/trace/time-row lookups through the new rules — they already read
> the same table, so what is missing is the persistence, not the lookup), 5's codec/merge/migration tests, and
> 6's boot log lines are **NOT done**: the 30-minute cap was reached at a clean boundary after item 2 + the format
> change that item 1 forced. Nothing half-wired was left behind: the tree builds and every suite is green.

### 1. ONE TABLE (done)

- `core/mc_store.h`: `kMaxNodes = 2000`, `kMaxRadio = 300`, and `kMaxContacts` kept as an ALIAS of `kMaxNodes`
  so every existing `kMaxContacts` user keeps compiling at the new capacity (renaming 30 sites would have been
  diff noise on a ticket that must stay reviewable).
- `Contact` gains `bool in_radio` (one flag, one table; a favourite does not imply it).
- `ContactStore` is no longer an inline `Contact c[32]`: `Contact* c` + `cap` + `count`, ONE allocation of
  `contactsTableBytes()` = **2,000 x 160 = 320,000 B**, taken from PSRAM on the device through
  `setContactAlloc()` (installed at the bottom of `main/mc_link.cpp`, before the long-lived store is constructed;
  the host leaves the hook at `malloc`). A failed allocation leaves `cap == 0`, which every operation treats as a
  full table and `initNodeTable()` logs. `sizeof(ContactStore)` is 24 B, not 320 KB.
- Copy semantics are OWNED (copy ctor / operator= / destructor), because the tests hand a small store back by
  value and the ASan stage (leak detection) would otherwise report one 320 KB block per store.
- **Order:** `contactsOrder()` is now ONE stable bottom-up merge sort (group = favourite / fresh / rest, then
  `heard_at` descending, then store index for a tie) instead of the three O(n^2) passes. The scratch is a
  file-scope `int[2000]` in `mc_store.cpp` (never a task stack); `out` is the merge's second buffer when it is
  large enough. Host measurement: **2,000 rows in 0.20-0.27 ms**, printed by the new test. The order is the same
  for every input the old code had (favourites, then fresh newest-first, then the rest).

### 2. THE RULES IN A PURE CORE FILE (done)

`core/node_table.h/.cpp`, host-tested (`test/host/test_node_table.cpp`, 6,685 checks):

- `HeardToRadio { Off, Clients, All }`, default **Off**, `heardToRadioFromByte()` maps any unknown byte to Off,
  `heardToRadioAllows()` says Clients = type 1 only.
- `advertInArchive()` inserts or refreshes; an INSERT sets `fresh` and sets `in_radio` only when the setting
  allows it AND there is room; a REFRESH never changes `in_radio` or `favourite` (a node the user moved to the
  archive cannot be pulled back by its next advert).
- `addToRadio()` → **Ok / AlreadyIn / RadioFull / NotFound**, and at 300 it refuses with **nothing changed** (no
  eviction, no reorder, no field moved) — Filip's rule 2.
- `removeFromRadio()` clears the flag and keeps the row (the archive still has the node).
- `deleteEverywhere()` removes the row from both stores; `setFavourite()` works on either store.

### 3. THE FORMAT CHANGE ITEM 1 FORCED (part of item 3, done)

The NVS blob's count was a u8 — 2,000 does not fit. `kContactBlobVersion = 4`: header is
`version u8 | 0 u8 | count u16 | record_size u16` (6 B) and the record is 159 B (the v3 tail + `in_radio`).
Versions 1, 2 and 3 still decode with their own 4-byte header and 148/158-byte records, so a blob already on
the device loads. `contactsDecode` no longer constructs a second store (that would have taken another 320 KB):
it clears `count` and keeps the caller's allocation. Round-trip at the full 2,000 rows is covered.

### What moved off .bss / the stack because the table grew (every kMaxContacts site)

| Where | Was | Now |
|---|---|---|
| `main/mc_link.cpp` NVS blob scratch | `uint8_t g_contact_blob[kContactBlobMaxBytes]` (would be 318 KB) | PSRAM pointer, `initNodeTable()` |
| `main/mc_link.cpp` DM peer snapshot | `static uint8_t[2000*32]` + `char[2000*24]` (112 KB) | PSRAM, once |
| `main/mc_link.cpp` PATH peer snapshot | `static uint8_t[2000*32]` (64 KB) | PSRAM, once |
| `main/rptr_link.cpp` merge rows | `static Row[kMaxContacts]` (~130 KB) | `static Row[kMaxRadio]` (repeaters only) |
| `main/main.cpp` Settings › NODES list | three arrays sized by `kMaxContacts` (~250 KB) | `kNodesSettingsRows = 64` (a page-sized list; the real list is Nodes) |
| `main/main.cpp` Nodes order buffers | `int order[kMaxContacts]` on the loop task's stack (8 KB each) | file-scope `g_nodes_order` / `g_nodes_shown` |
| `core/nodes_fresh.h` `nodesFrozenSync()` | `int kept[kMaxContacts]` on the caller's stack | compacts `f.order` in place |
| `test/host/test_store.cpp` | `int order[kMaxContacts]` | `static` (host) |

### Measured

- `sh test/host/run.sh` → **exit 0**, **23,496 checks, 0 failures** (the new suite alone: 6,685).
- `make build` → **exit 0**, `application.bin` = **1,688,928 B** (0x19C960), 19 % of the app partition free.
- Internal RAM (`esp_idf_size`, idf6.0 env): **DIRAM 281,740 of 572,368 B (49.22 %)**, 290,628 B remain.
  .bss 126,008 + 49,304, .data 22,268. The new .bss is the 8 KB sort scratch and the two 8 KB order buffers;
  the 320 KB table, the 318 KB blob scratch and the 176 KB of peer snapshots are in PSRAM by construction.

### Next (for EST-464 and the rest of EST-463)

The persistence half: mount `/int` exactly as the launcher does, `/int/om-lite/radio.bin` (header + records +
CRC-32, temp + rename, coalesced), `/sdcard/om-lite/nodes.bin`, the boot order and merge, the one-time migration
from the NVS v1/v2/v3 blob (which is now also the v4 fallback), and the six boot log lines. The lookup half
(item 4) needs no new table: every caller already searches this one table.

## 2026-11-19 · S515 (EST-462) · the time row

> Written by **DeepSeek** for ticket **EST-462**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v57 `dff0c02`**. **Version: 58** (`core/settings.h` kAppVersion).
> Items 1-5 are in: the real path on RX/TX, the L2 record carries it, the dim head line, host tests and the preview.
> `firmware/` was read only and is unchanged.

### 1. THE REAL PATH, KEPT (done)

- `core/mc_store.h`: `Message` and `DmMessage` gain `hops_known`, `hops`, `hash_size`, `path_bytes`,
  `path[meshcore::kMaxPath]`, `frame_len`, `RouteType route` (Flood / ScopedFlood / Direct / Unknown), `copies` and
  `frame_fp`. `snr_x10` and `direct` stay.
- `main/mc_link.cpp`: one `takeWirePath(view, len, message)` for the group-text and DM RX branches copies the
  path, the hop count, the hash size, the frame length **the RX log prints** and the route from `View::route()`
  (0x00 TRANSPORT_FLOOD = scoped, 0x01 flood, 0x02 direct). Our own channel text stores the frame length we queued
  and the route the scope plan chose; our own DM stores the composed frame's length and its flood/direct route.
  A retry moves those facts (length, route, fingerprint) to the frame the air now carries.
- **Copies** are counted, not just dropped: the seen-ring hit calls `noteDroppedCopy(fp, ours)` before returning.
  `ours` = our own echo (the 0x42 rule) → `copies++` on the outgoing line; otherwise a duplicate of a peer's frame
  → `copies++` on the stored incoming line. The match is the message's own `frame_fp`, so no second table exists.

### 2. WHAT IS TRUE AFTER A RESTART (done)

- `core/mc_history.cpp` writes the real path in the L2's OWN fields — `hops`, `pathHashSize = hash_size`,
  `pathCount = hops`, `path[]`, length derived by `msgSetPath` — and adds nothing to the format. An own flood
  (hops not knowable) keeps the old flood shape (hops 1, pathCount 0), which reads back as UNKNOWN.
- The loader compares the shape: `hops == 0` is **direct** (true in both encoders); `hops > 0` with
  `pathCount == 0` (the v57 fake) or `pathCount != hops` is **UNKNOWN** and shows no hop count; only
  `pathCount == hops` with 1..3-byte hashes is a real path. `frame_len`, `route` and `copies` stay absent for a
  restored line: they are not in the record, and the formatter shows bytes only when they are real.

### 3. THE HEAD LINE (done)

- New pure core `core/chat_meta.h/.cpp`:
  - `pathHopLabel(contactStore, hash, len)` — the name only when **exactly one** full-key contact starts with those
    bytes, else the lowercase hex ("86a8"). Prefix-only rows are ignored. EST-463 can add the SD archive beside the
    contact table; the rule stays one function.
  - `chatMetaText(MsgMeta)` — incoming `"direct"` / `"3 hops via X"` [+ ` · 87 B`] [+ ` · heard 2×`]; own
    `"87 B"` [+ ` · repeated 2×`]. `heard`/`repeated` appear only from two copies up. Unknown fields contribute
    nothing.
  - `metaIncomingX/MaxW`, `metaOutgoingRight/MaxW` — the pixel box shared with the page.
- `main/chat_page.cpp`: the dim tail (`Face::Hint`, `kDimmer`) is drawn after the sender's own width on an
  incoming line and right-aligned just left of "you" on ours, clipped with the ellipsis by `drawTextClipped` so it
  never reaches the age column. `main/main.cpp` builds each row's meta from the message and the contact table.
  RSSI and SNR are not shown (DEVICE.md § 5b).

### 4. HOST TESTS (done)

- New `test/host/test_chat_meta.cpp`: 30 checks — the resolver with 1/2/3-byte hashes, unique → name, two → hex,
  none → hex, a prefix-only row ignored; the formatter for direct, N hops with/without via, one hop singular, a
  restored old record (empty), a restored new record (hops but no bytes), one copy not shown, own bytes/repeats,
  own restored empty; the clip never reaching the age column or "you".
- `test/host/test_history.cpp` +3 functions (38 checks): the real path round trip through the L2 decoder
  (`h.hops 3 / pathHashSize 2 / pathCount 3 / pathLen 6 / bytes equal`), a v57-style fake record (hops 1,
  pathCount 0) reading as `hops_known=false` while hops 0 reads as direct, and the DM record's real path.
- **Armed, both against the old behaviour, with the FAIL lines:**
  - old encoder restored (fake `hops 0/1`, pathCount 0):
    `FAIL test_history.cpp:320  h.hops == 3 (got 1, want 3)`
  - old loader restored (trust `hops`, `direct = hops <= 0`):
    `FAIL test_history.cpp:331  out[0].hash_size == 2 (got 0, want 2)`
    `FAIL test_history.cpp:362  out[0].hops_known == false (got 1, want 0)`
    `FAIL test_history.cpp:411  out[0].hash_size == 1 (got 0, want 1)`
- `sh test/host/run.sh`: exit **0**. Before: 28 suites / 17,140 checks. After: 29 suites / **17,208 checks**
  (+68: 30 new suite, +38 history). The UBSan lines from the vendored Ed25519 are pre-existing.
- `make build`: exit **0**; `application.bin` 0x19d570 = **1,693,040 B**, 0x62a90 (19 %) free of the 2 MiB app slot.

### 5. PREVIEW (done)

- `shots/2026-11-19-chat-meta.png` (`tools/preview_tanmatsu.py chat_meta`): one channel with a direct message, a
  3-hop line with a named via, a 3-hop line with a hex via, a restored old line (nothing), and two of our own —
  the second `87 B · repeated 2×`.

### FILES CHANGED

`core/chat_meta.h` (new), `core/chat_meta.cpp` (new), `core/CMakeLists.txt`, `core/mc_store.h`,
`core/mc_history.cpp`, `core/settings.h` (version 58), `main/mc_link.cpp`, `main/chat_page.h`, `main/chat_page.cpp`,
`main/main.cpp`, `test/host/test_chat_meta.cpp` (new), `test/host/test_history.cpp`, `test/host/run.sh`,
`tools/preview_tanmatsu.py`, `shots/2026-11-19-chat-meta.png` (new), `docs/PROGRESS.md`.

### NEXT

EST-463 (node storage, radio + SD archive) extends `pathHopLabel` to the archive; nothing in this ticket blocks it.
Nothing installed, flashed, pushed or committed — Claude reviews and commits.

## 2026-11-19 · S515 (EST-461) · dots, new contacts, screen off

> Written by **DeepSeek** for ticket **EST-461**: fenced Paperclip run, no session start/end, nothing installed,
> flashed, pushed or committed. Starting point **v56 `22a1e86`**. **Version: 57** (`core/settings.h` kAppVersion).
> Items 1 (dots), 2 (new contacts), 3 (power button), 4 (host tests) and 5 (previews) are all in; the one thing
> this run could NOT do is MEASURE NVS (no install, see § NVS below).

### 1. DOTS ON THE SIGILS (done)

- `main/chrome.h/.cpp`: `hintDotsSet(mask)` / `hintDots()` / `hintDotsNeedsRepaint()`, one bit per sigil
  (`kHintDotNodes` = bit 1, `kHintDotChat` = bit 2). `drawHintBar` draws a **7 px** (`disc r 3`) red dot at the
  sigil's **top-right** (`sx + 9, cy - 8`), in the chat list's own badge red (`theme.h kBad`), clear of the gold
  active ring (its nearest point on that diagonal is ~4.9 px from the sigil centre). Because it is drawn by
  `drawHintBar`, **every** page shows it with no page code.
- The RULE (which dot, from what) is `core/nodes_fresh.h::hintDotMask()`, and the bits are `static_assert`ed equal
  to chrome.h's, so the two cannot drift.
- The COUNT is one source: the new `core/mc_store.h::unreadTotals()` sums `unreadForChannel()` (channels) and
  `dmUnreadForPeer()` (DMs) — the same two functions the chat cards draw their badges from. No second counter.
- Repaint with no key press: the loop sets the mask once per pass and `hintDotsNeedsRepaint()` follows
  `noticeNeedsRepaint()`'s pattern (True once per CHANGE), so a message arriving on the Map page lights the dot
  within one 250 ms pass.

### 2. NEW CONTACTS (done, except the NVS numbers — see below)

- **a) `Contact::fresh`**, set **only** in `contactsUpsert()`'s INSERT branch (the one caller of it is the verified
  advert path in `mc_link.cpp`); a refresh of an existing row keeps it as it was, and a restored row takes the
  stored value (or `false` from v1/v2). `contactsOrder()` is now **favourites → fresh (newest heard first) → rest
  (by last heard)**, three O(n²) passes, no unstable tie-break.
- **b) The row's dot**: `main/nodes_page.cpp`, `disc(list_x1 - kTmRingInset - 46, y + 16, 3, kBad)` — in the strip
  between the gold favourite mark (`-74`) and the age column (which keeps the right edge), so name, type, age
  and SNR still fit.
- **c) ACK** — `core/nodes_fresh.h` (`NodesAck`): a key press that LANDS on a row acks at once (`nodesAckKeyMoved`,
  raised by Up/Down, Down out of the search bar, and the own symbol key's return to the top); otherwise the box
  must REST 1.5 s (`kNodesFreshDwellMs`, `nodesAckTick`). Page ENTRY alone acks nothing (`nodesAckEnter` only
  starts the dwell). **The order is frozen as DATA**: `NodesFrozen` is a snapshot of STORE indices taken on entry
  (`nodesFrozenBegin`), the page draws and filters THAT (`nodesOrderForPage`), and `nodesFrozenSync` only appends
  a contact that arrived while open and drops one that left. Re-sorted on the next entry. The search filter and the
  node menu still work on it (EST-449/455: the menu follows its STORE row).
- **d) Blob v3** (`core/contact_blob.h/.cpp`): the record grows from 148 to **158** bytes —
  `fresh u8 | has_position u8 | lat_e6 i32 | lon_e6 i32`. The decoder derives the stride from the VERSION (v1/v2 =
  148, v3 = 158) and reads the tail only for v3, with `stride - 10` as the offset; v1/v2 load with `fresh=false`
  and no position. The position comes from `advertParse()`'s `AdvertPosition` (`mc_identity.h`, already decoded);
  `contactsUpsert()` takes it as 3 defaulted arguments, and an advert without a position never wipes a known one
  (the same rule as the name). **Nothing draws it yet** (EST-463).
- **d) Writes**: `markContactsAck()` raises its own dirty flag; `contactsPersistTick()` now honours a **5 s** floor
  for it (`kContactsAckWriteMs`), and `contactsAckLeave()` (called when the page changes away from Nodes) forces
  one write past the floor. No write per key press, and a failed write re-raises the flag.
- **e)** The "New node: …" notice is untouched.

### 3. POWER BUTTON = SCREEN OFF (done)

- `core/screen_power.h`: `ScreenPower::onEvent(ScreenEv)` → `ScreenAct`. Press toggles, RELEASE never acts, a KEY
  while off wakes it (`OnByKey`, i.e. wake-and-swallow), a MESSAGE never wakes it.
  **Corrected by Claude at review (S515, 2026-11-19), at Filip's word:** *"not any button, the power button wakes the device. It is also a 'key-lock' for transport.."* `OnByKey` is gone: a key while off returns `Swallow` and the screen STAYS OFF; only `PowerPress` wakes it. The test now checks two keys in a row stay dark (armed: the old wake-on-key rule gives 6 FAIL lines, `test_nodes_fresh.cpp:271-279`). The ticket had been filed with Claude's first, wrong wording.
- `main/main.cpp`: the ACTION event is read as `ev.args_action.type == BSP_INPUT_ACTION_TYPE_POWER_BUTTON` with
  `state == true` on press (struct names from `bsp/input.h`, not guessed). OFF remembers
  `bsp_display_get_backlight_brightness` / `bsp_input_get_backlight_brightness`, sets both to 0, logs
  `[screen] off`; ON restores both, logs `[screen] on (by power|key)` and repaints once. While off the frame is
  composed but **not presented**. Any nav/keyboard/scancode event while off wakes the screen and is `continue`d
  before the key handling, so it cannot move the focus, type or switch page. `leaveToLauncher()` restores the
  backlights first (`[screen] on (launcher)`), so the launcher never starts dark.

### 4. HOST TESTS (done)

- New suite `test/host/test_nodes_fresh.cpp` (**83 checks**), wired into `run.sh` twice: plain and under
  ASan+UBSan (like the blob stages). It pins: the dot mask from unread/fresh counts (including the real
  `unreadTotals()` over a message ring + a DM ring), `contactsOrder` with favourites/fresh/rest, an upsert refresh
  not re-freshening, the ack rules (key landing, 1.5 s rest, entry alone, the 32-bit wrap, one ack per rest), the
  frozen order (no re-sort on ack, appended on arrival, dropped on removal, re-sorted on entry, still filtered by
  the search) and the screen state machine.
- `test_contact_blob.cpp` grew the v3 tail (fresh + position round trip, `has_position=false` leaving no trusted
  coordinates, a REAL 148-byte v1 blob loading with `fresh=0`), and its version/width checks moved to 3/158.
- **`sh test/host/run.sh` → exit 0** — **28 compiled suite summaries, 14,971 checks, 0 failures**
  (before, on a pristine `22a1e86` exported to `/tmp`: **26 suites, 14,805 checks, 0 failures, exit 0**; the
  difference is the new suite twice: 83 + 83).
- **ARMED, three probes** (each patched into a copy under `/tmp`, never the tree):
  1. old two-pass `contactsOrder` (no fresh group) → `FAIL test_nodes_fresh.cpp:128  order[1] == 1 (got 3, want 1)`
     · `:129  order[2] == 2 (got 1, want 2)` · `:130  order[3] == 0 (got 2, want 0)` · `:131  order[4] == 3 (got 0,
     want 3)`.
  2. codec with no v3 tail (the pre-S515 record) → `FAIL test_contact_blob.cpp:87  a.fresh == b.fresh (got 1, want
     0)` · `:88  a.has_position == b.has_position (got 1, want 0)` · `:92  a.lat_e6 == b.lat_e6 (got 461013000,
     want -858993460)`.
  3. `kNodesFreshDwellMs = 0` (opening the page would ack the top row at once) → `FAIL test_nodes_fresh.cpp:167
     om::ui::nodesAckTick(a, 1000) == -1 (got 0, want -1)` · `:176  nodesAckTick(b, 1001) == -1 (got 3, want -1)`.

### 5. PREVIEWS (done)

- `shots/2026-11-19-nodes-fresh.png` — two favourites (gold mark), then the two still-new rows with the red dot in
  the device's own place, then the rest; the focus box sits on the first new row and its dot is still there.
- `shots/2026-11-19-hint-dots.png` — the hint bar with BOTH dots, drawn on the Launcher on purpose (the dots are
  chrome, not a page). `tools/preview_tanmatsu.py`'s `hint_bar()` gained a `dots=` argument drawn exactly where
  chrome.cpp draws them (`cx + 9, cy - 8, r 3`).

### Build

- `make build` → **exit 0**. `application.bin` **0x19c750 = 1,689,680 B (1,650.1 KiB) = 51.6 % of the 3,200 KiB
  ceiling**; the build's own line: `Smallest app partition is 0x200000 bytes. 0x638b0 bytes (19%) free`. That is
  **+5,408 B** on v56's 1,684,272 B.

### NVS — logged, NOT measured (no install in this run)

- `mc_link.cpp` logs `nvs before the contact blob (boot): used N, free N, total N entries`,
  `nvs after the contact blob (boot): …` and `nvs after the contact blob (write): …` (`nvs_get_stats` on the
  default partition), so the first boot log after the install answers the ticket's question.
- **The two numbers are NOT in this entry because this run had no device**: the ticket's "measure" needs the
  install that is explicitly excluded here. The v3 blob costs 10 B per row: 32 rows = 4 + 32 × 158 = **5,060 B**
  against 4 + 32 × 148 = **4,740 B** before (+320 B at a full table).

### Files

- `core/nodes_fresh.h` (new), `core/screen_power.h` (new), `test/host/test_nodes_fresh.cpp` (new).
- `core/mc_store.h/.cpp`, `core/contact_blob.h/.cpp`, `core/settings.h` (57), `main/chrome.h/.cpp`,
  `main/nodes_page.cpp`, `main/mc_link.h/.cpp`, `main/main.cpp`, `test/host/test_contact_blob.cpp`,
  `test/host/run.sh`, `tools/preview_tanmatsu.py`, two PNGs above.

### Known limits (named, not hidden)

- The freeze holds **STORE indices**, like EST-455's node menu. A **removal** while the page is open compacts the
  table, so a later row shifts down one; the snapshot drops rows past `count` but cannot tell that index 2 is now
  somebody else. Removing a node closes the menu, and the next entry re-sorts, so the window is one page visit.
- The dwell is decided on the main-task pass (250 ms, 30 ms while the map fills), so the ack lands at 1.5 s + up to
  one pass, never before 1.5 s.
- While the screen is off the frame is still composed every dirty pass (cheap, keeps the state current) and the map
  fill still logs; no panel blit, no backlight.
- Not checked on glass: the dots' exact pixel look, the power button's ACTION edge on this board, and the NVS
  entries above.

## 2026-11-19 · S514 (EST-457) · emoji in messages and names (a subset of the L2's set)

> Written by **DeepSeek** for ticket **EST-457**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Keeps EST-456's work. **Version: 52** (`core/settings.h` kAppVersion) — the ticket's number.

### What this builds

1. **A SUBSET, not the L2's 623,056 B blob.** `tools/mkemoji_subset.py` reads
   `firmware/src/ui/emoji_data.bin` **with the L2's own layout** and writes `main/emoji/emoji_subset.bin` in the
   same format (`"OMEM"` v1, 13x13 cells, RGB565 + 4-bit alpha, 4-byte aligned cells, sorted index):
   **274 glyphs / 120,576 B** (cap **120 KiB = 122,880 B**, headroom 2,304 B). The selection: **every code
   point the L2's own picker offers** (parsed out of `firmware/src/ui/emoji_picker.cpp`, so the two lists cannot
   drift) plus all of **Smileys & Emotion**, the **People & Body hand gestures**, the **hearts**, and the
   **common symbols** (check/cross, warning, question/exclamation, arrows, stars, fire, sparkles, weather,
   clocks, refresh, bell, locks). The parser is **the L2's** `firmware/src/ui/emoji_table.h`, included from that
   tree and compiled unchanged — not forked, and the host test includes the same header.
2. **Drawing = one place.** `main/font.cpp`'s `draw`/`textWidth`/`textWidthRange`/`textWidthN` now walk
   **the L2's `textUnitAt`** (`firmware/src/ui/text_wrap.cpp`, also compiled into the core component) instead of
   a local decode. An emoji the subset holds draws at **2x (26x26, nearest neighbour)**, its 4-bit coverage
   blended over the framebuffer pixel already there (so no background colour has to be guessed, unlike the L2's
   `drawEmoji`), **vertically centred on the text line** (`baseline - (ascent-descent)/2 - 13`), and advances
   **26 px = 2 x 13, the scaled width** (the ticket's word; the L2 adds 1 px of air at 13 px, none is added
   here). Because every text path in the app ends in `Font::draw`, this reaches **chat messages, the composer
   echo, channel/DM card previews and node names** in one edit. A code point the subset lacks draws nothing and
   keeps today's **half-em advance** (`ascent/2`).
3. **The wrap measures the real width.** `core/emoji.cpp::emojiTextWidth` is the ONE width rule; `Font`'s
   measuring functions and `main.cpp`'s `measureBody` callback (the `core/chat_layout.cpp` wrap's measure) both
   use it, so a line can never be measured narrower than it draws. The L2's walker rules are kept exactly: a
   variation selector / keycap / skin tone belongs to its base, a ZWJ belongs to the part before it, a stray
   modifier takes no room.
4. **Tests.** `test/host/test_emoji.cpp` (+ a `mkemoji_subset.py --check` stage that fails on a stale blob):
   the subset parses with the L2 parser under the cap with sorted/4-aligned/in-range/inked entries; lookups in
   and out (`0x1F44D` yes, `'A'`/ZWJ/VS16/skin tone/regional indicator/`0x1F5FF` no); the walker on heart+VS16,
   thumb+skin tone, a three-part ZWJ family and a keycap; widths (emoji 26, selectors free, ZWJ parts in order,
   out-of-subset half-em) and **the chat wrap with emoji**: every wrapped line is <= its width, four emoji fit in
   104 px and five do not.
5. **Preview.** `tools/preview_tanmatsu.py` reads the same subset blob and draws it at 2x;
   `shots/2026-11-19-chat-emoji.png` is a chat with `Radio check 👍🔥🗿` — two in the subset, the moai **not** in
   it (the gap today's behaviour leaves) — plus an emoji in the composer echo and in a card preview.

### Verified (real exit codes, this run)

- `sh test/host/run.sh` → **exit 0** (its last stage is the new emoji suite: `emoji: all checks passed`, and
  `mkemoji_subset.py --check` printed `matches a fresh build`). No FAIL line anywhere else (the two `FAIL` hits in
  the log are `test_dm_send: 140 checks, 0 FAILURES`).
- The L2 suite: `firmware$ sh test/host/run.sh` → **exit 0** (the shared `emoji_table.h` / `text_wrap.cpp` are
  unchanged by this ticket — the L2 tree was only READ).
- `make build` → **exit 0**; `application.bin` **0x19aec0 = 1,683,136 B** vs the **2 MB (0x200000)** build
  check → **0x65140 = 413,504 B free (20 %)**; vs the **3,200 KiB** per-app ceiling → **51.4 %**. Previous
  (EST-456): 0x17d080 = 1,558,656 B, so this ticket costs **+124,480 B** (the 120,576 B blob + ~3.9 KB of code).
  The map has `esp-idf/main/libmain.a(emoji_subset.bin.S.obj)`, so the blob is really in the image.
- Two ARMs, both red and restored: **arm 1** `kEmojiAdvance = 13` (the 2x scale forgotten) →
  `FAIL test_emoji.cpp:125 emojiAdvance(0x1F44D) == 26` plus 7 width/wrap FAILs; **arm 2**
  `emojiTextWidth` blind to emoji (`if (false && e > 0)`) → 7 FAILs incl. `fakeMeasure(four) == 104` and
  `tighter.count == 2`. Logs: `sweep/est457-arm1.log`, `est457-arm2.log`.
- Licence: `main/emoji/emoji_data.LICENSE.txt` copied next to the subset blob (SIL OFL 1.1 / Noto, the same file
  the L2 ships).
- **Nothing installed, flashed or committed.** No server touched.

## 2026-11-19 · S514 (EST-456) · node menu: Ping + Trace

> Written by **DeepSeek** for ticket **EST-456**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Keeps EST-455's work. **Version: 52** (`core/settings.h` kAppVersion).

### What this builds

1. **PING** is the L2's, exactly — `BaseChatMesh::sendRequest(REQ_TYPE_GET_STATUS)`
   (`firmware/lib/meshcore/helpers/BaseChatMesh.cpp:690-715`) and its RESPONSE through
   `Listener::onUnhandledResponse` (`firmware/src/mesh_link.cpp:757-779`). The request frame is **reused, not
   rewritten**: `core/node_ops.cpp::buildPingFrame` calls the EST-453 `rptr::buildStatusRequest`, and the answer is
   opened by `rptr::openResponse` + the 4-byte timestamp check. The L2's states are kept
   (`Idle/Sent/Replied/Timeout/Failed`), the deadline is the L2's status floor **8 s**
   (`rptr_link.cpp` kEstimateMs), and the reply's notice is the ticket's words **"Ping <name>: 1.8 s"** (seconds,
   one decimal). Offered for **every contact whose full key we hold** — the L2's own rule, `if (c.hasKey)`
   (`firmware/src/ui/node_menu.cpp:32-35`), a flood when no route is stored.
2. **TRACE** is upstream `Mesh::createTrace()` + `sendDirect()`
   (`firmware/lib/meshcore/Mesh.cpp:605-620`; `:692-714` appends the path **to the payload** and leaves header
   `path_len = 0`): header `(0x09 << 2) | DIRECT`, payload `tag(4 LE) | auth(4 LE) | flags | path hash bytes`,
   `flags & 3` = `hash_size - 1`. It is offered **only when the contact has a stored route**
   (`contactHasOutPath`, `out_path_len != 0xFF` — the L2's `c.hasKey && c.pathKnown`,
   `firmware/src/ui/node_menu.cpp:36-38`); with no route the row is **absent, not dimmed**. The answer is parsed
   like upstream `Mesh::onRecvPacket` (`Mesh.cpp:42-57`) and the L2's `onTraceRecv`
   (`firmware/src/mesh_link.cpp:784-818`): one SNR byte per hop from the frame's path, hop hashes from the payload
   after the 9-byte head, `hash_bytes = 1 << (flags & 3)`. The result is the L2's **TraceView**: one line per hop
   with the **2-byte hop hash** (a known repeater's name when the leading key bytes match, else hex) and its SNR,
   then "at this node", then the total ("3 hops · RTT 2.4 s"). Every length is bounds-checked and the parser is in
   the ASan/UBSan stage.
3. **One radio owner.** Both frames are built with the contact row under the store lock and pushed into the SAME
   four-slot `TxQueue` a DM/advert uses (`TxKind::Frame` via `queueFrame`), so the **radio task** is the only place
   that transmits (`mc_link.cpp::serviceRequests`). Nothing is sent from the receive path: `onPacket` only records
   the ping's reply and the trace's hops. While a job runs the row says **"waiting..."**; **Esc cancels the wait**
   (not the packet — it may already be on the air) and a deadline on the radio task's clock turns an unanswered
   ping into `Timeout` and a silent trace into "no data back".
4. **The menu** (`core/node_menu.h`) gains `NodeAction::Ping` and `NodeAction::Trace` in the L2's order
   (DM, Info, Ping, Trace, Favourite, Remove; max rows 6). `NodeMenuContext` gains `pathKnown` and the two
   pending flags (the value "waiting...").

### Results (real exit codes, 2026-11-19)

- **Host tests: `sh test/host/run.sh` exit 0** — **26 compiled suite summaries, 16,426 checks, 0 failures**
  (`sweep/est456-host-final.log`; the ASan/UBSan repeats are included, as in earlier entries). The new stage
  `test_node_ops` is **98 checks, 0 failure(s)**, run twice (plain and ASan/UBSan). It pins the TRACE frame to
  `26 00 44 33 22 11 00 00 00 00 01 ab cd ef 01 23 45` (from upstream, not from this code), the 3-hop answer's
  names/hashes/SNRs and the total, the ping frame to *be* the EST-453 status request, the state machine and
  "Ping RPT-Kamera: 1.8 s", and the menu rules (Trace absent without a route; Ping on `hasKey`; both on a
  repeater with a key, neither without one).
- **ARMED — two mutations, both red, run from throwaway copies (`sweep/est456-arm/`), the tree untouched:**
  - `core/node_menu.h` Trace condition `hasKey && pathKnown` → `hasKey` →
    `FAIL test_node_menu.cpp:98  om::ui::nodeMenuRows(...) == 5 (got 6, want 5)`.
  - `core/node_ops.cpp` `if (hops > count) hops = count;` removed →
    `FAIL test_node_ops.cpp:236  tv.count == 2 (got 4, want 2)`.
- **Build: `make build` exit 0.** `application.bin` **0x17d100 = 1,560,832 B**; `make size`: **Flash
  1,450,336 B used**, total image **1,560,434 B**, app partition **0x200000** with **0x82f00 (26 %) free**;
  the **3,200 KiB (3,276,800 B)** budget leaves **1,715,968 B free (52.4 %)**. Versus EST-455's 1,552,784 B
  (0x17b190): **+8,048 B**.
- **Preview:** `shots/2026-11-19-node-trace.png`, **800×480**, rendered by the one tool
  (`python3 tools/preview_tanmatsu.py node_trace`): the Nodes page behind a **3-hop** trace result — RPT-Kamera
  7.0, `2e33` 11.5 (the unmatched hop as its 2-byte hash), RPT-Vzhod 2.0, "at this node" −6.2, total
  "3 hops · RTT 2.4 s". Fixtures invented (rule 9).
- **Files (new):** `core/node_ops.{h,cpp}`, `test/host/test_node_ops.cpp`,
  `shots/2026-11-19-node-trace.png`.
  **Files (edited):** `core/node_menu.h` (the two actions, `pathKnown`, pending values, max 6),
  `main/mc_link.{h,cpp}` (the jobs, the two builders, the RX hooks, the deadlines, the cancel/poll),
  `main/nodes_page.{h,cpp}` (the trace-result sheet, the pending values),
  `main/main.cpp` (menu wiring, Esc = cancel the wait, the trace sheet, the poll),
  `core/settings.h` (version 52), `core/CMakeLists.txt`, `test/host/run.sh`,
  `test/host/test_node_menu.cpp`, `tools/preview_tanmatsu.py`, this file.
- **NOT done (by the ticket):** nothing installed or flashed (the device waits in BadgeLink for one install),
  nothing committed. **Not checked on the glass:** the live reply/SNR path needs an install.

## 2026-11-19 · S514 (EST-455) · node menu + favourites

> Written by **DeepSeek** for ticket **EST-455**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts after EST-454 is in the tree and keeps its work. **Version: 50.** The ticket
> asked for this entry FIRST; the results below are filled in after the real runs.

### What this builds

1. **Enter on a node in the Nodes list opens its NODE MENU** — a modal sheet in the confirm sheet's house style.
   Only actions that work on the Tanmatsu today, in the L2's order: **Direct message** (clients only; opens that
   contact's DM thread in Chat, including an empty thread view), **Info** (the existing detail pane), **Favourite**
   (value `on`/`off`; Enter toggles), **Remove contact** (the EST-447 confirm and removal). Ping, Trace, Map and
   Reset path are NOT offered until they exist. Esc closes the menu; `f` on a focused node toggles favourite
   without opening it.
2. **Favourite toggling** is a structural contact change: the RAM row changes under the store lock and raises the
   structural flag, so the same `contactsPersistTick()` writes the `mcct` NVS blob on this loop's pass (EST-437).
   A short notice says `<name>: favourite` / `<name>: not favourite`; the list re-sorts at once and the focus
   follows the node to its new row. The row mark is a **small gold dot**, because the generated faces carry
   ASCII + U+00A0..U+017F only and therefore have **no U+2605 star glyph** (tools/mkfonts.py).
3. **The order rule** — favourites at the top by last heard (newest first), then everyone else by last heard,
   ties stable, and the EST-449 search filter keeps that order inside its matches — is pinned in the host tests.

### Results (real exit codes, 2026-11-19)

- **Host tests: `sh test/host/run.sh` exit 0** — **26 compiled suite summaries, 14,575 checks, 0 failures**
  (`paperclip-pilot/sweep/est455-host.log`; the ASan/UBSan repeats are included in that count, as in the previous
  entries). The new stage is `test_node_menu`: **95 checks, 0 failure(s)**.
- **ARMED — two mutations, both red, reverted before the final runs above:**
  - `core/mc_store.cpp` tie-break `>` → `>=` (a later row wins an equal `heard_at`) →
    `FAIL .../est455-arm/test_node_menu.cpp:205  order[0] == 3 (got 5, want 3)`, 77 checks, 1 failure.
  - `core/node_menu.h` DM condition `hasKey && advType == kNodeMenuAdvChat` → `hasKey` (a repeater gets DM) →
    `FAIL .../est455-arm/test_node_menu.cpp:104  om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows) == 3 (got 4, want 3)`,
    76 checks, 1 failure.
- **Build: `make build` exit 0.** `application.bin` **0x17b190 = 1,552,784 B**; `make size`: **Flash
  1,442,278 B used**, total image **1,552,376 B**, **1,724,016 B free (52.6 %)** of the **3,200 KiB
  (3,276,800 B)** budget. Versus EST-454: **+3,936 B** in the bin. `kAppVersion` stays `50` (the ticket's number).
- **Preview:** `shots/2026-11-19-nodes-favourites.png`, **800×480**, rendered by the one tool
  (`python3 tools/preview_tanmatsu.py nodes_favourites`): two favourites at the top with the gold dot and the menu
  open on the third node (a client, so all four rows are honest). Every fixture is invented (rule 9).
- **Files (new):** `core/node_menu.h` (rows per type, favourite notice, focus follow), `test/host/test_node_menu.cpp`,
  `shots/2026-11-19-nodes-favourites.png`.
  **Files (edited):** `main/main.cpp` (menu state/keys and the DM/remove/toggle side effects, the on-page confirm),
  `main/nodes_page.{h,cpp}` (the sheet, the gold dot, the hints/detail words), `main/mc_link.{h,cpp}`
  (`contactsSetFavourite`, the structural dirty flag), `core/contact_admin.h` (`contactSetFavourite`), `test/host/run.sh`,
  `tools/preview_tanmatsu.py`, `docs/PROGRESS.md` (this entry).
- **NOT checked (named):** nothing was flashed, installed or committed; the sheet, the re-sort and the focus follow
  were host-tested and rendered, not yet seen on glass. The NVS write is the EST-437 rule, not a write inside the key
  handler: the toggle changes RAM under the store lock and raises the structural flag, and the main loop's
  `contactsPersistTick()` rewrites the `mcct` blob on that pass (the same path remove/clear use). The row mark is
  the gold **dot**, not a star: `tools/mkfonts.py` ships ASCII + U+00A0..U+017F only and U+2605 is not in the faces.

## 2026-11-19 · S514 (EST-454) · own key at the top = first row

> Written by **DeepSeek** for ticket **EST-454**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts after EST-453 (v49) is in the tree and keeps its work. **Version: 50.** The ticket
> asked for this entry FIRST; the results below are filled in after the real runs.

### The rule

Filip's extension of EST-444: the page's own symbol key, pressed while that page is ALREADY at its top level,
now moves the selection to the FIRST row and scrolls the list to the top. Inside an inner level the same key
still only closes that level (one press, one step) and does not also jump.

- **Nodes** (square): the first node in the filtered list (row 0); the search bar is NOT focused and the typed
  query stays. Inside the search bar the key leaves the bar (the inner level) and keeps the focus where it was.
- **Chat** (circle): the first channel card — Channels column (0), row 0 — and BOTH column scrolls reset to 0.
  Inside an open channel/DM the key closes it and leaves the focus alone; a second press goes to row 0.
- **RPTR** (diamond): the same rule because it has a list — the first found/contact row; inside login/status the
  key returns to the list (one step) without jumping.
- **Map** (cloud): no list, so unchanged. **Settings**: reachable from `,` and the Launcher bar, not from a
  symbol key, so it has no own key to change; its frame is unchanged.

### Where it lives

The router stays generic. EST-444's `Module::onTop` answer already says whether an inner level was closed; the
`false` ("already at the top") branch is now where the module resets its own focus. The reset arithmetic is a
pure core header, `core/own_top.h`, so the host test drives the SAME rule the panel runs; `main/main.cpp`'s
`g_om_lite.onTop` only copies its local state in and out and performs the close side effects (mark read, logout).

### Results (real exit codes, 2026-11-19)

- **Host tests: `sh test/host/run.sh` exit 0** — **26 compiled suites, 16,558 checks, 0 failures** (the sum of
  every stage's own summary in `paperclip-pilot/sweep/est454/host-final.log`; the ASan repeats included). The
  core/router suite is **275 checks, 0 failures** (was 229, so the new test adds 46).
- **ARMED (the EST-444 no-op is what fails):** the same `test_core.cpp` built against an
  `own_top.h` whose two functions only close an inner level and never reset the focus (`sweep/est454/arm/`) →
  **exit 1**, `FAIL test_core.cpp:512  g_om_col == om::mc::kColChannels (got 1, want 0)`, 235 checks,
  1 failure. With the real header: 275 checks, 0.
- **Build: `make build` exit 0.** `application.bin` **0x17a230 = 1,548,848 B**; `make size`: **Flash
  1,438,350 B used**, total image **1,548,448 B**, **548,304 B free (`0x85dd0`, 26 %)** of the 2 MiB
  (`0x200000`) app partition. Versus EST-453: +364 B flash/image, +352 B bin. `kAppVersion` is `50`.
- **Files (new):** `core/own_top.h` (the two reset rules).
  **Files (edited):** `main/main.cpp` (the onTop hook and the state it now resets), `test/host/test_core.cpp`
  (the rule through the module hook), `core/settings.h` (`kAppVersion` 50), `core/module.h`, `core/router.h`,
  `core/router.cpp`, `core/core.cpp` (comments only: the old "at the top nothing changes" text is no longer
  true; the router still only names the page), `docs/PROGRESS.md` (this entry).
- **NOT checked (named):** nothing was flashed, installed or committed; the focus jump and the scroll are
  host-tested, not seen on glass. Settings has no own symbol key in the five-key table, so the "same rule" has
  nothing to act on there; Map has no list.

## 2026-11-19 · S514 (EST-453) · RPTR part 1: scan, login, status

> Written by **DeepSeek** for ticket **EST-453**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts on `fc9b199` (v48) plus EST-452's uncommitted tree. **Version: 49.** The ticket
> asked for this entry FIRST. Part 1 is READ-ONLY: no `set` command is built, sent or reachable from the page.

### What this builds (items in the ticket's order)

1. **Pure core** `core/rptr_model.*` (found list, login state machine + deadline, status fields and units,
   formatting) and `core/rptr_wire.*` (the four packets, ported byte for byte from upstream). Host tests in
   `test/host/test_rptr.cpp`, with the expected frames from an independent Python oracle, plus a junk-input pass
   under AddressSanitizer/UBSan.
2. **Page::Rptr**: the diamond key's page — the found/contact list, Enter -> login sub-page (password shown as
   dots), then the STATUS view with the repeater's own fields. All radio work goes through the existing tx queue
   and the radio task; the page only queues requests.
3. **Settings > OM Lite > RPTR**: `Remember passwords` On/Off (default On) and `Forget all passwords` behind the
   shared confirm sheet.
4. **Preview shots** `shots/2026-11-19-rptr-list.png` and `shots/2026-11-19-rptr-status.png`.

### Results (real exit codes, 2026-11-19)

- **Host tests: `sh test/host/run.sh` exit 0** — 25 compiled suites, **14,434 checks, 0 failures**. The new stage
  is `test_rptr`: **101 checks, 0 failure(s)** in both the -O2 build and under AddressSanitizer/UBSan (the
  vendored Ed25519 C is ASan-only there, so its own S501-era UBSan left-shift reports cannot bury our parsers;
  our files get both sanitizers). The junk pass feeds 4,000 arbitrary byte strings into the frame decoder and
  every parser.
- **ARMED (the old code is what fails):**
  - scan header mutated from DIRECT to FLOOD → `FAIL test_rptr.cpp:104  frameEquals(frame, n, kScan)`
  - `statusParse` uptime offset 20 → 16 → `FAIL test_rptr.cpp:201  st.uptimeSec == 90061u (got 3600, want 90061)`
  Both mutations were reverted before the final runs above.
- **Build: `make build` exit 0.** `make size`: **Flash 1,437,986 B used**, total image **1,548,084 B**
  (`application.bin` 0x17a0d0 = 1,548,496 B), **vs the 3,200 KiB (3,276,800 B) budget: 1,728,816 B free
  (52.8 %)**. `kAppVersion` is `49`.
- **Preview:** `shots/2026-11-19-rptr-list.png` and `shots/2026-11-19-rptr-status.png`, both 800×480, rendered by
  the one tool `tools/preview_tanmatsu.py rptr_list rptr_status`; every fixture is invented (rule 9).
- **Files (new):** `core/rptr_model.{h,cpp}`, `core/rptr_wire.{h,cpp}`, `main/rptr_page.{h,cpp}`,
  `main/rptr_link.{h,cpp}`, `test/host/test_rptr.cpp`.
  **Files (edited):** `core/mc_wire.h` (kTypeResponse/kTypeControl), `core/CMakeLists.txt`, `main/mc_link.{h,cpp}`
  (two doors + the receive hooks), `main/main.cpp` (RPTR page state/draw/keys, the two Settings rows and the
  confirm target), `main/CMakeLists.txt`, `test/host/run.sh`, `tools/preview_tanmatsu.py`.
- **NOT checked (named):** nothing was flashed, installed or committed. No login or status has been on the air
  yet — that is Claude's test with Filip. The login/status deadline uses a fixed 8 s + 2.5 s estimate because
  this app has no upstream `getEstAirtimeFor` model; it is deliberately slower than a guess, never shorter than
  the L2's 8 s floor. The password is stored in NVS `mcrp` exactly as the L2 stores it (plain, because auto-login
  must read it back); it is never logged or drawn. Repeater CONTACTS from adverts are merged into the same found
  list every 2 s, so their row index and a scan answer's row index are the same one loginStart() uses.
- **Nothing of part 2 is here:** no `set` command is built, sent or reachable.

| Packet | Built / read from |
|---|---|
| Scan request (zero-hop CONTROL) | `Mesh.cpp:621-633` createControlData, `Mesh.cpp:717-726` sendZeroHop, `rptr_link.cpp:478-491` payload |
| Scan answer | `rptr_link.cpp:536-556` (type 0x90, snr x4, tag, key) |
| Login (ANON_REQ) | `BaseChatMesh.cpp:603-629` sendLogin, `Mesh.cpp:512-538` createAnonDatagram |
| Login answer | `rptr_model.cpp:168-193` loginParse (RESP_SERVER_LOGIN_OK layout) |
| Status request (REQ) | `BaseChatMesh.cpp:690-715` sendRequest(req_type), `Mesh.cpp:490-509` createDatagram |
| Status reply | `rptr_model.cpp:248-292` statusParse (56-byte RepeaterStats) |

## 2026-11-19 · S514 (EST-452) · FatFs fast seek for the packs: a z16/z17 fill stops re-walking the FAT per tile

> Written by **DeepSeek** for ticket **EST-452**: fenced Paperclip run, no session start/end, nothing
> installed, flashed or committed. Starts on `fc9b199` (v48), tree clean. **Version: 49.** The ticket asked
> for this entry FIRST; this run wrote it after the code and the tests, with the real results below (the
> EXPECTED numbers were worked out before the measurements and are left in, so the glass can settle them).

### What this builds (items in the ticket's order)

1. **A cluster link map (CLMT) on the pack's `FIL`, built at open.** `CONFIG_FATFS_USE_FASTSEEK` was already
   1 (`build/tanmatsu/config/sdkconfig.h:913` -> `ffconf.h:42` `FF_USE_FASTSEEK 1`), but FatFs only takes its
   short seek path when the FIL carries `cltbl` (`ff.c` `clmt_clust()`); with `cltbl == 0` it follows the FAT
   chain cluster by cluster. `main/map_link.cpp` now, at the end of `ensurePack()` (after the pack's own
   open log line): allocates the table in PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`), sets
   `tbl[0]` = its size in DWORDs, sets `fil.cltbl`, and calls `f_lseek(&fil, CREATE_LINKMAP)`.
   - **One retry, exact size.** On `FR_NOT_ENOUGH_CORE` the driver has written the DWORDs it needs into
     `tbl[0]`; the table is detached and freed, and one more is allocated at exactly that size. That retry
     cannot loop: with the size the driver asked for, its own `ulen <= tlen` test passes by construction.
   - **Log at open:** `z17: fast seek on, link map N fragment(s), 2056 B` — the fragment count is the value
     the driver put back in `tbl[0]` (`2 + 2 * fragments`), the bytes are the allocation. A contiguous
     pack is 1 fragment; a `the pack needs a ... retrying once` line would mean the pack is in more than
     256 pieces.
   - **Failure is a slower map, never a broken one:** no PSRAM, a driver error, or a table the previous pack
     still held leaves `cltbl` null and the pack reads exactly as in v48, with one warning logged.
   - **Freed in `closePack()`**, which runs on every reopen (a zoom across a band boundary) and at teardown:
     `cltbl` is detached from the FIL first, then the owner hands the pointer back and it is freed once. A
     second close frees nothing (the state is cleared in the owner, not in the caller).
   - The rules (sizes, the retry decision, the exactly-once hand-back) are pure and live in
     **`core/map_clmt.h`**; the FatFs calls themselves stay in `map_link.cpp`. **No FatFs is faked on the
     host** — a fake would only test the fake.
2. **The measurement line is unchanged on purpose**, because it is enough: `fill z16: 12 tile(s) in 945 ms
   (34 card reads, 2.8/tile, hardware decode)` already carries the time, and the boot probe still logs
   `z17 tile ... in X ms`. ⚠️ **`R` will not move:** `g_card_reads` counts INDEX reads only (2-4 a tile);
   the FAT reads that the seek spends were never counted, so the improvement shows in **ms**, not in `R`.
3. **Host tests, ARMED** (below): the sizing arithmetic against `ff.c`'s own protocol, the retry decision,
   and the free-on-close — the three things that are pure.

### What the 945 ms is made of, and what to EXPECT on the glass (not yet measured)

Read, not assumed: the hardware decoder needs **2 ms a tile** (`decoder check: ... 2 ms vs sw 41 ms` in
v43's boot log), and a whole 12-tile z16 fill issues only **34 card reads**, i.e. 2.8 index reads a tile —
a few ms. So of z16's **937-945 ms for 12 tiles (78 ms a tile)** and z17's **966 ms for 7 tiles (138 ms a
tile)**, almost everything is the seek: with no CLMT, `f_lseek` to a lower offset starts at the file's first
cluster and walks `get_fat` to the target. slo17 is 3,590,722,268 B; at 32 KB clusters that is ~110k FAT
entries = 440 KB of FAT = ~860 sectors of 512 B **for every backward seek**. That is the same order as the
465 ms boot tile (one cold walk from fptr 0) and the 78-138 ms a tile in a fill (a warm-ish walk).

Expected after this change (the fills are centre-out, so most of their seeks are backward):

- **z17 fill (7 tiles): 966 ms -> ~150-250 ms.** Per tile: the compressed tile read (tens of KB) + 2 ms of
  decode + the 30-35 index reads; the seek becomes a scan of a handful of (length, start) pairs.
- **z16 fill (12 tiles): 937-945 ms -> ~200-300 ms.** Same arithmetic, slightly smaller tiles.
- **z15 fill (8 tiles, the 991 MB `slo` pack): 571-579 ms -> ~120-200 ms.** ~30k clusters there, so the
  walk is ~2.8x shorter to begin with.
- **z12-z14: better, but less (they are already 15-30 ms a tile)** — their seeks are mostly short and
  forward, which normal `f_lseek` already handles cheaply.
- **The boot probes should be FLAT, and that is the honest part:** building the table costs ONE full walk,
  and today the first seek after each open costs exactly the same walk (it starts from cluster 0 to reach
  any offset). At boot the packs are opened and one tile is read per open, so the walk only MOVES from the
  seek into the open: `z17 tile ... 465 ms` is expected to stay ~465 ms. The second tile in a pack is the
  first that gets cheaper. If the boot gets materially slower, the cause is not this ticket's arithmetic.

### Results (real exit codes)

- **Host:** `sh test/host/run.sh` -> **exit 0, 15,861 checks, 0 failures** (was 13,805 on `fc9b199`; the
  map stage is **12,449**, was 10,393, **+2,056**). Log: `paperclip-pilot/sweep/est452/host-final.log`.
- **ARMED twice**, on copies of `core/map_clmt.h` in `sweep/est452/` (the production file was not touched):
  1. `kClmtItemsPerFragment` 2 -> 1 (the plausible misreading "one DWORD per fragment") -> red, exit 1:
     `FAIL test_map.cpp:654  om::map::clmtItemsForFragments(1) == 4 (got 3, want 4)`, and the retry size
     follows it down: `FAIL test_map.cpp:691  r.items == 1802 (got 902, want 1802)` (10,420 checks,
     2 failures). Log: `sweep/est452/arm-clmt.log`.
  2. The retry guard `needed > tried` -> `needed >= tried` (retry on no progress, i.e. a loop) -> red,
     exit 1: `FAIL test_map.cpp:696  !om::map::clmtRetry(needed, needed).retry` (12,439 checks, 1 failure).
     Log: `sweep/est452/arm-retry.log`.
- **Build:** `make build` -> **exit 0**, **0 warnings**. `application.bin` **0x175ff0 = 1,531,888 B
  (1,496.0 KiB)** = **46.7 % of the 3,200 KiB ceiling**; the build's own check says `0x8a010` (**27 %**)
  free in the 2 MB app partition. `map_link.cpp` rebuilt (`build-final.log:375`) and the object carries the
  new lines (`strings .../map_link.cpp.obj | grep "fast seek"` -> all six), which is the proof the
  `#if FF_USE_FASTSEEK` block was actually compiled in. Log: `sweep/est452/build-final.log`.

### Why this cannot corrupt a tile even if the sizing were wrong

FatFs never accepts an incomplete table: it walks the chain, writes what fits, and returns
`FR_NOT_ENOUGH_CORE` with the required `ulen` if `ulen > tlen`. So our first-attempt size can only decide
whether ONE extra walk happens; it can never leave a short table in use, which would have shown up as
`FR_INT_ERR` on the next seek. The host test pins our numbers to `ff.c`'s arithmetic anyway.

### Files

- **new** `core/map_clmt.h` — the CLMT's numbers (`clmtItemsForFragments` / `clmtFragmentsForItems`), the
  first-try size (256 fragments = 2,056 B), the 1 MB ceiling, `clmtRetry()` (exact size, no-progress and
  ceiling guards) and `ClmtTable` (`adopt()` / `drop()`, at most one live table, freed exactly once).
- `main/map_link.cpp` — `attachLinkMap()` (build the table, one retry, log, give up quietly),
  `OpenPack::clmt`, the call at the end of `ensurePack()`, and the detach+free in `closePack()`.
- `test/host/test_map.cpp` — `test_clmt_sizing` (2,019 checks incl. a 0..1000 round trip),
  `test_clmt_retry` (15) and `test_clmt_ownership_frees_once` (22); `#include "map_clmt.h"`.
- `core/settings.h` — `kAppVersion = "49"`.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** No device, no serial port.
- **The fragment counts of the real packs are unknown until the glass**; the arithmetic says "a handful",
  and 256 fragments is the first try, so the retry path is unlikely to be exercised in practice (which is
  why it is host-tested instead).
- **The real per-tile numbers** above are expectations from the v43 log, not measurements; that is the next
  step, and it needs Claude on the device.
- The two Settings preview shots still say version **48**; they were not re-rendered, because this ticket is
  code + host tests + build only.

### Next step

On the glass, one boot and one walk through the zooms: read the new `zNN: fast seek on, link map N
fragment(s), 2056 B` line for each pack (N should be a handful; a second `the pack needs a ... retrying once`
line means the packs are fragmented and is worth reporting), then compare `fill z16` and `fill z17` against
v43's 937-945 ms / 966 ms. Expect the fills to drop by most of their time and the boot `z17 tile` line to
stay where it is.

## 2026-11-19 · S514 (EST-451) · Volume with +/-, and the picker keeps its gold frame on screen

> Written by **DeepSeek** for ticket **EST-451**: fenced Paperclip run, no session start/end, nothing
> installed, flashed or committed. Starts on `38e78e7` ("Sound volume: the L2 curve"); the curve is
> untouched. **Version: 48.** The ticket asked for this entry first; this run wrote it after the code and
> tests, with the real results below.

### What this builds (items in the ticket's order)

1. **Volume by '+' / '-'** on Settings > General > SOUND > Volume. '+' (and '=' where '+' needs Shift)
   raises ten, '-' lowers ten, clamped 0..100 (`soundcfg::stepVolume`). Each real change writes the store,
   applies `audioout::setVolume`, and previews the "Sent" sound through the existing 400 ms gate
   (`audioout::playEvent`, which reads the just-stored level). At a clamp nothing is written and no
   preview plays. Enter still cycles 0→10→…→100→0. The row now reads `- 60 % +`, and its hint bar shows
   `+/- volume`.
2. **The picker's scroll is the root's scroll.** The defect was the body HEIGHT, not a second rule: the
   three list sub-pages (sound picker, Remove a contact, the chat channel/scope lists) passed
   `kSetBodyH + kSetTabH` = 386 px to `settingsSubPageKey`, while `drawSettingsRows` drew a sub-page body
   of `kTmH - kTmHintH - (head_bottom + 1)` = 385 px. The frame at the bottom was scrolled 1 px past the
   drawn body and then skipped ("no text on the chrome"), so the gold frame vanished. One function pair now
   owns the window (`settingsListBodyTop` / `settingsListBodyHeight` in core/settings.h), the renderer and
   all three key sites use `kSetSubBodyTop` / `kSetSubBodyH`, and the picker (which already called
   `settingsSubPageKey`) now gets the same 385 px it draws.
3. **Host test + ARM:** a 26-element list (2 sections) longer than 385 px: Down from the last fully visible
   row moves one row and scrolls just enough for its bottom to BE the drawn bottom; a second Down advances
   exactly 34 px; Up mirrors it at the top; the last row is fully inside the window. The ARM restored the
   old 386 px arithmetic and failed on the symptom (`got 386, want 385`).

### Results (real exit codes)

- **Host:** `sh test/host/run.sh` → **exit 0**, **13,805 checks, 0 failures**. Touched stages: Settings
  **291 checks** (was 275; +16), sound **183** (was 174 after the L2-curve ticket; +9). Log:
  `paperclip-pilot/sweep/est451/host-final2.log`.
- **ARMED** (copy in `sweep/est451/arm-scroll/`, only the `+ 1` in `settingsListBodyTop` removed; the
  production file was not touched): red → `FAIL test_settings.cpp:739
  tm::settingsRowTop(...) - scroll + 34 == kDrawH (got 386, want 385)`, 280 checks, 1 failure, exit 1.
  Green after restoring: 291 checks, 0 failures, exit 0.
- **Build:** `make build` → **exit 0**. `application.bin` **0x175bf0 = 1,530,864 B (1,495.0 KiB)** —
  **46.7 % of the 3,200 KiB ceiling**; the build says `0x8a410` (**27 %**) free in the 2 MB app partition.
  `main.cpp`, `settings_page.cpp` and the audio code compiled with no warnings. Log:
  `sweep/est451/build-final2.log`.
- **Preview:** `python3 tools/preview_tanmatsu.py settings_sound_picker` → exit 0,
  `shots/2026-11-19-sound-picker-scrolled.png` (800×480). The Message picker is scrolled into DUO; the
  focused "Wood Warm" row (value `preview`) has its gold frame ON the last visible line with more rows
  below and the thumb showing the position. `settings_general` and `settings_sound` were re-rendered so
  their Volume row reads `- 60 % +` and the version is 48.

### Why the hint bar drops two generic chips on that row

Measured from `sans_hint11.bin` advances: the five standard Settings chips already run to x=460 while the
symbol cluster starts at x=397, so `esc back` is clipped today. Six chips cannot fit. On the Volume row the
bar therefore carries the four that matter there — `, settings`, `+/- volume`, `ret change`, `esc back`
(361 px, all inside the limit). The other rows keep the standard five-chip set unchanged.

### Files

- `core/sound_events.h` — `stepVolume()`: ten at a time, clamped 0..100; `nextVolume()` untouched.
- `core/settings.h` — `settingsListBodyTop()` / `settingsListBodyHeight()`, the one LIST-sub-page window.
- `main/settings_page.h` — `kSetSubBodyTop` / `kSetSubBodyH`, and `SettingsView::volume_row`.
- `main/settings_page.cpp` — the list sub-page uses the shared metrics; the Volume row's hint bar.
- `main/main.cpp` — `- N % +` value, `adjustSoundVolume()` (store → setVolume → Sent preview through the
  gate), '+'/'='/'-' handled only on the Volume row, all three sub-page key calls use `kSetSubBodyH`.
- `test/host/test_settings.cpp` — `test_sub_page_list_scroll` (16 checks incl. the geometry pin).
- `test/host/test_sound_events.cpp` — 9 `stepVolume` checks.
- `tools/preview_tanmatsu.py` — `settings_sound_picker`, Volume value/version in the two existing shots.
- `shots/2026-11-19-sound-picker-scrolled.png` (**new**), `2026-11-19-settings-general.png`,
  `2026-11-19-settings-sound.png` (re-rendered).
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** No device, no serial port, no server.
- The '+' / '=' / '-' key codes on Filip's keyboard: the handler accepts `'+'`, `'='` and `'-'` as ASCII,
  but only the glass can show which of them the Tanmatsu sends.
- The 400 ms preview gate and the new level were not heard: the store/curve path is the same one EST-450
  and the L2-curve ticket already use, but the perceived level after `bsp_audio_set_rate(16000)` needs ears.
- The hint-bar fit is arithmetic over the real font's advances, not a panel render.
- Every other Settings list shares the fixed height, but only the picker has a host test; the chat/contact
  lists are the same call with the same constant.

### Next step

A device session: on the Volume row press '+' and '-' through 0 and 100 (preview once per change, clamped at
both ends), then scroll the Message picker to the Off row and confirm the gold frame stays on the last
visible line; check the same on Remove a contact and a long channel list.

## 2026-11-19 · S514 (EST-450) · Sounds: message, DM, sent, new node, error

> Written by **DeepSeek** for ticket **EST-450**: fenced Paperclip run, no session start/end, nothing
> installed, flashed or committed. Starts on `3dbf387` (v46) plus EST-445…449 in the tree and keeps their
> work. **Version: 47.** This entry is opened FIRST, as the ticket asks; Results and files are filled in as
> the work finishes.

### What this builds (items in the ticket's order)

1. **ONE synth source for both devices.** `core/sound_events.h` includes the L2's
   `firmware/src/sound_synth.h` across the tree (the `core/mc_history` pattern) and
   `core/CMakeLists.txt` compiles `../../firmware/src/sound_synth.cpp`; nothing is copied or forked.
2. **One owner of the I2S channel** (`main/audio_out.*`): a low-priority audio task with its own queue
   (requests copied by value, a full queue drops), a PSRAM phrase buffer, the amp ON only while playing,
   and the stack low-water mark logged like the radio task's.
3. **Events** in the L2's vocabulary: message, DM, sent, new node, error. Boot is OFF by default here; the
   L2's own default is Bell TK-TK with the welcome sound enabled. Boot is raised once at start, so it
   stays silent at the default and plays only a sound Filip chose. At most one sound per 400 ms.
4. **Settings, General tab, SOUND section:** Sound On/Off, Volume 0–100 in steps of 10, one row per event,
   and a picker grouped by the L2's shapes. First Enter previews a sound, the second chooses it. NVS
   namespace `"mcsnd"`, library ids stored exactly as the L2 stores them.
5. **Host tests** for the mapping, the 400 ms gate, the echo/duplicate/restored decision, the storage round
   trip and unknown-id redirection. The synth is already covered by the L2's `test_est369.cpp`.
6. **Preview:** `shots/2026-11-19-settings-sound.png`.

### Results (real exit codes)

- **Host:** `sh test/host/run.sh` → **exit 0**, **13,953 checks, 0 failures**. The new sound stage is **67
  checks**; the Settings stage is 275 (273 in EST-449's log — the existing stage, not a new one). Log:
  `paperclip-pilot/sweep/est450/host-final6.log`.
- **ARMED, twice** (backed up, broken, captured, restored and diffed identical each time; logs beside the
  host log):
  1. The 400 ms gate made strict (`>=` → `>` in `rateDue`) → red:
     `FAIL test_sound_events.cpp:90  tm::soundcfg::rateDue(1000, 1400)` (63 checks, 1 failure).
  2. Boot's Off default removed (it fell back to the L2's Bell TK-TK) → red:
     `FAIL test_sound_events.cpp:77  s.ids[(int)Event::Boot] == om::sound::kSoundOff (got 64, want 255)`
     `FAIL test_sound_events.cpp:118  !tm::soundcfg::shouldPlay(tm::soundcfg::defaultSettings(), Event::Boot, false, false, false, true)`
     (62 checks, 2 failures).
- **L2 suite:** `cd ../firmware && sh test/host/run.sh` → **exit 0**. The synth is the L2's own
  `firmware/test/host/test_est369.cpp` stage (built with `src/sound_synth.cpp` at
  `firmware/test/host/run.sh:236-243`), so no waveform test is duplicated here. Log:
  `paperclip-pilot/sweep/est450/l2-host-final.log`.
- **Build:** `make build` → **exit 0**. `application.bin` **0x1757d0 = 1,529,808 B (1,494.0 KiB)** —
  **46.7 % of the 3,200 KiB per-app ceiling**; the build's own check says `0x8a830` (**27 %**) free in the
  2 MB app partition. The first build log proves the shared source is compiled from the L2 tree:
  `__idf_core.dir/home/slofi/Projects/pocket-mesh/firmware/src/sound_synth.cpp.obj`
  (`est450/build-1.log:318`), plus `audio_out.cpp.obj` and `sound_store.cpp.obj`. Final log:
  `est450/build-final5.log`.
- **Preview:** `python3 tools/preview_tanmatsu.py settings_sound` → exit 0,
  `shots/2026-11-19-settings-sound.png` (800×480). The SOUND section is scrolled to the body top: Sound On,
  Volume 60%, Message Soft Hi, Direct message Wood Warm, Sent Soft Tick, New node Soft Lo, Error Bell Fall,
  Boot Off. The General preview was re-rendered too (SOUND rows, Automatic advert Off, version 47).

### BSP conflicts checked (cited, not assumed)

- `bsp_device_initialize()` **already calls `bsp_audio_initialize()`** at
  `managed_components/badgeteam__badge-bsp/common/badge_bsp_device.c:84`, after the primary I2C bus is
  created at `:39`. `audioout::begin()` therefore **takes the published I2S handle**
  (`bsp_audio_get_i2s_handle`) and only calls `bsp_audio_initialize()` itself as a one-time fallback when
  that handle is missing — calling it again would create a second I2S channel and repeat the codec
  bring-up.
- The audio init uses the same primary I2C bus (`targets/tanmatsu/badge_bsp_audio.c:70`) and configures
  the ES8156 at `0x08` (`:77-84`, `targets/tanmatsu/tanmatsu_hardware.h:62`). Our app never touches I2C
  directly, so there is no second owner.
- The amplifier is a **CH32 coprocessor command** (`badge_bsp_audio.c:103-107`,
  `tanmatsu_coprocessor_set_amplifier_enable`) — the same coprocessor this app power-cycles for the C6.
  We touch it only after boot is past, on the audio task, ON for a phrase and OFF after.
- The I2S slot is fixed **16-bit STEREO** (`badge_bsp_audio.c:32-34`), so the mono synth is duplicated
  into L/R. The BSP's default clock is **44,100 Hz** (`initialize_i2s(44100)`, `:84`); we call
  `bsp_audio_set_rate(16000)` (`:64-67`) because `sound_synth.h` fixes `kRate = 16000` and its low-pass /
  alias guard assume an 8 kHz Nyquist. At 44.1 kHz every phrase would be ~2.76× fast and high.

### Files

- **new** `core/sound_events.h` — events, L2 defaults (Boot Off here), the 400 ms gate, the
  echo/duplicate/restored/notice policy, the 9-byte settings codec and the kRedirect rule.
- **new** `main/audio_out.h/.cpp` — the one I2S owner: PSRAM phrase buffer (internal quarter fallback),
  3-slot value queue with zero-timeout drop, 400 ms gate, amp ON/OFF, stack low-water log.
- **new** `main/sound_store.h/.cpp` — NVS namespace `"mcsnd"`, key `"cfg"`, the pure blob only.
- **new** `test/host/test_sound_events.cpp` — the 67 checks and the two arm targets.
- **new** `shots/2026-11-19-settings-sound.png`.
- `core/CMakeLists.txt` — compiles `../../firmware/src/sound_synth.cpp`.
- `core/settings.h` — `kAppVersion = "47"`.
- `main/CMakeLists.txt` — `audio_out.cpp`, `sound_store.cpp`.
- `main/main.cpp` — SOUND section (Sound, Volume, six event rows), the picker sub-page, preview-then-choose,
  the Sent/Error calls on the Chat send result, `soundstore::begin()` + `audioout::begin()` after the SD
  mount and the C6 link, version 47.
- `main/mc_link.cpp` — Message after a live channel text, Dm after a live DM, NewNode under the same
  `before < 0 && g_new_node_notice` condition as the notice, Error on a refused radio send and on a DM
  with no confirmation.
- `test/host/run.sh` — the sound stage (links the L2's real `sound_synth.cpp`).
- `tools/preview_tanmatsu.py` — `settings_sound`, and the General preview's SOUND rows/v47.
- `docs/PROGRESS.md`, `shots/2026-11-19-settings-general.png` (re-rendered).

### What could NOT be checked

- **Nothing was installed, flashed or committed.** No device, no serial port, no server.
- The sounds were **not heard on the glass**: the 16 kHz codec path after `bsp_audio_set_rate`, the 30 ms
  amplifier settle (the L2 uses 250 ms after its ES8311 amp — a different chip), the tail silence, and the
  perceived 400 ms spacing all need Filip's ears.
- NVS after a reboot: the 9-byte `mcsnd/cfg` blob and the chosen ids across a power cycle.
- The event triggers against real mesh traffic (a live channel text, a DM, an advert that adds a contact,
  a refused send, a DM that never confirms). The decisions are host-tested; the wiring is compiled.
- The picker's sub-page scroll with the full 41-sound library on the panel (host arithmetic only).

### Next step

A device session with Filip: play each event once and each picker preview, check no click/hiss at the amp
transition and that a 401 ms burst of adverts produces one sound, then reboot and confirm the choices
survive.

## 2026-11-19 · S514 (EST-449) · Nodes search bar

> Written by **DeepSeek** for ticket **EST-449**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts AFTER EST-445/446/447/448 are in the tree and keeps their work. **Version: 44.**
> Ports the L2's S508 Nodes search (`firmware/src/ui/nodes_search.h`) to the 800×480 Nodes page, with the two
> corrections Filip made on the L2's glass: **ONE input field**, and **text never on the field's border**.

### What this changes

- **A search bar as the TOP ROW of the Nodes list** (always visible): dim `search nodes` placeholder while it is
  empty, the query once typed, and `N of M` on its right. No magnifier glyph: the six VLW faces carry Latin-1 +
  Latin Extended-A only (319 glyphs; U+2315 / U+1F50D are absent), so per the ticket the bar has none.
- **Focus:** the bar is focusable like a row. **Up from the first node**, or the **`/` key anywhere on the Nodes
  page**, puts the focus in it. **Down from the bar** lands on the first MATCH (and stays in the bar when nothing
  matches). **Esc** clears a non-empty query first; a second Esc (query now empty) leaves the bar for the list.
- **Typing (bar focused):** printable keys append byte-for-byte under the composer's own UTF-8 rule (one event is
  one byte; Backspace drops the whole trailing sequence), the list filters **LIVE**, and `N of M` tracks it.
  While the **LIST** has focus the keys keep their meaning (`a` = advert now, `,` = Settings, Esc = back).
- **Session-only state:** the query lives in the main loop's variables, never in NVS; leaving the Nodes page and
  coming back keeps it, the same lifetime as `nodes_focus`.
- **Empty filtered result:** `No matching nodes` is drawn in the LIST area; `No nodes yet` still means the contact
  table itself is empty.
- **Pure + host-tested** (new `core/nodes_search.h`, `test/host/test_nodes_search.cpp`): the matcher (case,
  substring, UTF-8 name, empty query = all), the `N of M` text, the filtered order, the focus moves (Up from the
  first row → bar, Down from the bar → first match, Esc twice) and `a` being the advert key only while the list
  has focus.

### Results

- **Host:** `sh test/host/run.sh` → **exit 0**, **14,053 checks, 0 failures** (before: 13,953; **+100** for the
  new stage). `test_nodes_search.cpp` covers the matcher (case, substring, UTF-8 name, empty query = all), the
  `N of M` text (incl. the 999 clamp and a tiny buffer), the filtered order over the page's own order, the UTF-8
  typing/Backspace rule, the focus moves (Up from the first row → bar, Down from the bar → first match, clamped
  at the last), Esc twice, `a` only while the list has focus, and the `/` shortcut.
- **ARM two** (broke the real tree twice, captured the FAIL lines, then restored — `core/nodes_search.h` was
  diffed identical to its backup afterwards):
  1. case fold removed from `nodeSearchFold`:
     `FAIL test/host/test_nodes_search.cpp:80  om::ui::nodeSearchMatches("RPT-Kamera", "KAM")`
     `FAIL test/host/test_nodes_search.cpp:143  om::ui::nodeSearchFilter(&s, order, 6, "ERA", out) == 2 (got 0, want 2)`
  2. `nodesFocusUp` no longer enters the bar from the first row:
     `FAIL test/host/test_nodes_search.cpp:214  bar`
- **Build:** `make build` exit 0. `application.bin` **0x16fe50 bytes (1,506,896 B, 1471.6 KiB)**, well under the
  **3,200 KiB per-app ceiling**; the build's own check says `0x901b0` (**28 %**) free in the 2 MB app partition.
- **Preview:** `shots/2026-11-19-nodes-search.png` (800×480, host render): query `era` in the focused bar,
  `2 of 6`, and the two matches (`TEST-Vera`, `RPT-Kamera`) with the first one's detail pane.
- **Files:** new `core/nodes_search.h`, `test/host/test_nodes_search.cpp`; changed `main/nodes_page.h`,
  `main/nodes_page.cpp`, `main/main.cpp`, `test/host/run.sh`, `tools/preview_tanmatsu.py`, `docs/PROGRESS.md`;
  new `shots/2026-11-19-nodes-search.png`.
- **Not done / honest limits:** nothing installed, flashed or committed; EST-445/446/447/448's uncommitted work is
  untouched and still builds. No magnifier glyph exists in the six faces (319 glyphs, Latin-1 + Latin Extended-A),
  so the bar draws none, as the ticket allows. The bar reads the **whole** `args_keyboard.utf8[]` sequence the BSP
  provides (so Alt_R+e lands as `é`, not `e`), and Backspace removes the whole trailing sequence; the Chat
  composer still types from `ascii` only — a pre-existing difference, not changed here. The query is session-only
  as asked: it is never written to NVS.

## 2026-11-19 · S514 (EST-447) · Settings part 3: Nodes + Map

> Written by **DeepSeek** for ticket **EST-447**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts AFTER EST-446 is in the tree; builds inside EST-448's OM Lite tab as the NODES and
> MAP sections (never as new top tabs) and reuses EST-445's rows, sub-pages and confirm sheet. **Version: 44.**
> Read the L2's Settings Nodes and Map pages (`firmware/src/ui/settings_layout.h`, `src/main.cpp` cases
> `SetPage::NodesRoot` / `SetPage::MapRoot`) and ported what applies to the Tanmatsu. This entry is open first,
> as the ticket asks; Results and files are filled in as the work finishes.

### What this changes

- **NODES section:** `Contacts N / 32` (read only) · **Remove a contact** (a list sub-page: name, type, last heard;
  Enter raises the shared sheet **"Remove <name>? A DM from it cannot be opened until it advertises again."** and
  Apply removes it from RAM and rewrites NVS `mcct` without it; its DM history stays on the card) · **Clear all
  contacts** (the same sheet, same consequence stated) · **New-node notice** On/Off, stored in NVS, default On,
  honoured where the "New node: ..." notice is raised.
- **MAP section:** **Start position** Ljubljana (default) / Last position (the last place the Map page was left,
  NVS, written at most once a minute and on leaving, never per keypress) · **Start zoom** 12..17, default 16,
  cycled with Enter · **Packs on the card**, read only: which of `slo` / `slo16` / `slo17` are present, their tile
  counts from the index, and the z17 file size.
- **RPTR:** unchanged, the one honest line "Repeater admin comes later".
- **Pure and host-tested:** contact removal + compaction and the clear-all, the encoded NVS `mcct` blob round
  trip without the removed rows, the start-position choice, the zoom range, and the last-position write rule with
  the time passed in.

### Results (real exit codes)

- `sh test/host/run.sh` → **exit 0**. **13,953 checks, 0 failures** (before: 13,856; **+97** for the new
  nodes-map stage). Per stage: core 229 · clock 91 · store 127 · history 84 · layout 62 · mc 115 · dm 45 ·
  mc-ASan 115 · dm-ASan 45 · dm_send 140 · dm_send-ASan 140 · map 10,393 · txqueue 93 · chat-cards 99 ·
  menu 28 · **settings 273** · channel-admin 162 · advert 162 · contact-blob 1,190 · channel-scope 94 ·
  dm guard 169 · **nodes-map 97**. Log: `paperclip-pilot/sweep/est447/host-final.log`.
- **ARMED, twice** (logs beside it; the rule restored and re-verified after each):
  - **Compaction removed** (`contactRemoveAt` drops the count without shifting, so the removed row's
    neighbour stays) → red: `FAIL test_nodes_map.cpp:80  "Bravo" == "Charlie"`, `:117` the same on the
    round trip (80 checks, 2 failures). `arm1-no-compaction.log`
  - **The 60 s floor made strict** (`>=` → `>`, so exactly one minute is not due) → red:
    `FAIL test_nodes_map.cpp:221  om::map::lastPositionWriteDue(false, true, 61000, 1000)` (92 checks,
    1 failure). `arm2-write-floor.log`
- `make build` → **exit 0**. `application.bin` **0x16f830 = 1,505,328 B (1,470.0 KiB)** — **45.9 % of the
  3,200 KiB per-app ceiling**; the build's own check says `0x907d0` (**28 %**) free in the 2 MB app
  partition. Log: `paperclip-pilot/sweep/est447/build-final.log`.
- `python3 tools/preview_tanmatsu.py settings_nodes settings_map settings_omlite settings_chat` → **exit 0**:
  `shots/2026-11-19-settings-nodes.png` (the NODES section: Contacts 3/32, Remove a contact focused, Clear
  all contacts, New-node notice On, CHAT below), `shots/2026-11-19-settings-map.png` (scrolled to MAP: Start
  position, Start zoom, the three pack rows, RPTR), and the om-lite/chat renders re-run because their old
  NODES/MAP placeholder rows are gone.

### Files changed

- **new** `core/contact_admin.h` — remove one contact with compaction (and the vacated row zeroed), the
  clear-all, the type words (`client`/`repeater`/`room`/`sensor`/`node`, now the one definition the advert
  log also uses), and the new-node notice's stored rule (absent/corrupt = On, only 0 = Off).
- **new** `core/map_settings.h` — the start-position choice (Ljubljana unless a stored 1), the 12..17 start
  zoom with its default, the last-position write rule (`lastPositionWriteDue`, time passed in, wrap-safe),
  and `startViewFromSaved` (keeps the saved CENTRE when the start zoom differs).
- **new** `test/host/test_nodes_map.cpp` — the 97 checks and two arm targets.
- `core/module.h`/`core/settings_model.h`: unchanged (the hook already generalised settings as data).
- `main/mc_link.h/.cpp` — `contactsRemove` / `contactsClear` under the store lock, raising the existing
  structural dirty flag so `contactsPersistTick()` rewrites `mcct` on the same pass; the new-node notice
  cached from NVS `mcset`/`newnode` and honoured where "New node: ..." is set; `advertTypeName` now
  delegates to `contactTypeLabel`.
- `main/map_link.h/.cpp` — NVS `mcset`: start position, start zoom, last view (z + world pixel); the
  last-position write; `packStatus()` (presence, tile count from the index size, file bytes), cached per pack
  and quiet when a pack is absent.
- `main/main.cpp` — the NODES section (read-only Contacts, Remove-a-contact list sub-page with the shared
  sheet, Clear-all with the same sheet, New-node notice toggle), the MAP section (two cycle rows + three
  read-only pack rows), the NODES list key branch and confirm targets, the row counts derived with
  `sizeof` (EST-448 had left NODES/MAP at 1), and the Map page: place from Last position / Start zoom on
  the first draw, mark a move, write per the rule while open and on leaving, re-place when a start row
  changes.
- `test/host/run.sh` — the nodes-map stage (links `contact_blob.cpp` + `mc_store.cpp`, so the round trip is
  the real codec).
- `tools/preview_tanmatsu.py` — one `_omlite_rows()`/`_draw_omlite_rows()` pair used by all four OM Lite
  previews, plus `settings_nodes` and `settings_map`.
- `docs/PROGRESS.md` — this entry.
- `shots/2026-11-19-settings-nodes.png`, `shots/2026-11-19-settings-map.png` (+ om-lite/chat re-rendered).

### Self-sweep (what it found, and what it did not)

- **Found and fixed — EST-448 built only one row of NODES and MAP.** `omLiteSettings` passed a hard-coded
  `1` for both sections, so an EST-447 provider with four NODES rows and five MAP rows would have drawn only
  its first row each. Both counts are now `sizeof(array)/sizeof(array[0])`.
- **Found and fixed — a stale root focus after Clear all.** The Clear sheet is raised at the ROOT, and the
  first version called `closeNodesPage()` on Apply; `closeSubPage()` at depth 0 restores the last sub-page's
  saved focus, so the Settings page would jump to a row the user never left. The Apply path now closes only
  when a sub-page is actually open.
- **Found and fixed — "now ago".** `formatAge` answers `now` for a fresh advert; the list row now says
  "client · now" instead of "client · now ago".
- **Swept the pack rows:** they read `maplink::packStatus`, which caches per pack (a getter runs on every
  draw) and says `no card` when the SD never mounted, `absent` for a missing pack, so the MAP section cannot
  turn into 30 failed `f_stat`s on a card-less boot.
- **Swept the write rule's corners:** no change = no write; leaving ignores the floor; a 32-bit wrap is
  measured as the real gap (both "not yet" and "due" pinned). The Map save is additionally gated on the map
  being ready and the card mounted, so a key press on a "No card" page cannot store a view that was never
  drawn.
- **Swept the removal against arming:** the list row's id is the STORE index captured from the live order;
  a stale row is refused (`index >= count`) rather than removing a neighbour, and the confirm text's count
  is never used as a target for Clear all.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** No device, no serial port, no server.
- The panel on glass: the NODES list's focus ring/scroll with 32 contacts, the "now"/age value column at
  800 px, and the sheet over the list.
- NVS after a real reboot: `mcct` after a removal/clear, `mcset`'s start position/zoom/last view across a
  power cycle. The codec and the rules are host-tested; the flash on the device is not.
- The real pack counts and the z17 file size on Filip's card: the preview's numbers are invented (rule 9)
  and `packStatus` was not run against the card.
- The once-a-minute rule at wall-clock speed on the device (host-tested at exact instants only).

### Next step

A device session should walk: remove one contact (watch `contacts: removed row N` and the next
`contacts: N stored to NVS`), clear all, toggle the notice and hear a new advert silently, set Last position
and z17, leave the Map page (watch `map settings: last view saved`), reopen it and land on the same place.
EST-447 completes the three-part Settings rebuild; the next ticket can be the whole-Om-Lite review.

## 2026-11-19 · S514 (EST-446) · Settings part 2: channels + scope

> Written by **DeepSeek** for ticket **EST-446**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts AFTER EST-445 and EST-448 (their uncommitted work is the base). **Version: 44.**
> This entry is open first, as the ticket asks; Results and files are filled in as the work finishes.
>
> **Never in the tree:** no channel key appears in this repo, the notes, a test or a log line. The host oracle uses
> derivable hashtag keys and public PSK-style vectors only; the private "Don't Panic" key exists only on the device.

### What this changes

- **Inside the OM Lite tab, not a second Settings:** the ticket's "CHAT tab" is the **CHAT section** of the OM Lite
  tab from EST-448 (the per-app structure is the newer decision). Its rows are the device's live channels.
- **CHANNELS:** one row per channel — name left; right `scope #si` / `no scope` / `default`; a chevron into the
  channel page. Below them `Add hashtag channel` and `Add channel by key`.
- **Channel page:** Name (read-only for a `#hashtag`; a private channel can be renamed), Region / scope (the L2's
  Default / No scope / Region choice, Region opens the shared field), Key (base64 shown ON SCREEN only after the
  confirm "Show the secret key on screen?"), Remove channel (the shared sheet: "Remove <name>? Its history stays on
  the card."). Index 0 is removable like any other.
- **Add:** hashtag normalisation is the L2's (`channels.cpp:424-441`): one `#`, no spaces, LOWERCASED; the key is
  `sha256(tag)[0..15]`. Add-by-key uses the L2's base64 decoder rules (`channels.cpp:388-410`): spaces/tabs
  skipped, `=` ends, bad characters refused, exactly 16 bytes. A duplicate is refused with a notice.
- **Storage:** the existing NVS `mcch` layout (`cnt`, `n<i>`, `k<i>`, `s<hh>`), rewritten in a safe order — new
  entries first, `cnt` last, cleanup after — so a power cut at any step leaves a loadable table. Remove compacts
  the indices and erases `s<hh>` only when no remaining channel has that hash.
- **No restart:** the main task writes NVS, then asks the radio task (the one owner of the table) to reload; the
  chat list and the radio's view see the new table on its next loop.

### Results (real exit codes)

- `sh test/host/run.sh` → **exit 0**. **13,856 checks, 0 failures** (before: 13,680; +162 for the new
  channel-admin stage and **+14** for the list sub-page keys). Per stage: core 229 · clock 91 · store 127 ·
  history 84 · layout 62 · mc 115 · dm 45 · mc-ASan 115 · dm-ASan 45 · dm_send 140 · dm_send-ASan 140 ·
  map 10,393 · txqueue 93 · chat-cards 99 · menu 28 · **settings 273** · **channel-admin 162** · advert 162 ·
  contact-blob 1,190 · channel-scope 94 · dm guard 169. Log: `paperclip-pilot/sweep/est446/host-final.log`.
- **ARMED, twice** (logs beside it; `channel_admin.cpp` restored and re-verified after each):
  - **Hashtag lowercasing removed** → red: `FAIL test_channel_admin.cpp:75  "#Slovenija" == "#slovenija"`,
    `:108` the same on the hashlib vector (124 checks, 2 failures). `arm1-lowercase.log`
  - **`cnt` written FIRST instead of last** (the unsafe order) → red:
    `FAIL test_channel_admin.cpp:345  plan[0].op == meshcore::ChannelNvsOp::SetKey` (88 checks, 1 failure).
    `arm2-count-first.log`
- `make build` → **exit 0**. `application.bin` **0x16d7c0 = 1,497,024 B (1,461.9 KiB)** — **45.7 % of the
  3,200 KiB per-app ceiling**; the build's own check says `0x92840` (**29 %**) free in the 2 MB app partition.
  Log: `paperclip-pilot/sweep/est446/build-final.log`.
- `python3 tools/preview_tanmatsu.py settings_chat` → **exit 0**: `shots/2026-11-19-settings-chat.png`
  (800×480), the OM Lite tab's CHAT section with the four real channels and their scopes, the focus ring on
  the first channel and the two add rows.

### Self-sweep (what it found, and what it did not)

- **Found and fixed — every EST-448 row became ambiguous.** The first row-aware getter was a second
  `(Core&, const char*, …, SettingsValueFn, …)` overload; a row that passed `nullptr` as its getter then
  matched BOTH constructors, and test_settings.cpp stopped compiling. The getter now travels in a
  `RowValueFn` wrapper, so `nullptr` can never select it.
- **Found and fixed — a duplicated scope erase.** Two removed slots can share one 1-byte hash; the first
  plan emitted `EraseScope` twice for it. It now emits each hash once (the test pins `erased == 1`).
- **Found and fixed — a stale sub-page name.** The channel page first read a name captured by the TAB
  builder, so a rename would show the old label for one frame. It reads `mc_link::channelName()` live.
- **Swept against call sites:** every `beginConfirm` now sets a target (name / remove / show key) and Cancel
  cannot reach any of the three; the field's character rule is per page (a hashtag allows `#` and lowercases
  in the normaliser, the key allows base64 + spaces, the region uses MeshCore's own `is_name_char`); the
  text field was widened to 64 bytes for a pasted key while the node-name path keeps its 31-byte rule.
- **Not swept / not visible here:** the panel itself (nothing installed or flashed); the real NVS after a
  reboot; and the store's channel-index remap after a remove — the history FILES stay on the card (the
  ticket's wording) but the RAM message ring is still indexed by the OLD slot until the next boot. That is
  the one honest limitation of this part.

### Files changed

- **new** `core/channel_admin.h/.cpp` — hashtag normalisation (the L2's `channels.cpp:424-441`), the
  sha256 tag key, base64 encode/decode (the L2's `channels.cpp:76-85, 388-410`), duplicate refusal, the
  scope row text, and `channelRewritePlan` (new entries first, `cnt` last, cleanup after; one `EraseScope`
  per removed hash that no survivor carries).
- **new** `test/host/test_channel_admin.cpp` — 162 checks: normalisation, keys against Python's hashlib,
  base64 good/short/bad/too-long, duplicate refusal, the rewrite plan cut after EVERY step (a counted slot
  always has a key, no invented key, the full plan loads `after`), remove + compaction, scope cleanup with
  and without a hash collision, and the three row texts.
- `core/settings_model.h` — the row-aware getter (`RowValueFn`), so one row per channel can read its slot.
- `core/settings.h` — `settingsSubPageKey`: a LIST sub-page uses the same row arithmetic and events.
- `core/CMakeLists.txt` — `channel_admin.cpp`.
- `main/mc_link.h/.cpp` — `channelsAddHashtag`, `channelsAddByKey`, `channelsRename`, `channelsRemove`,
  `channelsKeyCopy`, `channelApplyPlan` (commit per step), and the radio task's reload flag/revision in
  `serviceRequests()`; names are logged, keys never.
- `main/main.cpp` — the OM Lite CHAT section built from the LIVE table (one row per channel + the two add
  rows), the channel / scope / add-by-key list sub-pages, the shared field for hashtag, region and key, the
  one confirm sheet for remove and "show the key", and the field's per-page rules.
- `main/settings_page.h/.cpp` — a list sub-page draws through the same body renderer as a tab; the shared
  field takes its hint and maximum from the page.
- `test/host/run.sh` — the channel-admin stage and the Python hashlib/base64 oracle (temp dir, never
  committed).
- `test/host/test_settings.cpp` — +14 checks for the list sub-page keys.
- `tools/preview_tanmatsu.py` — `settings_chat`.
- `docs/PROGRESS.md` — this entry.
- `shots/2026-11-19-settings-chat.png`.

### Items done (the ticket's numbers)

1. **CHAT section** (the ticket's "CHAT tab", inside EST-448's OM Lite tab): one row per channel, name left,
   `scope #si` / `no scope` / `default` right, a chevron; then **Add hashtag channel** and **Add channel by
   key**.
2. **Channel page:** Name (Info for a `#hashtag`, editable for a private channel) · Region / scope (the L2's
   Default / No scope / Region, Region opens the shared field) · Key "share" after the confirm **"Show the
   secret key on screen?"**, on screen only, never logged · **Remove channel** through the sheet **"Remove
   <name>? Its history stays on the card."** Index 0 removable like any other (the host test removes 0 and 1).
3. **Add:** the L2's normalisation (one `#`, no spaces, lowercased) and `sha256(tag)[0..15]`; add-by-key uses
   the L2's base64 rules; duplicates and bad keys are refused with a notice.
4. **Storage:** the existing `mcch` layout rewritten in the safe order (host test cuts after every step),
   remove compacts and erases `s<hh>` only when no survivor has that hash, and the radio task reloads the
   live table + its view without a restart.
5. **Host tests + two arms** — above.
6. **Preview** — `shots/2026-11-19-settings-chat.png` with the four real channels: Don't Panic `default`,
   Public `no scope`, #slovenija `#si`, #slovenia `#europe`.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** No device, no serial port, no server.
- The panel on glass: the list sub-page's focus ring/scroll, the key page's base64 at 800 px, and the
  confirm sheet over a sub-page (drawn by the same code, but not photographed).
- NVS after a real power cut and a reboot; the reload's timing against a frame the user is looking at.
- The RAM message ring's channel indices after a remove (see the self-sweep's last bullet).

### Next step

**EST-447** fills the NODES and MAP sections from the same module hook (contacts/keys/favourites, map tiles,
zoom range, centre). A device session should then walk: add `#si`, remove channel 0, show a private key,
read the log to prove the key never appears.

## 2026-11-19 · S514 (EST-448) · Settings per app: one tab per owner, modules supply their own rows

> Written by **DeepSeek** for ticket **EST-448**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts AFTER EST-445 is in the tree and BEFORE EST-446/447. **Version: 44.** This entry is
> open first, as the ticket asks; Results and files are filled in as the work finishes.

### What this changes

- **Top tabs are owners, not functions:** General (the core) · OM Lite · OPS-TOC · Mesh-Torry, in the start menu's
  order after General. The tab list is DERIVED from the module registry, never a second hard-coded table.
- **The module hook** (`core/module.h`): a module returns its settings as data — a list of sections (header + rows),
  each row carrying its label, a value getter that reads the real store, a kind (info / toggle / cycle / sub-page /
  action) and its handler. The Settings frame draws whatever the module returns.
- **A module with no settings data** (`settings == nullptr`) shows one dim line, "Settings come with the app".
  OPS-TOC and Mesh-Torry are both still LATER, so that is what their tabs show today.
- **A stopped module still shows its settings** (they are stored values); an action row that needs the module
  running is refused with a notice instead of silently doing nothing.
- **The OM Lite tab** is one scrolling list with the same section treatment as General: NODES · CHAT · MAP · RPTR.
  RPTR carries the one honest line "Repeater admin comes later". EST-445's four separate Nodes/Chat/Map/RPTR tabs
  move here as sections; EST-446 (channels) and EST-447 (contacts + map) will fill the CHAT / NODES / MAP sections.
- **The `,` key** from an OM Lite page opens the OM Lite tab scrolled to that page's section (Nodes -> NODES,
  Chat -> CHAT, Map -> MAP, RPTR -> RPTR). From the start menu (`,`) or the Settings bar it opens General.
- **Kept from EST-445:** the frame's key arithmetic (tabs clamped, focus skipping section headers, scroll following
  focus, Esc's two levels), the one shared confirm sheet, and every value read from its real source.

### Results (real exit codes)

- `sh test/host/run.sh` → **exit 0**. **13,680 checks, 0 failures** (before this ticket: 13,604). The Settings
  stage is now **259 checks** (was 183; +76 for the registry tabs, the module hook/placeholder, the `,` target and
  NeedsRunning). Per stage: core 229 · clock 91 · store 127 · history 84 · layout 62 · mc 115 · dm 45 · mc-ASan 115 ·
  dm-ASan 45 · dm_send 140 · dm_send-ASan 140 · map 10,393 · txqueue 93 · chat-cards 99 · menu 28 · **settings 259** ·
  advert 162 · contact-blob 1,190 · channel-scope 94 · dm guard 169. Log:
  `paperclip-pilot/sweep/est448/host-final.log`.
- **ARMED, twice** (logs in `paperclip-pilot/sweep/est448/`; `settings.h` restored and re-verified after each):
  - **Tab order** — the registry loop walked BACKWARDS, so the tabs were no longer General · OM Lite · OPS-TOC ·
    Mesh-Torry → red at `test_settings.cpp:299` `"Mesh-Torry" == "OM Lite"`, `:334` `om != nullptr`, `:464`
    (229 checks, 3 failures). `arm1-tab-order.log`
  - **`,` section mapping** — Chat mapped to section 0 (NODES) instead of 1 (CHAT) → red at
    `test_settings.cpp:365` `t.section == 1 (got 0, want 1)` (246 checks, 1 failure). `arm2-comma-section.log`
- `make build` → **exit 0**. `application.bin` **0x16a8c0 = 1,484,992 B (1,450.2 KiB)** — **45.3 % of the
  3,200 KiB per-app ceiling**, well under it; the build's own check says `0x95740` (**29 %**) free in the 2 MB app
  partition. Log: `paperclip-pilot/sweep/est448/build-final.log`.
- `python3 tools/preview_tanmatsu.py settings_general settings_omlite settings_opstoc` → **exit 0**:
  `shots/2026-11-19-settings-general.png`, `shots/2026-11-19-settings-omlite.png` (OM Lite tab, CHAT focused/in
  view, NODES · CHAT · MAP · RPTR) and `shots/2026-11-19-settings-opstoc.png` (the one honest dim line).

### Self-sweep (what it found, and what it did not)

- **Found and fixed:** `kSettingsMaxTabs` was 8 while the registry allows **8 modules + General = 9**, so the last
  app's tab would silently vanish at full capacity. It is now `Registry::kMaxModules + 1`. Also, a shrunken
  registry could leave the frame on a tab index that no longer exists; the draw now clamps it with
  `settingsTabIndex` first.
- **Swept against call sites:** the module hook (null provider in one helper), every row's getter (called on every
  draw, never a display copy), the frame's Enter/NeedsRunning path, the shell's id → row mapping for the Name
  sub-page, and the `,` handler's precedence over the menu branch (an open Chat composer still eats its comma).
- **Not swept / not visible here:** the panel itself (nothing installed or flashed), and real NVS values after a
  reboot.

### Files changed

- **new** `core/settings_model.h` — the data shape: `SettingRow` (label, value getter, kind Info/Toggle/Cycle/
  SubPage/Action, handler, `needs_running`), `SettingsSection` (header + rows), `SettingsHook` and
  `kSettingsPlaceholder`.
- `core/module.h` — the hook itself: `SettingsHook settings` as the last `Module` field.
- `core/settings.h` — the tab list DERIVED from the registry (`settingsTabDefs`), `settingsTargetForPage`
  (`tab + section`), section-aware focus/scroll arithmetic, `focusSection`, the new row kinds/events
  (`ToggleValue`, `NeedsRunning`), and the unchanged name/advert/confirm rules. `kAppVersion` stays `"44"`.
- `main/settings_page.h/.cpp` — dynamic owner tabs, sections from the tab's provider, each value read through its
  getter, the one-line placeholder, and the same focus ring / thumb / confirm sheet as EST-445.
- `main/main.cpp` — General as the CORE's data (sections + getters + Advert-now/automatic-advert handlers), OM Lite's
  four-section provider, the two LATER apps' `settings = nullptr`, `,` landing on tab+section, and the key branch
  calling the row's handler instead of switching on ids.
- `test/host/test_settings.cpp` — 259 checks: registry tab order (including a reversed registry), a stopped
  module still answering, `nullptr` → the one-line state, `,` → tab+section for all four pages, a LATER tab
  present, section jump/scroll, header-skipping focus, NeedsRunning, Cancel/Apply, and the EST-445 name/advert/
  menu checks.
- `test/host/run.sh` — links `registry.cpp` + `alloc.cpp` into the Settings stage (the tab list needs the real
  registry).
- `test/host/test_core.cpp` — the new `Module::settings` field in every test module (all `nullptr`).
- `tools/preview_tanmatsu.py` — `settings_omlite` + `settings_opstoc`, and General's tab row is now the four owners.
- `docs/PROGRESS.md` — this entry.
- `shots/2026-11-19-settings-omlite.png`, `shots/2026-11-19-settings-opstoc.png` (+ General re-rendered).

### Items done (the ticket's numbers)

1. **Top tabs** — General · OM Lite · OPS-TOC · Mesh-Torry, derived from the registry in menu order, General first.
2. **Module hook** — `Module::settings` returning `SettingsSection*` with rows (label, getter, kind, handler); a
   `nullptr` hook is the one dim line; a stopped module still shows stored values and an action that needs it
   running is refused with a notice.
3. **OM Lite tab** — one scrolling list: NODES · CHAT · MAP · RPTR (RPTR = "Repeater admin comes later"); `,` from
   Nodes/Chat/Map/RPTR lands on that section; menu/`,`/Settings bar land on General.
4. **EST-445 kept** — frame key rules, confirm sheet, read-from-real-source; the four new host checks plus two arms
   are above.
5. **Previews** — both PNG paths above.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** The frames are host renders; the four-tab row, the section
  scroll on glass, the placeholder line and the Name confirm sheet still need a device session.
- **`main/` is not host-tested.** The providers, getters and the key branch compile with `make build`; every
  DECISION they call is in `core/settings.h` and tested.
- **Opening Settings still starts OM Lite first** (it is the module that owns `Page::Settings`), so on the device
  General is reached with OM Lite running. The data model and tests cover the stopped-module case, but the page
  cannot be drawn with every module stopped until Settings gets its own core-owned page.
- **NVS after a reboot** and the real 1/3/6/12 h schedule were not run.

## 2026-11-19 · S514 (EST-445) · Settings, part 1: the frame + General

> Written by **DeepSeek** for ticket **EST-445**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. On top of commit `d3943e8` (v43) plus EST-444's uncommitted work, kept as it is. **Version:
> 44.** Read `BRIEF.md`, `docs/PLAN.md` §4-5, this file's S513-S514 entries, the L2's `ui/settings_layout.h`,
> `ui/settings_prefs.h`, `ui/settings_actions.h`, `ui/screen_settings.*`, `core/router.*`, `core/pages.h`,
> `main/main.cpp`, `main/chrome.*` and `tools/preview_tanmatsu.py`. This entry is written FIRST, as the ticket asks;
> Results is filled in as the work finishes.

### What this part builds (and what it deliberately leaves to EST-446/447)

- **The frame:** five tabs (General · Nodes · Chat · Map · RPTR), Left/Right clamped; Up/Down over the rows, clamped
  at both ends and skipping section headers; the scroll follows the focused row. Enter opens a sub-page, cycles a
  value, or runs a row action. Esc closes a sub-page, then leaves Settings for the page it was opened from. The
  Settings key (`,`) inside a sub-page returns to the Settings root (the router's "own key = page top" rule).
- **Reaching it:** from the start menu, Down from the three cards lands on a full-width Settings bar; from any OM
  Lite page, `,` opens Settings on the tab that matches the page (`settingsTabForPage`: Nodes→Nodes, Chat→Chat,
  Map→Map, RPTR→RPTR, menu→General). Every hint bar shows `, settings`.
- **General tab, part 1 only:** Name (sub-page + confirm sheet), Public key, Advert now, Automatic advert
  (Off/1/3/6/12 h), then the read-only RADIO (the numbers actually pushed to the chip, from `core/radio_plan`) and
  DEVICE sections. Nodes/Chat/Map/RPTR show "coming next" bodies: EST-446/447.
- **One shared confirm sheet** (the L2's EST-422 pattern): the row keeps its OLD value behind the sheet until
  Apply; Cancel changes nothing.

### Results (real exit codes)

- `sh test/host/run.sh` → **exit 0**. **13,604 checks, 0 failures** (before this ticket: 13,393). The new
  `test_settings` stage is **183 checks**; the start menu's stage is now **28** and is actually RUN — the old
  `run.sh` compiled `test_menu_cards.cpp` but never executed it, so the "before" number did not include it.
  Per stage: core 229 · clock 91 · store 127 · history 84 · layout 62 · mc 115 · dm 45 · mc-ASan 115 · dm-ASan 45 ·
  dm_send 140 · dm_send-ASan 140 · map 10,393 · txqueue 93 · chat-cards 99 · menu 28 · **settings 183** · advert 162 ·
  contact-blob 1,190 · channel-scope 94 · dm guard 169. Full log:
  `paperclip-pilot/sweep/est445-host-final3.log`.
- **ARMED, twice** (the ticket's two reds; copies under `paperclip-pilot/sweep/est445/arm-*`):
  - **Name rule** — `[ ] \ : , ? *` wrongly allowed → the test is red:
    `FAIL test/host/test_settings.cpp:63  !tm::nodeNameCharAllowed('[')`;
    `FAIL test/host/test_settings.cpp:102  !tm::nodeNameValid("A[")`;
    `FAIL test/host/test_settings.cpp:126  !tm::nodeNameDraftPut('[', 3, &ok)` (163 checks, 3 failures).
  - **Name length** — the over-long check removed, so a 32-byte name would be silently truncated →
    `FAIL test/host/test_settings.cpp:110  !tm::nodeNameApply(max32, applied, sizeof(applied))`
    (180 checks, 1 failure).
  - **Frame keys** — tabs wrapping instead of clamping, focus stepping onto section headers, scroll not following
    focus → `FAIL test/host/test_settings.cpp:223  tm::settingsTabMove(0, -1) == 0 (got 4, want 0)`;
    `FAIL test/host/test_settings.cpp:252  tm::settingsFocusMove(kRows, kRowCount, 3, 1) == 5 (got 4, want 5)`;
    `FAIL test/host/test_settings.cpp:288  r.event == tm::SettingsEvent::None` (132 checks, 3 failures).
  - With the real `core/settings.h`: **183 checks, 0 failures**.
- `make build` → **exit 0**. `application.bin` **0x16a060 = 1,482,848 B** (1,448.09 KiB) — **45.3 % of the
  3,200 KiB per-app ceiling**, well under it; the build's own size check says `0x95fa0` (**29 %**) free in the
  2 MB app partition. Log: `paperclip-pilot/sweep/est445-build-final3.log`.
- `python3 tools/preview_tanmatsu.py settings_general` → **exit 0**, wrote
  `shots/2026-11-19-settings-general.png` (800×480).

### Files changed

- **new** `core/settings.h` — the frame's pure rules: five tabs + `settingsTabForPage`, focus/scroll arithmetic
  that skips section headers, `SettingsFrame` + the shared confirm sheet state, `settingsKey`, the node-name rule
  (1..31 printable ASCII, `[ ] \ : , ? *` refused, trimming), the advert-interval mapping (Off/1/3/6/12 h) and
  `advertScheduleDue`, and `kAppVersion = "44"`.
- **new** `main/settings_page.h/.cpp` — the drawing only: status bar, page head, tab row, section rows, label/value
  rows, vector chevron, gold focus ring, scroll thumb, the Name sub-page and the shared sheet.
- **new** `test/host/test_settings.cpp` — 183 checks (name rules, interval mapping incl. Off vs the manual 'a' path,
  tabs/focus/scroll, Enter by row kind, Esc's two levels, Cancel/Apply, the menu's Settings bar).
- `core/menu_cards.h/.cpp` — the menu is now three cards + the full-width Settings bar (`kMenuSettingsFocus`),
  Down from any card reaches it, Up returns to the first card, `menuEnterActionAt` answers `OpenSettings`.
- `core/mc_wire.h` — `kFloodPathHashSize = 2`, the ONE value `advertBuild` / `buildScopedChannelText` are given
  and the RADIO row reads.
- `main/mc_link.h/.cpp` — the node name now comes from NVS `mcid/name` (default `TNMT (SI)`) and feeds adverts AND
  the channel-text sender; `setNodeName`; the automatic interval from `mcid/advint` (seconds, 0 = Off) read by
  `serviceRequests()` every loop; `setAdvertIntervalSeconds`; the two flood-builder literals use
  `kFloodPathHashSize`.
- `main/main.cpp` — the Settings state and row builder (every value read from its real source), the key branch
  (frame, Name editor, confirm sheet), `,` opens Settings on the page's tab, Esc returns to the page it came from,
  `module.pages` now includes Settings, and the menu's Settings bar / Down focus.
- `main/launcher_page.h/.cpp` — the full-width Settings bar under the three cards and the `, settings` hint.
- `main/chrome.cpp` — hint chips may be five and stop before the symbol cluster; compacted spacing so the ticket's
  Settings hints all fit.
- `main/CMakeLists.txt` — `settings_page.cpp` in the app.
- `main/nodes_page.cpp`, `main/chat_page.cpp`, `main/map_page.cpp` — `, settings` added to every hint bar.
- `test/host/run.sh` — the new Settings stage, and `"$MENU"` so the menu test is actually executed.
- `test/host/test_menu_cards.cpp` — the Settings-bar focus/Enter rules.
- `tools/preview_tanmatsu.py` — `settings_general` renderer and the PNG above.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** The look is a host render, not a panel frame; the tab row, the
  focus ring, the Name editor and the confirm sheet on glass are for the next device session.
- **NVS on the device.** `mcid/name` and `mcid/advint` are read/written through the same NVS calls as the identity
  and channel code, but the write survives a reboot only on the panel: this run could not reboot the device.
- **The schedule over hours.** The mapping and `advertScheduleDue` are host-tested; a real 3 h (or Off) wait is not.
- **`main/` is not host-tested.** `settings_page.cpp`, the `main.cpp` branches and the NVS glue need the IDF; they
  are compiled by `make build` and reviewed, while every DECISION they call is in `core/settings.h` and tested.
- **A DM's "sender name".** MeshCore direct messages carry no name field (the peer is identified by its key), so the
  rename affects adverts and channel-text senders — the two places the old `kNodeName` was used. The DM view still
  names the peer from its contact advert.
- **The full General tab does not fit one 800×480 screen.** The body scrolls and the preview shows the top (NODE,
  ADVERT, the start of RADIO) with the scroll thumb; the DEVICE rows are built and drawn when scrolled to.

## 2026-11-19 · S514 (EST-444) · own symbol key = page top; Enter key cap

> Written by **DeepSeek** for ticket **EST-444**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Starts AFTER EST-443 and keeps its uncommitted work as it is (this is the second half of the
> same **Version: 43** image, not a new version). Read `BRIEF.md`, `docs/PLAN.md` §5, `core/router.*`,
> `core/keys.h`, `core/core.*`, `core/module.h`, `main/main.cpp` (the symbol-key route and the Chat open state),
> `main/chat_page.cpp` (the composer), `test/host/test_core.cpp`, `tools/preview_tanmatsu.py`. This entry is written
> FIRST, as the ticket asks. Note: the date line says 2026-11-19 as the ticket does; the clock on this machine also
> reads 2026-11-19.

### 1. The rule and where it lives

**Filip's rule (EST-444):** pressing the symbol key of the page you are ALREADY on returns to that page's top
level — Chat closes an open channel or DM to the two-column list; Nodes/Map/RPTR close an inner level if one is
open; at the top (and on the Launcher) nothing changes. Another page's key routes as before.

- The decision is in **`core/router.cpp::Router::symbol`**: a symbol key whose page is the current page returns the
  new `RouteAction::Top` and never calls `go()`. This is still the ONE place the five-key table is read
  (`core/keys.h::page_for_symbol`).
- The effect is in **`core/core.cpp::Core::handleKey`**: on `Top` it calls the page owner's new
  `Module::onTop(Core&, Page)`. The Page matters: OM Lite owns five pages, so the hook may only close the level of
  the page whose key was pressed (the Chat level must not vanish because the Nodes key was pressed). A module
  returns `true` when it had an inner level and closed it, `false` at the top — so "own key at top = no-op" is the
  module's own answer, not a guess by the router. The module that owns the level is the only object that can close
  it (the same split `router.h` already documents for Esc).
- `Module::onTop` is an optional pointer; `nullptr` means "no inner level", which is today's Nodes, Map, RPTR and
  Settings (no search field or sheet is built on these pages yet). The Launcher has no module, so its own triangle
  key is a `Noop`.
- Chat's one implementation (`main/main.cpp`) closes the open level **exactly as Esc does today**: it first checks
  `page == Page::Chat` (so the Nodes/Map/RPTR keys cannot close the Chat level), then marks the channel or DM read
  (`noteChannelRead` / `noteDmRead`), `chat_open = false`, scroll and seen-count reset, and the composer text is left
  untouched (it is one buffer per shell session, as before). The six open-level variables are `static` in `app_main`
  so the hook can be a captureless lambda in the module record.

### The L2 check (cited, and it DIFFERS)

- `firmware/src/main.cpp:3899-3905` (`showTab`): `if (tab == g_tab) { ... plain redraw ... return; }` — a tap on the
  active tab on the L2 is a plain redraw. It does **not** close the open chat.
- The L2's Chat is one window with a selector (`firmware/src/main.cpp:1417 handleChatTap`, `ChatHit::Head` opens the
  selector), not a list level above an open chat, so "tap the active tab inside a chat" has no list to go back to.
- **So the L2 differs from Filip's rule.** As the ticket says, Filip's rule wins: the Tanmatsu's own key goes to the
  page's top. This is recorded here, not silently copied from the L2.

### 2. The composer: "Send" button -> Enter key cap

- The filled gold "Send" plate is gone. The right end of the composer is now a **key cap in the hint bar's style**:
  the same dark plate + 1 px outline as a hint chip (`main/chrome.cpp::drawKeyCap`, now the ONE cap drawing used by
  both), with a small vector **↵** (the generated font has no U+21B5 glyph) followed by the word **Enter**.
- **Dim when the draft is empty (`kDimmer`), normal when there is text (`kInk`)**, in channels and DMs alike; the
  behaviour is unchanged (`Enter` sends).
- `tools/preview_tanmatsu.py` draws the same cap (its `key_cap` helper, the hint bar's own `Panel2`/`KeyLine`), and
  the DM preview is re-rendered as `shots/2026-11-19-dm-enter.png`.

### Results (real exit codes)

- `sh test/host/run.sh` → **exit 0**. The core/router suite is now **229 checks, 0 failures** (was 192, so the new
  rule adds 37): own key inside → `RouteAction::Top`, the owner's `onTop` closes the level, the page stays Chat,
  and the module's `onKey` never sees a symbol key; own key at the top → consumed, `onTop` answers false, nothing
  changes; the Nodes key while a Chat level is open → the hook is told Nodes and leaves the Chat level alone;
  another page's key → `Switched` as before; the Launcher's own triangle → `Noop`.
  Every other suite unchanged: clock 91, store 127, history 84, layout 62, mc 115, dm 45, mc-ASan 115, dm-ASan 45,
  dm_send 140, dm_send-ASan 140, map 10,393, txqueue 93, cards 99, advert 162, contact_blob 1,190, channel_scope 94,
  dm guard 169. Full log: `paperclip-pilot/sweep/est444-host-final.log`.
- **ARMED** (the ticket's "ARM it"): the same new test built against the pre-fix `core/router.cpp` + `core/core.cpp`
  from `HEAD` (`paperclip-pilot/sweep/est444/arm/`) → **exit 1**, `FAIL test/host/test_core.cpp:389
  own.action == tm::RouteAction::Top` (199 checks, 1 failure). With the fix: 229 checks, 0. The test catches the bug.
- `make build` → **exit 0**. `application.bin` **0x167370 = 1,470,320 B**, **30 %** of the 2 MB app partition free
  (`0x98c90`). Log: `paperclip-pilot/sweep/est444-build-final.log`.
- `python3 tools/preview_tanmatsu.py dm_enter` → **exit 0**, wrote
  `shots/2026-11-19-dm-enter.png` (800×480).

### Files changed

- `core/router.h`, `core/router.cpp`: `RouteAction::Top`; `symbol()` answers Top for the page's own key and never
  calls `go()`; the comment states the one-place rule.
- `core/keys.h`: `is_own_symbol(Key, Page)` — one predicate for "the page's own key", tested.
- `core/module.h`: the optional `Module::onTop(Core&, Page)` hook (nullptr = no inner level; the Page keeps one
  page's level from being closed by another page's key).
- `core/core.cpp`: `Core::handleKey` dispatches Top to the page owner's `onTop`; a symbol key is still always the
  core's.
- `main/main.cpp`: `g_om_lite.onTop` installed next to the Chat state; it closes exactly like Esc (mark read, list
  returns, scroll/seen reset, composer kept). The six open-level variables are `static` so the hook can be a
  captureless lambda; the other two module rows gained the `nullptr` field.
- `main/chrome.h`, `main/chrome.cpp`: `drawKeyCap` (now the hint bar's own cap drawing) and `drawEnterKeyCap` (the
  same cap with a stroke-drawn ↵, since the font has no U+21B5).
- `main/chat_page.cpp`: the composer's filled Send button is replaced by the Enter key cap, dim when the draft is
  empty and normal ink when there is text (channels and DMs share this composer).
- `test/host/test_core.cpp`: the new router test (4b) and the onTop fixtures.
- `tools/preview_tanmatsu.py`: `key_cap` helper (the hint bar uses it too), the channel and DM composers draw it,
  and the DM renderer is renamed `dm_enter` → `2026-11-19-dm-enter.png`.
- `docs/PROGRESS.md`: this entry.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** The own-key behaviour and the cap's look are for the next glass
  test; the host test proves the router/core decision and the preview is a host render, not a panel frame.
- **The ↵ is stroke-drawn on the device, not a font glyph.** The generated faces carry Latin-1 + Latin Extended-A,
  so U+21B5 does not exist in them; the preview can use DejaVu's glyph, the panel gets the same shape in three
  short lines. The exact pixel weight has not been seen on glass.
- **The channel preview was updated in the tool but only the DM PNG was re-rendered**, because the ticket names that
  one file; `shots/2026-11-13-chat.png` is still the older render of the list-and-chat page.
- The composer's placeholder line still says "Enter sends" next to the new cap. That is deliberate (the ticket only
  replaces the button); if Filip wants the line shortened, it is a one-line change in `main/chat_page.cpp`.

## 2026-11-19 · S514 (EST-443) · map: no black squares, fewer card reads

> Written by **DeepSeek** for ticket **EST-443**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. Read `BRIEF.md`, this file's S506 map entries and S513, `core/map_view.*`,
> `core/map_pack.*`, `main/map_link.cpp`, `main/map_page.cpp`, `test/host/test_map.cpp`, `test/host/run.sh`.
> **Version: 43.** This entry is written FIRST, as the ticket asks.

### The evidence, read from the log — not from a memory of it

`measurements/tanmatsu-v42-air-test-2026-11-19-S514.log`, the `fill:` lines: one zoom step fills 5-12 tiles in
193-640 ms with 66-87 card reads — e.g. `5 tile(s) in 217 ms (69 card reads)` is ~14 index reads per tile — and
each tile that has not landed yet leaves a void-coloured square on the glass. The reads are the binary search over
`tiles.idx` (24-byte records, ~log2(900,000) ≈ 20 probes), which is what item 2 attacks.

### 1. PLACEHOLDERS (the visible fix)

**Rules in `core/map_view.*`, pixels in `main/map_page.cpp`, cache access in `main/map_link.*`.**

- New pure geometry `parentPlaceholder()` / `childPlaceholder()`: for a target tile (z,x,y) that is still
  Pending or Empty, name the source tile and the source/destination rectangles.
  - **Zooming IN:** the parent (z-1) tile, source rectangle `(x&1)*128, (y&1)*128, 128x128`, drawn into the whole
    256x256 target tile (2x upscale).
  - **Zooming OUT:** the four children (z+1), `(2x+i, 2y+j)`, each whole 256x256 tile drawn into its 128x128
    quadrant of the target (1/2 downscale); a child that is not Ready leaves its quadrant void, as the ticket says.
  - **Bounds:** no parent at z9 (there are no z8 records) and no children at z17 (there are no z18 records);
    children are also refused past the last tile of z+1.
- **Scaling choice: nearest neighbour.** One source pixel per destination pixel, no per-channel arithmetic: a 2x
  upscale repeats each pixel, a 1/2 downscale takes the top-left of each 2x2 block. It is the cheapest correct
  shape and the placeholder is replaced by the real tile on the next repaint after it lands.
- **A placeholder never changes cache state.** The page reads it through a new read-only `TileCache::peek()` and
  `maplink::drawTileScaled()`, which never call `find()`/`take()`/`commit()` and never touch the LRU stamps, so a
  stand-in cannot evict a tile the fill still needs. `peek()` is host-tested against exactly that.
- The page chooses the direction from the previous composed zoom (first draw / same zoom = `Same`, which uses the
  parent); only the direction's source is tried, so a zoom out follows the ticket's "missing child quadrant stays
  void" rule rather than mixing resolutions.

### 2. FEWER CARD READS

**Shape (decided, not suggested blindly):** a two-level search in `core/map_pack.*`, `findTileBlocks()`.

- **Blocks of 170 records = 4080 B = one 4 KB card read.** Level 1 binary-searches the block's FIRST record; level 2
  reads the whole winning block once and searches its records in RAM.
- **Block-boundary keys in PSRAM, filled lazily.** `BlockCache` holds one 24-byte `Entry` + one valid byte per block;
  the caller sizes it from `count` at pack open (allocation only, **no index read at open**, so opening stays fast)
  and records are fetched on first visit. For the real packs: z17 ≈ 5,295 blocks ≈ **132 KB**, z16 ≈ 1,324 ≈ 33 KB,
  the z9-15 pack ≈ ≤447 ≈ 11 KB — **~176 KB for all three together, under the 512 KB budget**.
- **4 whole blocks in a round-robin LRU** (4 × 170 × 24 B = **16 KB**), so neighbouring tiles in a column/row read no
  card at all once their block is in.
- **The old per-column `RowHint` is removed.** Host measurement below: the block search is 0.0 reads/tile warm,
  while a hint adds one 24-byte record read on every hit and is no cheaper than a block-cache hit. Keeping it "just
  in case" would have added card traffic, so it is gone rather than silently left.
- **Fallbacks:** if the PSRAM arrays do not fit, `findRecord()` uses the old `findTile()`; `findTileBlocks()` with a
  null/mis-sized cache or a null `BlockReader` is still correct (record-by-record inside the chosen block), just
  slower. A failed block-key read is a failure, never a false hit.
- **Opening a pack adds one log line** with the block count and bytes, so the budget is visible on the glass:
  `z17: block index 5295 block(s), ... B keys + ... B LRU`.

**Host test (`test_block_search`):** a 5,000-record synthetic index; `findTileBlocks` must return the SAME record
index as `findTile` for **every one of the 5,000 records** and for three misses (wrong y, wrong x, wrong z), the
partial last block included. Read counters run through the same callbacks the device uses. Measured on a 15-tile
view (5×3, 30-block index): plain search **166 reads**, block search **cold 9**, **warm 0 (0.0/tile)** — the
ticket's ≤ 2/tile is met with margin once the block-key cache is warm; the cold number is much lower than the old
search too. The absolute cold count on z17 will be higher (a 5,295-block tree), which is why the block keys persist
for the life of the open pack and the LRU covers neighbouring tiles.

### 3. THE `fill:` LOG LINE

`logFill()` now prints the zoom and reads per tile, exactly the ticket's shape:
`fill z15: 12 tile(s) in 309 ms (73 card reads, 6.1/tile, hardware decode)`. `g_fill_z` is set when a new fill
starts. The page compose line also gained `N stand-in`, so the placeholder path is visible in the log next to
`N drawn`.

### Results (real exit codes)

- `sh test/host/run.sh` → **exit 0**; map suite **10,393 checks, 0 failures** (was 347 before this run's additions).
  Full log: `paperclip-pilot/sweep/est443-host-final2.log`.
- `make build` → **exit 0**; `application.bin` **0x167090 = 1,470,608 B**, **30 %** of the 2 MB app partition free.
  Log: `paperclip-pilot/sweep/est443-build-final2.log`.

### Files changed

- new code: `core/map_pack.*` (`BlockCache`, `BlockReader`, `blockCount`, `findTileBlocks`), `core/map_view.*`
  (`Placeholder`, `ZoomDir`, `parentPlaceholder`, `childPlaceholder`, `TileCache::peek`).
- changed: `main/map_link.*` (block cache storage + `readBlockRecords`, `drawTileScaled`, arming, `fill z…` line;
  RowHint removed), `main/map_page.cpp` (scaled nearest-neighbour blit, stand-ins, `N stand-in` in the compose log),
  `test/host/test_map.cpp` (block-search equality + read counts, placeholder geometry, `peek` LRU arm).
- `docs/PROGRESS.md`: this entry.

### What could NOT be checked

- **Nothing was installed, flashed or committed.** The placeholder look, the real card-read count and the
  `fill z…` line are for the next glass test; I have measured only the host callbacks.
- **The z17 block-key memory is arithmetic, not an allocation on the device.** 5,295 blocks comes from the 21.6 MB
  index; the `heap_caps_malloc` path in `ensurePack()` has not run on hardware. The fallback is deliberate.
- **The 4 KB read latency is not measured on this card.** It replaces ~14 small random reads with one sequential
  read plus one 24-byte record read, which is the intended win, but the glass log is the proof.
- **Rules 9:** no coordinates, SSIDs or keys were added; the test index is synthetic (x 1000+, y 2000+).

## 2026-11-18 · S513 (EST-441) · DM fixes from the air

> Written by **DeepSeek** for ticket **EST-441**: fenced Paperclip run, no session start/end, nothing installed,
> flashed or committed. On top of commit `8c8d25a` (v41) **plus EST-440's uncommitted work, kept as it is**.
> **Version: 42.** This entry is written FIRST, as the ticket asks.

### The evidence, read from the log — not from a memory of it

`measurements/tanmatsu-v41-dm-send-fail-2026-11-18-S513.log` (lines with `tm/mc`):

- 5292 `contacts: none stored yet (no "mcct" namespace)`; 5302 `restored DM thread ERA-3 (SI) (1 message(s),
  with a seen point)`; **no advert from ERA-3 in the whole log**.
- 55342 `DM: queued "t4" to "ERA-3 (SI)" (22 B, flood)` -> 56332 `DM from "ERA-3 (SI)": t4 (ts 1795011320, ...)`
  — our own frame, our own timestamp, echoed back by a repeater.
- 57672 `DM: route to "ERA-3 (SI)" stored (path_len 0x41, 1 hop(s))` and `PATH carried an ACK no pending message
  expected` — we learned a route from **our own** echoed path return (0x41 is 2-byte hashes, count 1: a repeater
  added its hash).
- 57842/68272/71372 `contacts: 1 stored to NVS (152 B)` — the bogus row is **persisted**, so it survives a restart.
- 74942 `DM: message 2 has NO CONFIRMATION after 3 attempts`; and ERA-3's real DMs are the `rx ... TXT_MSG, no
  contact of ours opens it (src 0x42)` lines. Both keys begin `0x42` (ours `42a16070…`, ERA-3 `428b4979…`), so the
  1-byte payload hashes collide and the wrong contact is the only one tried.

### Root causes, as the code has them

**A — a contact was invented from a file name.** `main/sd_history.cpp` `restoreDmThreads()` took the four bytes in
`dm-<8 hex>.log` and called `contactsUpsert()` with them, leaving bytes 4..31 zero. That row then (1) is what
`queueDm()` used for `identitySharedSecret()` — a secret ERA-3 cannot derive, so it cannot open our DM; (2) is the
only `0x42` candidate the RX path tries, whose MAC fails on ERA-3's real frames; (3) is written to NVS.

**B — our own frames were never registered as seen.** `payloadFingerprint()` existed and `doText()` used it for
channel texts, but nothing on the DM/ACK/PATH/advert send path did. Upstream does exactly this, and says why:
`firmware/lib/meshcore/Mesh.cpp:651, 680, 713, 723, 736` — `_tables->markSeen(packet); // mark this packet as
already sent in case it is rebroadcast back to us` (in `sendFlood`, `sendFlood` with transport codes, `sendDirect`,
`sendZeroHop` ×2), stored by `helpers/SimpleMeshTables.h:52-57` (`markSeen` writes the packet's hash; the ring is
`MAX_PACKET_HASHES = 128+32` slots), computed by `Packet::calculatePacketHash` (`Packet.cpp:41-50`) as
`sha256(payload_type || payload)[0..7]` — **the payload, never the path**, which is precisely why a repeater's echo
of our frame must match: it adds path bytes and changes nothing else. The L2 goes further and keeps its own-send
hashes separately (`src/mesh_link.cpp:408-425`).

### 1. A contact comes only from a verified advert (fix A)

- `restoreDmThreads()` no longer creates contacts. The thread lives in the DM ring alone, named from its own
  records (the first incoming record's `peer_name`, else the 8-hex prefix), until a real advert rebinds it.
- New pure rules in `core/mc_store.*`: `contactKeyIncomplete()` (bytes 4..31 all zero), `contactsDropIncomplete()`
  and `dmPeerKey()`. `restoreContacts()` runs the drop straight after `contactsDecode()` and logs once,
  `contacts: dropped N record(s) without a full key`; if anything was dropped the table is marked structurally dirty
  so the next main-task pass rewrites NVS **without** the bogus row — that is what cleans Filip's device.
- If a restored thread's 4-byte prefix matches **exactly one** full-key contact already in NVS (a row that came
  from a verified advert), the thread is rebound to that full key at restore time (`contactsFindUniquePrefix`);
  with none, or with an ambiguous prefix (the `0x42` collision is exactly that), it keeps the prefix and stays
  orphaned rather than guessing.
- `queueDm()` asks `dmPeerKey()` first and refuses a peer that is not a full-key contact — `identitySharedSecret()`
  is never called on a prefix-only key. The DM view (`main.cpp`) then keeps the text and says
  `No key for <name> yet: waiting for its advert`.

### 2. Every frame we transmit is registered before it goes out (fix B)

- `core/mc_wire.*` gains `frameFingerprint(const View&)` (the old `payloadFingerprint`, moved) and `SeenRing`: 32
  slots, each a fingerprint **plus one flag** `ours`, and `seen()` is the atomic check-and-insert the old
  `seenBefore()` was.
- Registration happens at the three points where a frame leaves: `queueFrame()` (DMs, retries, ACKs, path returns,
  adverts queued by the main task), `doText()` and `doAdvert()` (they call `om::radio::send()` directly). The
  channel-text registration moved from **after** the send to **before** it.
- RX checks the ring once, immediately after `decode()`, so every type is covered; a match is dropped with
  `rx N B type T: our own echo, dropped`, and a match that is NOT ours keeps the old `duplicate, already shown`.
- **The ring is 32, not 8:** every transmit now costs a slot as well as every receive, and one DM attempt alone
  produces a path return + an ACK on the air, a flood retry repeats them, and an advert goes out on the same ring.
  At 8 slots a retry burst pushed our own echo out before the repeater returned it — the exact defect. 32 slots ×
  5 bytes = 160 B, and it covers a full 3-attempt cycle with its replies and duplicates. Upstream keeps 160 slots
  of 8-byte hashes because a repeater forwards for many nodes; a leaf node knows only its own traffic.

### 3. Tests (`test/host/test_dm_guard.cpp`, new, in `run.sh`)

**ARMED — this is what they say against v41's behaviour.** The same test file was run against a `/tmp` copy with
the four old behaviours put back (no `noteOurs` on the send path, ring 8, no drop on load, any key accepted for the
secret, and `contactsUpsert()` inside the restore) — the new API kept so it compiles:

```
FAIL test_dm_guard.cpp:106  contacts.count == 0          <- A: a restored thread created a contact
FAIL test_dm_guard.cpp:107  om::mc::contactsFind(contacts, pub) < 0
FAIL test_dm_guard.cpp:134  contacts.count == 0
FAIL test_dm_guard.cpp:160  contacts.count == 1          <- a rebound thread added a SECOND row
FAIL test_dm_guard.cpp:214  dropped == 1                 <- B/NVS: the prefix-only record survived the load
FAIL test_dm_guard.cpp:215  loaded.count == 1
FAIL test_dm_guard.cpp:224  again.count == 1
FAIL test_dm_guard.cpp:231  om::mc::contactsDropIncomplete(only_bogus) == 1
FAIL test_dm_guard.cpp:232  only_bogus.count == 0
FAIL test_dm_guard.cpp:242  !om::mc::dmPeerKey(s, prefix, name, sizeof(name))   <- a 4-byte "contact" was sendable
FAIL test_dm_guard.cpp:246  !om::mc::dmPeerKey(s, prefix, name, sizeof(name))
FAIL test_dm_guard.cpp:254  !om::mc::dmPeerKey(s, prefix, name, sizeof(name))
FAIL test_dm_guard.cpp:257  got "" want "ERA-3 (SI)"
FAIL test_dm_guard.cpp:263  !om::mc::dmPeerKey(bad, prefix, name, sizeof(name))
FAIL test_dm_guard.cpp:335  ring.seen(meshcore::frameFingerprint(echo), &ours)   <- our own echo was NOT recognised
FAIL test_dm_guard.cpp:336  ours
FAIL test_dm_guard.cpp:350  ours2
FAIL test_dm_guard.cpp:420  meshcore::kSeenSlots == 32
FAIL test_dm_guard.cpp:428  ring.seen(1, &ours)
FAIL test_dm_guard.cpp:431  !ring.seen(1, &ours)
FAIL test_dm_guard.cpp:449  ring.seen(7, &ours)
FAIL test_dm_guard.cpp:450  ours
FAIL test_dm_guard.cpp:455  ours
dm guard: 23 of 169 checks FAILED
```

With the fixes in place: **`dm guard: 169 checks passed`**. What it pins, case by case:

- `dmRestoreThread()`: a prefix-only log creates **no** contact, the thread is named from its records (or the 8-hex
  prefix when only our own messages are there), the Meta point lands on the first record's new `seq`; a UNIQUE
  full-key row is bound to the thread (one row, not two); two rows sharing the prefix leave it orphaned.
- `contactsDecode()` + `contactsDropIncomplete()`: the real NVS blob with a prefix-only record loads and the row is
  gone; the cleaned table re-encodes and re-decodes to one row; a table of only bogus rows comes back empty.
- `dmPeerKey()`: refused with no contact, refused for the four bytes even when a full key with the same prefix
  exists, refused when the stored row IS the prefix, allowed for the full key (with its name).
- `SeenRing` + `frameFingerprint`: a DM we composed **and registered** is dropped when it comes back byte for byte
  **and** through a repeater (same payload, `path_len 0x41` + 2 path bytes); the same frame with our own timestamp
  and text DOES open against the colliding peer (proving why the drop is needed); a genuine DM from that peer is
  new, is not dropped, and `dmOpen()` reads it; a peer's duplicate is dropped as a duplicate, not as our echo;
  the ring is 32; `noteOurs` is idempotent and can upgrade a heard entry to "ours".

`pub[0] == peer.pub[0]` is not assumed: the test derives seeds until it finds a **real** Ed25519 key whose first
byte equals ours (the arithmetic seed family used first turned out correlated — 3000 such seeds reached only 147
of the 256 first-byte values — so it uses an xorshift; the comment says so).

### Results

- `sh test/host/run.sh` → **exit 0**, every stage green:
  `core 192 · clock 91 · store 127 · history 84 · layout 62 · mc 115 · dm 45 · mc-asan 115 · dm-asan 45 ·
  dm-send 140 · dm-send-asan 140 · map 267 · txqueue 93 · chat-cards 99 · menu-cards 23 · advert 162 ·
  contact-blob 1190 · channel-scope 94 · dm guard 169` — **3253 checks, 0 failures**, and the two ASan+UBSan
  stages clean (the `runtime error: left shift of negative value` lines are the vendored Ed25519 C, present before
  this ticket and not our code).
- `make build` → **exit 0**; `application.bin` **0x166300 = 1 467 136 B**, smallest app partition 0x200000,
  **0x99d00 (30%) free**.
- **Nothing was installed, flashed or committed**; EST-440's uncommitted work is untouched.

### Files

- `core/mc_wire.h/.cpp` — `kSeenSlots = 32`, `frameFingerprint()`, `SeenRing{seen,noteOurs,reset}`.
- `core/mc_store.h/.cpp` — `contactKeyIncomplete()`, `contactsDropIncomplete()`, `contactsFindUniquePrefix()`,
  `dmPeerKey()`.
- `core/mc_history.h/.cpp` — `RestoredDm`, `dmRestoreThread()` (the per-thread restore, no contact).
- `main/mc_link.cpp` — the ring and `markOwnFrame()`; `queueFrame`/`doText`/`doAdvert` register before the frame
  goes out; one RX check after `decode()`; the drop on contacts load with its one log line; `rebindRestoredDmPeer`
  rebinds threads only; `dmPeerReady()`; `queueDm()` refuses a peer without a full key.
- `main/sd_history.cpp` — `restoreDmThreads()` restores threads through `dmRestoreThread()`, and no longer calls
  `contactsUpsert()` (the `prefixOnlyKey()` helper there is gone).
- `main/mc_link.h`, `main/main.cpp` — `dmPeerReady()` declared; the DM view keeps the text and says
  `No key for <name> yet: waiting for its advert` instead of "radio busy".
- `test/host/test_dm_guard.cpp` (new), `test/host/run.sh` (one stage).

### What could NOT be checked here

- **The air.** Nothing was flashed, so the two defects are fixed in code and pinned on the host, not observed on a
  radio. The checks to run on the next flash: a DM to ERA-3 completes (ACK + `DELIVERED`), ERA-3's DMs open
  (`src 0x42` is no longer "no contact of ours opens it"), and the log shows `our own echo, dropped` where it used
  to show `DM from "ERA-3 (SI)"` for our own text.
- **NVS on the device.** The drop runs on the decoded table; that the written-back blob really loses the row on
  Filip's unit is a device check (`contacts: dropped 1 record(s) without a full key`, then nothing on the next boot).
- **`main/` on the host.** `mc_link.cpp`, `sd_history.cpp` and `main.cpp` need the IDF and the card, so their
  compile is `make build` and their behaviour is reviewed, not unit-tested; the DECISIONS they call are the ones
  the new test pins.

### Next

1. Claude: review, then flash v42 and run the three checks above.
2. A DM thread restored from the card is now unread-blind until it is rebound (no contact row holds `dm_seen`); the
   DM column shows it with no badge rather than a wrong one. If that bothers, the fix is a thread-level seen point,
   not a contact.

## 2026-11-18 · S513 (EST-438) · per-channel flood scope

> Written by DeepSeek for ticket **EST-440** (it replaces EST-438, stopped before it changed a file): fenced
> Paperclip run, no session start/end, nothing installed, flashed or committed. On top of commit `8c8d25a`
> (v41). **Version: 42.** This entry is written FIRST, as the ticket asks; Results is filled in as the work finishes.

### Read before writing anything

- The L2's implementation, not a memory of it: `firmware/src/ui/channel_scope.h/.cpp` (the modes and the name
  rules), `firmware/src/mesh_link.cpp` `sendChannelText()` (S509), `firmware/src/channels.cpp`
  `mcChannelScope()/mcChannelSetScope()` (NVS), the S509 entry in `firmware/docs/PROGRESS.md` (including its two
  found defects: read with the **LIVE** channel hash, and `remove()` answers NOT_FOUND for a key that never
  existed), and the MeshCore sources it cites: `helpers/TransportKeyStore.cpp` (`calcTransportCode`,
  `getAutoKeyFor`), `helpers/RegionMap.cpp:182-203`, `Mesh.cpp:664-677` (`sendFlood` with transport codes),
  `Packet.cpp:37-80` (`writeTo`/`readFrom`) and `Packet.h:14` (`ROUTE_TYPE_TRANSPORT_FLOOD`).
- On the Tanmatsu side: `BRIEF.md`, this PROGRESS, `core/mc_wire.*` (the decoder already skips 4 transport
  bytes for route 0/3), `main/mc_link.cpp` (channel load, `doText`), `main/chat_page.*` and `main.cpp`'s
  `ChatView` fill.

### 1. The model (`core/channel_scope.*`, pure and ported)

- The three modes are the L2's: **Default** (channel never set), **No scope** (the stored sentinel `!none`;
  send unscoped even if the node has a default) and **Region** (a `#tag` name). Resolution is
  **channel -> node default -> unscoped** (`scopePlanFor`). The Tanmatsu node has no stored default scope, so
  Default currently resolves to **unscoped**; the parameter exists so the rule is the L2's, not a second rule.
- The name rules are ported byte-for-byte from `channel_scope.cpp`: leading spaces and one `#` dropped, then
  spaces; a leading `$` is refused (a private region's key cannot be derived from its name); 1..30 characters,
  every one passing MeshCore's `RegionMap::is_name_char` (`rptr_model.h:68-75` in the L2); a refused name writes
  nothing. **Case is kept**, so `#si` and `#SI` are different regions.
- The region key is `sha256("#name")[0..15]` (`TransportKeyStore::getAutoKeyFor`, called with `#name` by
  `RegionMap::getTransportKeysFor`), and the 2-byte code is
  `HMAC-SHA256(key, payload_type || payload)[0..1]`, interpreted little-endian as MeshCore's
  `finalizeHMAC(..., &code, 2)` does, with `0x0000 -> 0x0001` and `0xFFFF -> 0xFFFE`.
- The frame builder writes MeshCore's exact order (`Packet::writeTo`): `header` with
  `ROUTE_TYPE_TRANSPORT_FLOOD` (0x00), **both 16-bit transport codes**, then `path_len`, path, payload. Codes
  are `{this region's code, 0}` exactly as `sendChannelText()` and upstream's `sendFloodScoped()` do; the
  second is "we carry no home region". No scope builds the plain flood through the existing
  `meshcore::buildGroupText` (route 1, no codes).

### 2. Storage — the L2's namespace, key and value format

- Namespace **`mcch`**, key **`s<hh>`** (two lowercase hex digits) of the channel's **live** 1-byte hash, value
  a string: absent/empty = **Default**, `!none` = **No scope**, anything else = the region name without the
  leading `#` (what the L2's editor stores: `scopeNameCanonical()` then `mcChannelSetScope()`). The reader
  canonicalises a value again and falls back to Default on a corrupt one, so a `#si` written by hand and the
  L2's `si` are one rule.
- Both directions use the live hash from the loaded table, never a mirrored copy from an older secret (S509's
  first defect). Removing a value that is not there is **success**: NVS `NOT_FOUND` on erase is the Default
  state, not a failure (S509's second defect).
- No editing UI yet (Settings is later). Claude provisions the three values with BadgeLink `nvs` writes:

```
nvs write mcch s11 str !none
nvs write mcch sec str si
nvs write mcch s95 str europe
```

`0x11` = Public (no scope), `0xEC` = #slovenija (region `#si`), `0x95` = #slovenia (region `#europe`). The
stored region text is canonical without `#`, as on the L2; a `#si`/`#europe` value is also accepted on read.
(The channel names, order and keys are Claude's provisioning; no key is written in this repo or in a test.)

### 3. Sending (`main/mc_link.cpp`)

- `doText` resolves the channel's stored scope once per send and builds either the scoped transport-flood frame
  or the plain flood. The TX log names the mode the way the L2 does:
  `TX ch1 "#slovenija" ... scope=region "#si"` / `scope=none` / `scope=unscoped`.
- Nothing else on the send path changed: still one owner of the radio, still queued by the main task.

### 4. What is visible

- The channel card's second line is unchanged. In the OPEN channel the page head's right-hand text (already
  the dim face) gains the scope when it is not unscoped: `scope #si`, `scope #europe`, or `no scope`, before
  the message/notice text. A DM thread and a Default/unscoped channel show no scope text.

### Results — code, tests and build done (nothing installed)

- **Host:** `sh test/host/run.sh` → **exit 0**, **17 stages, 3061 checks, 0 failures**. **Before: 2967
  checks, 16 stages**; the new stage is `test_channel_scope` with **94 checks**, so the arithmetic is exact
  (2967 + 94 = 3061). The ed25519 UBSan `left shift of negative value` lines are the pre-existing vendored
  C, not ours. Logs: `paperclip-pilot/sweep/est440-baseline.log` and `est440-host-final2.log`.
- **What the 94 checks pin:** the three modes in a `constexpr` context; the name rules (`#` dropped,
  spaces trimmed, `$` refused, `si lj`/`si.lj` refused, 30 accepted / 31 refused, case kept, nothing written
  on a refusal); resolution Default -> node default -> unscoped and No scope beating a node default; a
  corrupt record falling back to Default; the head words; the region keys for `#si`/`#SI`/`#europe`; the
  transport codes for two fixed payloads (`0xa609`, `0x321d`) and `#SI`'s different code (`0xe9e3`); the
  complete 41-byte scoped frame byte for byte (header 0x14, codes `41 f2`, path_len 0x40, payload), the
  decoder reading those 4 bytes and `openGroupText` opening the frame; `#si` and `si` producing the same
  frame; No scope / Default / a null node key producing the plain flood, byte-identical to
  `buildGroupText` (route 1, no codes); a too-small buffer and hash_size 4 refused.
- **ARM:** two deliberately broken copies in `paperclip-pilot/sweep/est440/arm/` and `arm2/`, each compiled
  against the real test. Break 1 (region key hashed without `#`): **ARM_EXIT=1, 59 checks, 2 FAILURES**
  (the `#si` key vector and the fixed frame). Break 2 (the 2-byte code read big-endian): **ARM_EXIT=1, 68
  checks, 2 FAILURES** (`code == 0xa609` got 2470, and the fixed frame). The real tree is **94 checks, 0
  failures**.
- **Build:** the first `make build` returned **exit 2** — two `-Werror=format-truncation` errors of mine
  (`chat_page.cpp:102-103` composing scope + head text, `mc_link.cpp:968` composing `region "#name"` into a
  40-byte buffer). Fixed with a 96/128-byte head buffer and bounded `%.31s`/`%.90s` (plus a 48-byte scope
  log buffer), then **`make build` → exit 0**, **0 `warning:`/`error:` lines**. `application.bin` =
  **0x165a10 = 1,464,848 B**; **0x9a5f0 = 632,816 B (30 %) of the 2 MB app partition free**. That is
  **+2,608 B** over EST-439's 1,462,240 B (the ported model, the builder, the NVS glue and the head text).
  Logs: `paperclip-pilot/sweep/est440-build.log` (the failure) and `est440-build2.log` (green).
- **Files:** `core/channel_scope.{h,cpp}` (new, pure), `core/mc_wire.h` (`kRouteTransportFlood`),
  `core/CMakeLists.txt`, `main/mc_link.{h,cpp}` (NVS `mcch` `s<hh>` + `doText`), `main/chat_page.{h,cpp}`
  (the head text), `main/main.cpp` (fills it), `test/host/test_channel_scope.cpp` (new),
  `test/host/run.sh` (the new stage), `docs/PROGRESS.md`.
- **Checked for rule 9:** no channel key, real or otherwise, is in the code, notes or test. The only key in
  the test is the synthetic `00 01 ... 0f` used to pin the frame bytes. The three values in the NVS commands
  are a mode sentinel and two public `#tag` names; the channel hashes are the ones the ticket gives.
- **Not checked:** nothing ran on hardware, so no repeater has yet forwarded or refused one of these scoped
  floods and the NVS writes below have not been executed on a device. No PNG: this ticket changes one line
  of existing head text and asks for no preview.

### Next

- Install v42, provision the three scopes with the `nvs write` lines above, then send one text on each
  channel. The device log must read `scope=none` (Public), `scope=region "#si"` (#slovenija) and
  `scope=region "#europe"` (#slovenia); opening each channel must show the same words on the head. A
  repeater that carries the region should forward it; one that does not must drop it (S509's own open item
  1, still unproven on the L2 too).


## 2026-11-18 · S513 (EST-439) · DM send, wired

> Written by DeepSeek for ticket EST-439 (fenced Paperclip run: no session start/end, nothing installed, flashed or
> committed). Continues EST-436: its core (`core/dm_send.*`) is unchanged except for item 0 below. **Version: 42.**

### 0. One defect first: a zero-hop direct route is a route
- `dmCompose()` refused `path_bytes == 0` ("is not a route"). In MeshCore the ONLY "no route" value is
  `OUT_PATH_UNKNOWN` (`ContactInfo.h`; `MeshCore.h:19`, 0xFF); `sendMessage()` tests exactly that and otherwise calls
  `sendDirect(pkt, out_path, out_path_len)` (BaseChatMesh.cpp:481-486), which is how a neighbour heard directly is
  sent to — path count 0, hash-size bits kept.
- Fixed: `dmCompose()` floods only for a null path OR `out_path_len == kDmPathUnknown` (0xFF); every valid encoding is
  DIRECT, count 0 included. The test that pinned the refusal now pins the opposite, and it was **armed red against
  the old rule** (`sweep/est439/arm0-old/`, new test + old `dm_send.cpp`): **12 failures**, then 0 with the fix.
- New checks also pin the oracle's direct frame byte for byte with the two path bytes removed, the 0x00 (1-byte
  hashes, count 0) case, and that 0xFF still floods.

### 1. The wiring: queueDm → radio task → ACK/PATH → main-task retry
- `mc::queueDm(pub, text)` (MAIN task) composes attempt 0 against the contact's stored route, writes the outgoing
  message into the thread and `dm-<8 hex>.log` as **SENDING**, begins the pending entry (expected ACK computed over OUR
  pubkey), then `pushFrame`s it. It never transmits, and it refuses BEFORE storing anything when the send queue is
  full — so the caller keeps its text.
- The RADIO task handles the answer, and still never transmits from the RX path: a **PATH** packet is opened with
  `dmOpenPath` (decrypted route + extra), the route is stored on the contact (and marks the NVS blob dirty), and an
  embedded ACK clears the pending entry; a plain **ACK** (4-byte payload) matches the same table. The delivered mark
  is written under the store lock; the reply the radio builds still leaves through the tx queue.
- `mc::dmTick()` (MAIN task, every loop) polls `dmPendingTimeout`: retry attempt 1 then 2, the LAST attempt is a flood,
  and a DIRECT attempt that timed out **clears the stored route**; after attempt 2 the message becomes
  **no confirmation**. A retry is composed and QUEUED, never sent inline; a retry that cannot be queued is postponed
  300 ms without spending the attempt (`dmPendingDelay`).
- Timeouts are the companion's formula (MyMesh.cpp:888-895). The C6 lora component has no `getEstAirtimeFor()`, so
  `dmAirtimeMillis()` is the SX1262 time-on-air formula at `kSloPlan`'s numbers (SF8 / 62.5 kHz / CR 4/8 / preamble
  32). It only sizes a timeout; the ACK still decides delivery. A direct arrival is now ACKed along the stored route
  when there is one (`dmBuildAck`), and flooded only for OUT_PATH_UNKNOWN.

### 2. The route survives a restart — contact blob version 2
- `Contact` grew `out_path_len` + `out_path[kMaxPath]` (default 0xFF/zero), filling EST-437's reserved slot, so the
  record width stays 148. `kContactBlobVersion = 2`; **a version-1 blob still loads** and every route comes back
  OUT_PATH_UNKNOWN. A stored encoding that is not valid (hash-size 4 reserved, or too many bytes) is treated as
  unknown rather than trusted. Tests: every field round-trips (including a zero-hop 0x40), v1 loads, a bad route byte
  is ignored, and the encoder writes 0xFF (not a zero-length direct path) for an unknown route.

### 3. UI: the DM thread has the channel's composer, and our lines carry their state
- A DM thread types and sends exactly like a channel: Enter calls `queueDm`; a full queue is refused and the text
  stays in the composer with **"radio busy, not sent"** in the head slot for 3 s. Backspace deletes a UTF-8 sequence.
  The "sending DMs comes next" line and the DM-only hint bar are gone.
- Our message shows at once on the right under **"you"**, with its state AFTER the time: `09:52 sending` →
  `09:48 delivered` → `no confirmation` after the last attempt. The font has no check glyph, so the word is drawn;
  the state values are the L2's own `om::ui::DmState` (`firmware/src/ui/dm_delivery.h`).
- Outgoing DMs are written to `dm-<8hex>.log` with the format's outgoing flag (`Msg::mine`). The delivered mark is
  kept across a restart **the L2's way**: the message record carries the format's `delivery` byte, and the ACK appends
  an **Update** record (`history_log.h` Type::Update / `kUpdateState`, naming the log id); `historyLoadDm` applies
  Update records in file order. A power cut before the ACK therefore comes back as "sending", never as delivered.

### 4. Preview
- `shots/2026-11-18-dm-send.png` (host render, `tools/preview_tanmatsu.py dm_send`): two messages from Alpha on the
  left, two from us on the right — `09:48 delivered` and `09:52 sending` — above the shared composer.

### Results
- `sh test/host/run.sh` → **exit 0**. Totals (checks): core 192, clock 91, store 127, **history 84** (the delivery
  Update round trip is new), layout 62, mc 115, dm 45, mc-ASan 115, dm-ASan 45, **dm_send 140** (was 113; drop/delay/
  airtime are new), dm_send-ASan 140, map 267, txqueue 93, chat cards 99, advert 162, **contact_blob 1190** (v1 load
  and route round trip are new). The UBSan lines from the vendored ed25519 `fe.c/ge.c` are pre-existing and not ours.
- `make build` → **exit 0**, `application.bin` **1,462,240 B (0x164fe0), 0x9b020 B (30 %) of the 2 MB app partition
  free** — +12,002 B over v42/EST-437 (1,450,238 B), no warning from our own files.
- Files: `core/dm_send.{h,cpp}`, `core/mc_store.h`, `core/contact_blob.{h,cpp}`, `core/mc_history.{h,cpp}`,
  `main/mc_link.{h,cpp}`, `main/chat_page.{h,cpp}`, `main/main.cpp`, `main/sd_history.{h,cpp}`,
  `test/host/test_dm_send.cpp`, `test/host/test_contact_blob.cpp`, `test/host/test_history.cpp`,
  `tools/preview_tanmatsu.py`, `shots/2026-11-18-dm-send.png`.
- **Not checked:** nothing ran on hardware, so no real ACK/PATH was received, no retry was seen on the air, and the
  PNG is a host render. The radio queue, the NVS write and the 10-minute floor are the EST-437 paths, unchanged.

### Next
- Install v42 and try a DM to a real neighbour: the log line `DM: route to "<name>" stored (path_len 0x40, 0 hops)`
  is the one-hop case item 0 fixed; `DM: message N DELIVERED` is the ACK; the thread's state must read `delivered`
  after a restart (the Update record). A neighbour that never answers should show `no confirmation` after ~3 attempts.

## 2026-11-18 · S513 (EST-437) · contacts kept + advert notices

> Written by DeepSeek for ticket EST-437 (fenced Paperclip run: no session start/end, nothing installed, flashed or
> committed). **Version for Claude's install: 42** (one image with EST-435..437). This entry is written FIRST, as the
> ticket asks; the Results section is filled in as the work finishes.

### 1. Contacts kept across restarts — NVS namespace `mcct`, one versioned blob
- `core/contact_blob.{h,cpp}` is the pure codec: header `version(1) | count(1) | record_size(2, LE)`, then one
  92-byte record per contact (`pub 32`, `name 24`, `type 1`, `path_len 1`, `remote_ts 4`, `heard_at 4`, `rssi 2`,
  `snr 2`, `favourite 1`, `dm_seen 4`, `dm_seen_log 4`, `dm_log_next 4`, and a **reserved out_path slot
  `len 1 + bytes 8`** — EST-436 did not add an out_path to `Contact`, so it is reserved, not guessed).
  No NVS, no clock, no radio in this file; the host tests drive it directly.
- `main/mc_link.cpp` owns the NVS I/O. `restoreContacts()` runs at boot **before the radio task exists** and logs
  `contacts: N restored from NVS`; a bad version, record size or length is ignored with one warning line. A wrong
  blob can never half-fill the table: the decoder validates every header field BEFORE its first write.
- Writes are **never made on the radio task**. An advert marks a dirty flag instead: add / rename / route (path)
  change is written on the next main-loop pass; last-heard / SNR changes at most once per 10 minutes. The blob is
  encoded under the store lock into a static buffer, the flags are cleared before the lock is released, and only
  then is NVS written — a mutation during the write sets the flag again. **Route change currently means
  `path_len`**: EST-436 added no route bytes to `Contact`, and the reserved `out_path` slot is zeroes today.

### 2. Advert notices — the two sentences item 2 asks for
- `doAdvert()` sets **"Advert sent"** when `radio::send()` returned > 0 and **"Advert NOT sent"** otherwise, for 3 s,
  through the shared chrome notice slot (already safe from the radio task).
- The advert RX branch takes `contactsFind()` **before** `contactsUpsert()`; only a genuinely added contact (not a
  known one, not our own echo, which returns earlier) shows **"New node: <name> (<type>)"**, with the L2's
  `node_menu.h` words `client / repeater / room / sensor / node` (a full table adds nothing, so it says nothing).

### Results — both items done

- **Host:** `sh test/host/run.sh` → **exit 0**, **16 stages, 2838 checks, 0 failures**; no AddressSanitizer report
  (the ed25519 `left shift of negative value` UBSan lines remain the vendored third-party C). The new stage is
  **1125 checks** (`test_contact_blob`, exit 0). **Before: 1736 checks.** The arithmetic reconciles exactly:
  1736 − 23 (a stray duplicate `"$MENU"` run left at the end of run.sh by EST-435; it tested the same 23-check
  suite twice, and this stage replaces it as the last line) + 1125 = **2838**. Logs:
  `paperclip-pilot/sweep/est437-host-before.log` and `est437-host-final.log`.
- **Build:** `make build` → **exit 0**, **0** `warning:` / `error:` lines. `application.bin` =
  **0x162290 = 1,450,640 B**, **0x9dd70 = 646,512 B (31 %) of the 2 MB app partition free**; total image
  **1,450,238 B**; **DIRAM 194,036 B (33.9 %)**. v41 was 1,447,456 B, so this is **+3,184 B** (the blob codec,
  the NVS glue and the notice text). Logs: `paperclip-pilot/sweep/est437-build-final2.log`,
  `est437-size-final2.log`. One compile fix worth naming: adding `chrome.h` to `mc_link.cpp` collides with
  `<time.h>` (`struct tm` vs `namespace tm`), so the file now asks `time_source::epochNow()` — exactly the rule
  `time_source.h` already states — instead of including `<time.h>`.
- **ARM, against a codec with five deliberate rule breaks** (copy in `paperclip-pilot/sweep/est437-arm/`):
  header record size written as 147, version check removed, truncated reported as wrong-size, count limit removed,
  name terminator not forced → **ARM_EXIT=1**, **95 checks, 8 FAILURES**, log
  `paperclip-pilot/sweep/est437-arm.log`. The real tree is **1125 checks, 0 failures**. The FAIL lines pin the
  wrong version, truncation, extra bytes and full-table cases, not just the happy path.

### Files changed

- **new** `core/contact_blob.{h,cpp}` — the versioned blob codec; reserved `out_path_len + out_path[kMaxPath]`
  slot (EST-436 added no `Contact.out_path`), no NVS/clock/radio inside.
- **new** `test/host/test_contact_blob.cpp` — 1125 checks: empty table, full-field round trip, wrong version,
  wrong record width, every truncation of a two-row blob, extra bytes, over-large count, the full 32-row table,
  unterminated name, the reserved zeroed route slot, short-buffer refusal.
- `test/host/run.sh` — the new stage; the stray duplicate `"$MENU"` at the end replaced by it.
- `core/CMakeLists.txt` — `contact_blob.cpp` added to the component.
- `main/mc_link.{h,cpp}` — `restoreContacts()` (before the radio task, `contacts: N restored from NVS`, wrong
  blob ignored with a warning), `contactsPersistTick()` (main task; structural immediate, last-heard/SNR 10-minute
  floor, 10 s retry after a failed write), dirty flags set on add/rename/route/rebind, and the two advert notices.
- `main/main.cpp` — `restoreContacts()` before `restoreDmHistory()` and before `core.start()`; `contactsPersistTick()`
  in the main loop.
- `main/chrome.cpp` — notice slot 40 → 64 bytes so a 44-byte "New node: <name> (<type>)" is never cut.
- `docs/PROGRESS.md` — this entry.

### Not done / not verified

1. **Nothing was installed or flashed**, so the NVS read/write and both notices are proven by the host tests and
   the build, not on the glass. The exact log line to look for after Claude's install is
   `contacts: N restored from NVS`.
2. The NVS blob is **not** CRC-protected; the ticket asked for version/size, and a fixed 148-byte record plus an
   exact length catches truncation and a format change. A corrupted-but-right-sized record would decode as data.
3. Last-heard/SNR is written at most every 10 minutes **and not at all in the first 10 minutes after boot** (the
   floor starts at boot); an add/rename/route change is never delayed by that floor.
4. No PNG was produced (this ticket is code + tests only).
5. **Version for Claude's install: 42** — one image carrying EST-435, EST-436 and EST-437.


## 2026-11-18 · S513 (EST-436) · direct messages slice 2: send + delivered — the CODE RULES, not the wiring

> Written by DeepSeek for ticket EST-436 (fenced Paperclip run: no session start/end, nothing installed, flashed or
> committed). Verdict: **item 1 is done; item 2's rules are done in the core and NOT wired; items 3, 4, 5 were not
> started.** The ticket's **30-minute cap** is why, and item 3 has a precondition that is not in the tree.

**Read first:** `BRIEF.md`; this file's S513 entries (EST-435, EST-433/434); `core/mc_dm.*`, `core/mc_store.*`,
`core/tx_queue.*`; `main/mc_link.{h,cpp}` (the TXT_MSG RX path and `serviceRequests()`), `main/chat_page.*` and
`main/main.cpp` (the channel composer); and, line by line, upstream: `helpers/BaseChatMesh.cpp:43-58, 222-270,
341-384, 451-492, 989-994`, `Mesh.cpp:117-128, 160-205, 441-486, 488-505`, `MeshCore.h:19`,
`Packet.cpp:13-18`, and the companion's `MyMesh.cpp:108-112, 417-438, 888-895`.

### 1. The core rules — `core/dm_send.*` (DONE, host-tested)
Every rule is a port with a citation, not an invention:
- **Compose** (`dmCompose`, BaseChatMesh.cpp:451-471 + Mesh.cpp:488-505): `plain = ts(4, LE) | (attempt & 3) | text`
  (length `5 + text_len`, NUL excluded), `attempt > 3` appends NUL + the full attempt byte (and refuses
  `text_len > MAX_TEXT_LEN-2`); `expected_ack = sha256(plain || OUR pub)[0..4]` — the hash stops BEFORE the
  extended-attempt tail; payload `= THEIR pub[0] | OUR pub[0] | encryptThenMAC(secret, plain)`. No stored route →
  the usual FLOOD (`path_len 0x40`, 2-byte hashes, count 0); a stored route → DIRECT with `out_path_len` copied
  back and its bytes, exactly `sendDirect`.
- **PATH parse** (`dmOpenPath`, Mesh.cpp:160-180): payload is `dest(1) src(1) | sealed`, sealed decrypts to
  `path_len | path | extra_type | extra`. `path_len` is validated with `Packet::isValidPathLen`'s own rule
  (Packet.cpp:13-18: hash_size 4 is reserved, count*hash_size ≤ MAX_PATH_SIZE), and **every offset is checked
  against the decrypted length before it is used**, because `extra_len` is "remainder of packet (may be padded
  with zeroes)". A PATH with a bad/truncated length yields nothing.
- **ACK matching** (`dmPendingBegin/ Rearm/ Ack/ Timeout`): 8 slots, the oldest reused when full (never drop a new
  send), matched on **four bytes** as `MyMesh::processAck` does (MyMesh.cpp:425), and a matched slot is cleared
  because "the same ACK can be received multiple times" (MyMesh.cpp:431). A plain ACK packet is
  `dmAckPayload`: ACK header, payload ≥ 4 bytes, first four are the crc (Mesh.cpp:117-121). Our own EST-434
  builder's **6**-byte ACK (crc + attempt + random) is therefore accepted, which is why the test pins both 4 and 6.

### 2. The retry rule — the ticket's, and what upstream actually does (rules done, wiring NOT done)
**Said plainly, because the ticket asked:** the retry loop is **not in this tree's upstream library**.
`BaseChatMesh::loop()` only calls `onSendTimeout()` when `txt_send_timeout` expires
(BaseChatMesh.cpp:989-994), and the companion leaves `MyMesh::onSendTimeout()` **empty** (MyMesh.cpp:897); the
phone app drives attempts. The L2's own app did retry (`firmware/src/mesh_link.cpp:570-596, 1564-1574`,
attempt 0..3 on `est_timeout`), but it **never forced a flood** and **never cleared a path**. So, per the ticket's
fallback: retry on timeout, **the last attempt is a flood even with a stored path**, and **a failed DIRECT attempt
clears the stored path**. `dmPendingTimeout()` returns exactly that (`Retry` with `flood`/`clear_path`, or
`GiveUp`), and nothing in it transmits. The per-attempt deadlines are the companion's arithmetic, copied:
`flood = 500 + 16 * airtime_ms`, `direct = 500 + (airtime_ms*6 + 250) * (hash_count+1)`
(MyMesh.cpp:888-895, constants :108-111).
**Not done:** wiring this into `main/mc_link.cpp` (a `TxKind`/queue path for a composed DM, the deadline poll next
to the advert schedule, and the delivered state in the DM store). No half-built version is in the tree.

### 3. out_path in EST-435's NVS contact record — BLOCKED, the record does not exist
EST-435's own entry above says item 3 (**contacts across restarts, NVS**) was **NOT DONE**: `ContactStore` is still
RAM only, there is no NVS namespace, no encoder and no record version. So there is nothing to bump. Doing this
ticket's item 3 would first mean building the whole contact record (a separate ticket's worth). Recorded rather
than guessed; no NVS code was written here.

### 4 and 5. UI composer, the state after the time, the preview — NOT STARTED
`chat_page.cpp:203-205` still draws the dim **"sending DMs comes next"** line, `main.cpp:917-946` still refuses
Enter and typing while a DM is open, and there is no `shots/2026-11-18-dm-send.png`. Nothing was half-wired.

### Results (real exit codes)
- `sh test/host/run.sh` → **exit 0**, **16 stages, 1736 checks, 0 failures**, no AddressSanitizer report (the
  ed25519 `left shift of negative value` UBSan lines are the vendored third-party C and pre-existing).
  **Before this change: 14 stages, 1510 checks** (EST-435's own final figure). **+226 checks** = the new stage
  **113** checks twice (normal **and** the new ASan stage); 1510 + 226 = 1736, which is the measured total.
  Log: `paperclip-pilot/sweep/est436-host-3.log` (the two runs before it, `-1` and `-2`, are the red steps on the
  way: my first test vectors assumed the oracle's frame was the direction we SEND, which it is not).
- `make build` → **exit 0**, **0** `warning:`/`error:` lines. `application.bin` = **0x161620 = 1,447,456 B**,
  **649,696 B (0x9e9e0, 31 %) of the 2 MB app partition free** — the **same size as v41**, and that is expected:
  `dm_send.cpp` is compiled into the component but nothing calls it yet, so the linker drops it. Log:
  `paperclip-pilot/sweep/est436-build.log`.
- **ARM, two wrong rules, both red** (copies in `paperclip-pilot/sweep/est436-arm1|2/`):
  - wrong ACK key (the recipient's pub instead of ours): **ARM1_EXIT=1**, **1 failure**,
    `FAIL test_dm_send.cpp:125  memcmp(c.expected_ack, full, 4) == 0`.
  - retry rule without the last-flood / path-clear rule: **ARM2_EXIT=1**, **2 failures**,
    `FAIL test_dm_send.cpp:273  d.flood` and `FAIL test_dm_send.cpp:274  d.clear_path`.
  - The real tree is **113 checks, 0 failures** (and the same 113 under ASan).
- **PNG: none** (item 5 not reached). **Version: not bumped** — 41 stays the version for Claude's install, because
  nothing in this entry changes the installed image's behaviour (the new core file is compiled but not called).

### Files changed
- **new** `core/dm_send.h`, `core/dm_send.cpp` — compose, PATH parse, the pending/ACK table, the retry rule.
- **new** `test/host/test_dm_send.cpp` — 113 checks: the oracle's frame/seal mirrored for our direction, the ACK
  rule from its inputs, direct vs flood, bad lengths, a real PATH round trip from a derived peer identity, every
  truncation, ACK matching (right/wrong/twice/full table), the retry timeline at exact instants, the timeout maths.
- `test/host/run.sh` — one normal stage and one ASan stage (the PATH parser carries air-sourced lengths).
- `core/CMakeLists.txt` — `dm_send.cpp` added to the component.
- `docs/PROGRESS.md` — this entry.

### Next step (for a fresh cap, in the ticket's order)
1. Wire item 2: a `queueDm(contact_index, text)` that composes, `pushFrame`s, begins a pending entry; the radio
   task resolves it with the value `radio::send()` returned; a poll next to the advert schedule asks
   `dmPendingTimeout()`. 2. Item 3 after EST-435's contact NVS exists. 3. Items 4-5 (composer + state + PNG).

## 2026-11-18 · S513 (EST-435) · contacts kept, advert notices, one advert per 5 s

> Written by DeepSeek for ticket EST-435 (fenced Paperclip run: no session start/end, nothing installed, flashed or
> committed). Code + host tests + build only. **Version for Claude's install: 41.** The ticket carries a
> **30-minute cap**: the numbered items are done in order and this entry says exactly where the run stopped.

**Ticket's three asks (Filip on the glass):** contacts must survive a restart, an advert must pop a notice (ours
sent, and a NEW node heard), and at most one advert per 5 s so a key held down cannot flood the mesh.

**Read first:** `BRIEF.md` in full; this file's S513 entries (EST-433/434, EST-432, EST-430, EST-429); `core/mc_store.*`;
`main/mc_link.cpp`; `main/chrome.*`; `main/main.cpp`'s two advert key branches and the EST-428/EST-432 notice pattern;
the L2's `firmware/src/ui/new_node.h` (whole file) and `advTypeName` in `firmware/src/ui/node_menu.h`.

### 1. One advert per 5 s — the rule in `core/advert_rule.*`, armed first
- `AdvertGate` holds only what the rule needs — `ever_sent`, `last_tx_ms`, `in_flight` — and EVERY function takes
  `now_ms` as an argument: no clock, no NVS, no radio in the pure file, so the host tests drive exact instants.
- `advertRequest()` answers `Send` / `Wait` / `Busy`. `Wait` carries the **whole seconds left**,
  `ceil((5000 - elapsed)/1000)` — so +1.0 s reads "wait 4 s" and +4.9 s reads "wait 1 s", and only +5.0 s sends.
  `Busy` is "an advert we queued has not been transmitted yet": a second one is refused regardless of the clock.
- `advertSent(gate, now_ms, ok)` is called where the transmission actually happened, from the value
  `radio::send()` returned, so the 5 s counts from the AIR, not from the key press. A failed send does not start
  the clock (nothing was transmitted) and says "Advert NOT sent".
- The SAME rule covers the 3-hour schedule: `advertRequest()` is asked before the schedule queues anything, so a
  manual advert 2 s ago also defers the schedule. That is the point — the mesh sees one advert, whoever asked.
- **Which text for `Busy`:** the ticket gives one sentence, "Advert already sent, wait N s", for the 5 s refusal.
  For the still-queued refusal that sentence would be a lie (nothing has been transmitted yet), so that one shows
  **"Advert already queued"**. Nothing else about the wording was invented.
- **ARM, against a rule without the limit:** `test/host/test_advert.cpp` compiled against a copy of the rule whose
  `advertRequest()` always answers `Send` is **red** (`ARM_EXIT=1`), the exact FAIL lines quoted in Results below.
  The real tree is green.

### 2. The notice — one slot in the shared chrome (the plumbing only; the two advert notices are NOT done)
- `main/chrome.*` owns ONE notice slot for the whole app (`noticeSet(text, ttl_ms)` / `noticeText()`), and
  `drawHintBar()` draws it as a plate just above the hint bar — so every page that already calls the shared bar
  shows it, and a page cannot forget it. Newest wins: a second notice replaces the first and restarts its 3 s.
- The slot is published under a `portMUX_TYPE` critical section (the `time_source` pattern) with a copy taken out
  for the reader, so a writer on the radio task can never tear the string the main task is drawing.
- **Stated honestly — it OVERLAYS, it does not reflow:** the plate is drawn on top of the 22 px strip above the hint
  bar. Nothing but list space sits there on Launcher/Nodes/Map/RPTR, but the **Chat page's composer box** ends right
  at that line, so on an open channel the plate would cover the right end of the composer for its 3 s. No advert is
  sent from the Chat page today (its hint bar has no 'a'), so the case is unreachable until item 2's "new node"
  notice can arrive while typing there. Not measured on the glass — nothing was installed.
- **What is NOT done:** "Advert sent" / "Advert NOT sent" from `doAdvert()`, and "New node: <name> (<type>)" for an
  advert that ADDS a contact. Those are item 2's own decisions and they were not reached inside the cap (see
  Results). No half-built version of them is in the tree: `doAdvert()` writes no notice today.

### 3. Contacts across restarts
**NOT DONE.** `ContactStore` is still RAM only, so a restart still opens the Nodes page empty and a DM to a known
node still cannot be opened until it advertises again. No NVS namespace, no encoder, no restore — nothing partial.

### 4. Something the ticket expected that was not in the tree
- The ticket names `firmware/src/ui/new_node.h` and `node_menu.h`'s `advTypeName`: both exist under
  `Projects/pocket-mesh/firmware/` and were read (the type words are `client / repeater / room / sensor / node`).
  Nothing from them was needed for item 1.
- The ticket's item 1 text ("Advert already sent, wait N s") is one sentence for a 5 s refusal; the *queued*
  refusal the ticket also asks for has no wording of its own. That case says **"Advert already queued"** — see § 1.

### Results — where the run stopped
**Item 1 is done and verified. Items 2 and 3 were not started in substance** (item 2 has only the shared slot it
needs, which item 1 uses for its refusal). The 30-minute cap is the reason; nothing was left half-wired.
- `sh test/host/run.sh` → **exit 0**, **14 stages, 1510 checks, 0 failures**, no `AddressSanitizer` report.
  **Before this change: 1348 checks across 13 stages** (the new stage's 162 subtracted from the same run — the run
  itself is the measurement). **Stated honestly:** the EST-434 entry quoted "1296 across 13"; counted today with the
  same grep the pre-change total is 1348, so that figure under-counted by 52. The number above is measured, not
  copied. The `UBSan: left shift of negative value` lines from `core/ed25519/fe.c` are the vendored third-party C
  and are pre-existing (they appear on the untouched tree too); they do not fail the stage.
- `make build` → **exit 0**, `application.bin` **1,447,456 B (0x161620)**, **649,696 B (0x9e9e0, 31 %) of the
  2 MB app partition free**, **+1,280 B** from v40 (v40 was 1,446,176 B, 0x161120).
- **ARM, against a rule without the limit:** `test/host/test_advert.cpp` compiled against a copy of `advert_rule.cpp`
  whose `advertRequest()` never refuses (`/home/slofi/paperclip-pilot/sweep/est435-arm/`): **ARM_EXIT=1**,
  **15 checks, 6 failures**, log in `paperclip-pilot/sweep/est435-arm.log`. The FAIL lines, verbatim:
  ```
  FAIL test_advert.cpp:59  d.verdict == om::mc::AdvertVerdict::Wait
  FAIL test_advert.cpp:75  d.verdict == om::mc::AdvertVerdict::Wait
  FAIL test_advert.cpp:96  d.verdict == om::mc::AdvertVerdict::Busy
  FAIL test_advert.cpp:125  om::mc::advertRefusalText(d, text, sizeof(text))
  FAIL test_advert.cpp:145  d.verdict == om::mc::AdvertVerdict::Wait
  FAIL test_advert.cpp:158  om::mc::advertRequest(g, 102000).verdict == om::mc::AdvertVerdict::Wait
  test_advert: 15 checks, 6 FAILURES
  ```
  The real tree is **162 checks, 0 failures**.

### Files changed
- **new** `core/advert_rule.h`, `core/advert_rule.cpp` — the rule (`AdvertGate`, `advertRequest`,
  `advertQueued`, `advertResolved`, `advertRefusalText`). No clock, no NVS, no radio.
- **new** `test/host/test_advert.cpp` — 162 checks: the five-press flood, the 100 ms walk from +1.0 to +4.9 s and
  the +5.0 s send, the queued refusal, the failed send, the pinned sentences, the 32-bit wrap, and the schedule.
- `test/host/run.sh` — the new stage, wired like the others.
- `core/CMakeLists.txt` — `advert_rule.cpp` added to the component (one copy for device and host).
- `main/mc_link.{h,cpp}` — the gate, `monotonicMs()`, `queueAdvert()` now returns the rule's `AdvertDecision`, the
  3-hour schedule asks the same rule, and `doAdvert()` resolves the gate on EVERY exit (so a build failure cannot
  leave it "in flight" for ever) using the value `radio::send()` returned.
- `main/chrome.{h,cpp}` — the one notice slot and the plate `drawHintBar()` draws above the bar.
- `main/main.cpp` — one `pressAdvert()` lambda used by BOTH advert keys (the start menu and the Nodes page): a
  refusal becomes a 3 s notice, a fresh advert clears the last refusal. The sentence comes from the rule, so the
  wording the test pins is the wording on the glass.

### Next step
Item 2's two notices: `doAdvert()` sets "Advert sent" when `sent > 0` and "Advert NOT sent" otherwise; the advert
RX branch takes `contactsFind()` BEFORE `contactsUpsert()` to know "new", then sets
`New node: <name> (<type>)` with the L2's words (`client / repeater / room / sensor / node`) — both through
`noticeSet()`, which already exists and is already safe from the radio task. Then item 3's NVS blob.

## 2026-11-18 · S513 (EST-433) · direct messages, slice 1: receive, acknowledge, show, keep

> Written by DeepSeek for ticket EST-433 (fenced Paperclip run: no session start/end, nothing installed, nothing
> flashed, nothing committed). Code + host tests + build only. **Version for Claude's install: 40.** This entry is
> the ticket's FIRST artefact. The ticket carries a **30-minute cap**: the numbered items are done in order and
> this entry's Results section says exactly where the run stopped.

### EST-434 (continuation) — the receive path wired, the DM column on the glass

> Written by DeepSeek (S513, EST-434 ticket run, 2026-11-18). Fenced Paperclip run: no session start/end, nothing
> installed, flashed or committed. **All six numbered items are done.** Version stays **40**.

**0. The ACK routing correction (Claude's review).** `dmBuildAck()` no longer takes the arrived `View`. Its new
shape is `dmBuildAck(const uint8_t* out_path, uint8_t out_path_len, ack6, out, out_size)`:
- `out_path == nullptr` means the contact's route is `OUT_PATH_UNKNOWN`, and the frame is upstream's **flooded**
  `createAck()` (`BaseChatMesh.cpp:43-58`): header `(ACK << 2) | flood` = `0x0D`, `path_len` `0x40` (2-byte
  hashes, count 0), payload = the 6 ACK bytes and nothing else. A direct arrival today therefore gets
  `0d40f976f1cc005a`, not the old `0e41abcd…`.
- A later PATH slice only has to pass the stored route in; `out_path_len` carries the hash mode/count, so
  `out_path_len = 0x41` with `ab cd` produces the unchanged direct `0e41abcdf976f1cc005a`.
- **ARM, against the old builder:** the same `test_dm.cpp` compiled with the pre-review `dmBuildAck(View&)`
  (`paperclip-pilot/sweep/est434-arm/`) is **red**: `FAIL ...:208 got "0e41abcdf976f1cc005a" want
  "0d40f976f1cc005a"`, **45 checks, 1 failure**. The real tree is **45 checks, 0 failures**.

**2. The radio RX path (`main/mc_link.cpp`).** `onPacket` now handles `TXT_MSG` before the channel table:
- snapshots the contact table, calls `dmOpen` (every contact with that `src_hash` is tried; the MAC decides),
- drops a duplicate payload by the existing seen-ring, pushes the message into the RAM DM store,
- queues the reply through the send queue as the new `TxKind::Frame` request (a complete frame, copied by value);
  **the receive path never transmits** — `serviceRequests()` is still the only sender. A FLOOD arrival gets
  `dmBuildPathReturn`, a DIRECT arrival gets the flooded `dmBuildAck` above,
- the DM timestamp is recorded but **never passed to `noteText`/`noteAdvert`**: it is not a clock source.
- `main/radio_link.cpp`'s per-packet RX log now prints the three raw stats bytes next to the converted values
  (`raw rssi %u snr %d signal %u -> rssi %d dBm, snr %d dB, signal %d dBm`). **No conversion was changed** — the
  vendor's C6-side `lora.c:442` contradiction can be decided from the console first.

**3. Per-contact DM threads in RAM (`core/mc_store.*`).** `DmRing` (64 messages) holds `peer_pub`, the peer's
name, text, wire/local times, SNR, `direct` and `log_id`; a thread is a filtered view, not a second allocation.
Unread is counted from each contact's own `dm_seen` sequence (the same rule as a channel's `ReadState`), so reading
one thread cannot clear another. `dmThreadSummaries()` returns threads newest-first for the column. Host-tested
(name/preview/unread/order, ring eviction, two threads independent).

**4. SD history (`core/mc_history.*`, `main/sd_history.*`).** One log per contact at
`/sdcard/om-lite/history/dm-<pubkey[0..3] 8 hex>.log`, in the L2's own record format (`h.setDm(true)`, the peer in
the name field) and with the L2's Meta seen point. `restoreDmThreads()` scans the directory after the card is
mounted and before the radio task exists, restores every thread, and seeds unread from the Meta record. A restored
row knows only the 8-hex prefix the file name holds; the first real advert for that contact rebinds the row and its
messages to the full 32-byte key (`rebindRestoredDmPeer`). `noteDmRead()` appends the seen point only when it moved.
Host-tested record round trip (peer, text, log id, timestamps).

**5. The Chat page's DM column (`main/main.cpp`, `main/chat_page.*`).** The right column is filled from
`dmThreadSummaries`: newest first, contact name or 8-hex prefix, unread badge, newest preview and its **local**
HH:MM. Enter opens the card exactly like a channel (a DM read view with the same wrap/window/scrolling code);
Esc goes up and marks it read; the composer slot is a dim **"sending DMs comes next"** line, not a text field and
not a Send button. The page head now includes the thread count when the column is populated.

**6. Preview:** `shots/2026-11-18-chat-dm-column.png` (**new**, **800x480**), from
`python3 tools/preview_tanmatsu.py chat_dm` — the same card renderer as `2026-11-18-chat-two-columns.png`, with four
DM threads, the prefix-named restored one, unread badges and the gold focus ring on the second DM card.

### Results (real exit codes)

- `sh test/host/run.sh` → **HOST_SUITE_FINAL_EXIT=0**, **0 failures** everywhere. Totals **before 1296 across 13
  suites → after 1348 across 13**: store 99→**127**, history 65→**74**, DM 43→**45** (normal AND ASan), tx queue
  82→**93**; the other suites are unchanged (192 / 91 / 62 / 115 / 115 / 267 / 99 / 23). **+52 checks.**
  Logs: `paperclip-pilot/sweep/est434-host-before.log`, `.../est434-host-final3.log`.
- `make build` → **BUILD_EXIT=0**, **0** `warning:`/`error:` lines. `application.bin` = **0x161120 =
  1,446,176 B**, **650,976 B (31 %) of the 2 MB app partition free**, **+16,720 B** from the EST-433 image.
  Log: `.../est434-build-final2.log`; `make size` log: `.../est434-size-final2.log` (1,445,768 B total image).
- **PNG:** `shots/2026-11-18-chat-dm-column.png`, 800x480, 34,139 B, checked visually.
- **Version: 40**, code + host tests + build only; **nothing installed or flashed, no commit.**

### Files changed in EST-434

- `core/mc_dm.h`, `core/mc_dm.cpp` — `dmBuildAck` takes an optional stored `out_path`; nullptr = flooded
  `createAck()` (`0x0D`, `path_len 0x40`, payload = ack6). Direct-with-route form unchanged.
- `core/tx_queue.h`, `core/tx_queue.cpp` — `TxKind::Frame`, `pushFrame()`, a copied 256-byte frame in `TxRequest`.
- `core/mc_store.h`, `core/mc_store.cpp` — `DmMessage`/`DmRing`, push/filter/unread/summaries, `dm_seen`,
  `dm_seen_log`; `DmSummary.pub`.
- `core/mc_history.h`, `core/mc_history.cpp` — `historyEncodeDm`, `historyLoadDm` (the L2 format, `dm` flag set).
- `main/sd_history.h`, `main/sd_history.cpp` — `dm-<8 hex>.log` path, append/load/seen, `restoreDmThreads()`.
- `main/mc_link.h`, `main/mc_link.cpp` — TXT_MSG RX, DM store write + log id, queued ACK/path return, prefix rebind,
  `restoreDmHistory()`, `noteDmRead()`, `dmThreads()`.
- `main/radio_link.cpp` — the three raw stats bytes in the per-packet RX log (conversions untouched).
- `main/chat_page.h`, `main/chat_page.cpp` — `dm_open`, DM title/empty line, dim no-composer line, DM hints, head count.
- `main/main.cpp` — DM column fill, Enter on a DM card, Esc/refresh mark the thread read, shared wrap/window path.
- `test/host/test_dm.cpp`, `test_store.cpp`, `test_history.cpp`, `test_txqueue.cpp` — 52 new checks (see totals).
- `tools/preview_tanmatsu.py` — `chat_dm` renderer (the one shared preview tool, no per-ticket script).
- `shots/2026-11-18-chat-dm-column.png` (**new**).
- `docs/PROGRESS.md` — this subsection.



### What was read (this run)
- `BRIEF.md`, this file's S513 entries (EST-432, EST-430, EST-429, EST-427, EST-B) for the card style, the
  notice pattern, the clock rule and the install discipline.
- `core/mc_wire.*`, `core/mc_identity.*`, `core/mc_store.*`, `core/chat_cards.*`, `core/mc_history.*`,
  `core/tx_queue.*`, `main/mc_link.*`, `main/chat_page.*`, `main/sd_history.*`.
- Upstream, line by line, not from memory: `Mesh.cpp:133-205` (TXT_MSG receive / seen-table / first packet
  wins), `Identity.cpp:155` (`ed25519_key_exchange`), `helpers/BaseChatMesh.cpp:222-270` (plaintext layout and
  the 6-byte ACK), `Mesh.cpp:448-486` (`createPathReturn`), `Mesh.cpp:560-573` (`createAck`),
  `Mesh.cpp:693-712` (`sendDirect`), `firmware/src/history/history_log.h:13,478-486` (the `dm-<8 hex>` id).

### 1. The pure decoder and the ACK builders: `core/mc_dm.*`

`dmOpen(self, pkt, peers, count, out)` follows `Mesh.cpp:133-205`:
- payload = `dest_hash(1) src_hash(1) MAC(2) ciphertext`; it is opened ONLY when `dest_hash == self.pub[0]`;
- EVERY contact whose `pub[0] == src_hash` is tried, in table order, and the 2-byte MAC decides; the first that
  opens wins (upstream's "first packet wins");
- the secret is `ed25519_key_exchange(their_pub, our_prv)` (`Identity.cpp:155`), the already-vendored
  `core/ed25519/key_exchange.c`; MAC-then-decrypt goes through `mc_wire`'s own helpers (exposed, not copied);
- the plaintext is read exactly as `BaseChatMesh.cpp:222-270`: `ts(4, LE)`, `flags = data[4]`,
  `txt_type = flags >> 2`, `attempt = flags & 3`, text from `data[5]` to the first NUL.

Only `TXT_TYPE_PLAIN` produces an ACK; every other txt_type is reported with its type and no ack. The 6-byte
ACK is the ticket's rule: `sha256(data[0 .. 5+text_len) || SENDER pub)[0..4]`, then byte 4 = the extended
attempt byte (`data[5+text_len+1]`) and byte 5 = a caller-supplied random byte (so the host test is
deterministic). Every length is clamped/refused before a copy; the parser joins the ASan stage.

The two frames are byte-for-byte ports of upstream builders:
- `dmBuildPathReturn` — `header = (0x08 << 2) | ROUTE_TYPE_FLOOD`, `path_len = 0x40` (2-byte path hashes, count
  0), payload = `dest_hash(1) src_hash(1)` + `encryptThenMAC(secret, path_len || path || 0x03(Ack) || ack6)`.
  This is `createPathReturn` + `sendFlood` (`Mesh.cpp:448-486, 641-676`).
- `dmBuildAck` — `header = (0x03 << 2) | ROUTE_TYPE_DIRECT` (0x0E), the incoming frame's path copied back,
  payload = the 6 ACK bytes. This is `createAck` + `sendDirect` (`Mesh.cpp:560-573, 693-712`).

### Results (real exit codes)

**Where this run stopped: after numbered item 1, because of the ticket's 30-minute cap.** The decoder, the two ACK
builders, the oracle suite, the ARM and the build are finished and green. Items 2-6 (the `mc_link.cpp` RX wiring,
the per-contact store, the SD `dm-` logs, the Chat UI and the preview PNG) are **NOT started** — nothing of them is
half-written in the tree. Installing this build therefore shows **no visible change**: it is the receive path's
portable core only, waiting for item 2.

- **Independent oracle:** `test/host/test_dm.cpp` was written from a Python oracle (PyNaCl +
  `cryptography` + `hashlib`/`hmac`) and **its first version was wrong**, which is why the vectors were
  regenerated: orlp's keypair stores `SHA-512(seed)[:32]`, clamped, as the private scalar, so
  `X25519PrivateKey.from_private_bytes(seed)` does **not** reproduce `key_exchange.c`. The corrected oracle uses
  PyNaCl's `crypto_sign_ed25519_sk_to_curve25519` + `crypto_scalarmult`; both directions then agree with the C++
  (`f6f92e…` for self/A, checked side by side). This is stated because it is exactly the kind of oracle mistake
  that would have made a real incompatibility look green.
- **ARM FIRST — the suite is red against deliberately wrong rules.** The same `test_dm.cpp` was compiled against
  a sweep-only copy (`paperclip-pilot/sweep/est433/mc_dm_wrong.cpp`, outside the tree and not built by `run.sh`)
  with the destination-hash check removed and only the FIRST contact sharing a `src_hash` tried.
  **ARM_EXIT=1**, **43 checks, 4 failures**; the FAIL lines, verbatim
  (`paperclip-pilot/sweep/est433/arm.log`):

      FAIL test/host/test_dm.cpp:155  !openFrame(kFrameNotOurs, g_a, 1, &m)
      FAIL test/host/test_dm.cpp:164  openFrame(kFrameA, &both[0][0], 2, &m)
      FAIL test/host/test_dm.cpp:165  m.peer == 1
      FAIL test/host/test_dm.cpp:166  got "" want "hello from A"

  The same file against the real `core/mc_dm.cpp`: **43 checks, 0 failures**.
- `sh test/host/run.sh` → **HOST_SUITE_FINAL_EXIT=0**. Suites: 192 / 91 / 99 / 65 / 62 / 115 (MeshCore) /
  **43 (DM, new)** / 115 (MeshCore under ASan+UBSan) / **43 (DM under ASan+UBSan, new)** / 267 (map) / 82 (tx
  queue) / 99 (chat cards) / 23 (menu cards) — **0 failures** everywhere, **0** `AddressSanitizer` lines. The
  ASan stage includes the ticket's **186-byte junk TXT_MSG**, which is refused before any copy. Totals: **1210
  checks across 11 suites before, 1296 across 13 after**. **Stated honestly:** the EST-432 entry quoted the map
  suite as 260; the clean tree at 37d3932 actually runs **267** here, so the "before" total is the measured 1210,
  not the historical 1203. Nothing in this ticket touches `test_map.cpp`.
- `make build` → **BUILD_EXIT=0**, **0** `warning:` lines. `application.bin` = **0x15CFD0 = 1,429,456 B**,
  **0xA3030 = 667,696 B (32 %) of the 2 MB app partition free**, **+464 B** from v39 (0x15CE00 = 1,428,992 B).
  Logs: `paperclip-pilot/sweep/est433/host.log`, `.../build.log`.
- **Preview PNG: NOT produced** — item 6 was not reached. The ticket's path
  `shots/2026-11-18-chat-dm-column.png` does **not** exist, and no file was added under `shots/`.
- **Version: 40** as the ticket says, with the caveat above: it is core code only, no UI change.

### Files changed

- `core/mc_dm.h`, `core/mc_dm.cpp` (**new**) — `dmOpen` (dest/src hashes, every matching contact tried, MAC
  decides, plaintext layout, plain-only ACK), `dmBuildAck` (direct), `dmBuildPathReturn` (flood). Every length
  from the air is checked before use; no allocation.
- `core/mc_wire.h`, `core/mc_wire.cpp` — `sealPayload` / `openPayload` expose the existing MAC-then-decrypt
  helpers so `mc_dm` reuses them instead of keeping a second copy.
- `core/mc_identity.h`, `core/mc_identity.cpp` — `identitySharedSecret`: `ed25519_key_exchange(their_pub,
  our_prv)`, the one-line port of `Identity.cpp:155`.
- `core/CMakeLists.txt` — `mc_dm.cpp` added to the component.
- `test/host/test_dm.cpp` (**new**) — 43 checks: A opens / B does not, wrong MAC, dest_hash not ours, two
  contacts sharing `src_hash` (both orders), CLI_DATA not acked, ACK bytes and both answer frames against the
  oracle, and the bounds cases (186-byte junk, 19-byte payload, non-block ciphertext, null output).
- `test/host/run.sh` — the DM suite and a second DM stage under ASan+UBSan.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked (honest list)

- **Items 2-6 are not done:** `main/mc_link.cpp` does not handle TXT_MSG yet, there is no per-contact DM store,
  no `dm-<8 hex>` SD log, no DM card in the Chat column, no dim "sending DMs comes next" line, and no PNG. A DM
  arriving at the device today is still logged as "not a channel text" and dropped.
- **Nothing installed, flashed or committed** (ticket terms). No glass test, and the ticket's Claude-on-glass
  check ("L2 sends a DM to TNMT (SI)") **cannot pass yet** — the RX wiring is item 2.
- The ACK builders are oracle-checked as bytes, but **not yet sent**: the tx-queue request kind for an ACK and
  the radio-task path are part of item 2.
- The direct ACK's path is copied from the arrival's path (the only route this slice has); upstream uses the
  contact's stored `out_path` (`BaseChatMesh.cpp:41-57`). That needs the per-contact store (item 3) to be exact
  on a multi-hop direct link; it is identical for a zero-hop path.

---

## 2026-11-18 · S513 (EST-432) · the menu redone: three app cards

> Written by DeepSeek for ticket EST-432 (fenced Paperclip run: no session start/end, nothing installed, nothing
> flashed, nothing committed). Code + host tests + build + one preview PNG only. **Version for Claude's install:
> 39.** This entry is the ticket's FIRST artefact: it goes in before the preview and the code, and its Results
> section is filled from the real runs below. (EST-431, which removed the menu, was cancelled before it touched
> anything; the tree was clean at 52baaa0 and this starts from there.)

### What was read (this run)
- `BRIEF.md` in full, `docs/PLAN.md` § 1, § 2, § 4, § 5, and this file's S513 entries (EST-430, EST-429,
  EST-427) for the card style, the notice pattern and the install discipline.
- `main/launcher_page.h/.cpp` (the list + detail pane being replaced), the Launcher part of `main/main.cpp`
  (the rows, the two-area key branch, the draw switch), `main/chat_page.h/.cpp` (the new rounded card style),
  `main/chrome.h/.cpp` (head, hint bar, status bar), `main/nodes_page.cpp` (the Nodes empty-list text and hints),
  `main/tm_theme.h`, `main/screen.h`, `core/registry.*`, `core/router.*`, `core/module.h`, `core/pages.h`,
  `test/host/run.sh`, `test/host/test_core.cpp`, `tools/preview_tanmatsu.py`, and the two shots
  `shots/2026-11-13-launcher-home.png` (the old page) and `shots/2026-11-18-chat-two-columns.png` (the card style).

### 1. The menu: three large cards, no list, no detail pane

The page head now says **Tanmatsu** (the head's right slot keeps the core line, e.g. `core ready · radio free`,
and hands that slot to a 3-second notice when one is live). Below it, three equal-width cards side by side in
Filip's names and order: **OM Lite**, **OPS-TOC**, **Mesh-Torry**.

| number | value | why |
|---|---|---|
| work box | x 18 … 782 | `kTmPad` margins |
| card gap | **16 px** | the Chat cards' gutter (S513/EST-430) |
| card width | **244 px** each | `(764 - 2*16)/3`; the odd pixel stays in the gap |
| card height | **232 px** | one large plate, name + description + chip + two numbers |
| corner radius | **6 px** | the Chat cards' radius, and the same 2 px gold focus outline |
| card top | head line + **22 px** | the cards start under the page head |
| focused line | card bottom + **28 px** | one short dim line about the focused card |

A card shows the name large, one dim description (OM Lite "mesh companion", OPS-TOC "field ops map &
checklists", Mesh-Torry "the territory game"), a state chip in the existing chip style (`RUNNING` green /
`STOPPED` plain / `LATER` dim), then for the running card two small lines with its memory and uptime (the
numbers the old detail pane had). A LATER card says `coming later` in the dim colour; a STOPPED card says
`stopped · ↵ starts it`. Under the row sits **one** short dim line about the focused card (OM Lite:
`radio 868 · MeshCore session · SD history`). The old explanatory paragraph is gone: say less.

### 2. Keys: picking is the main action (this REPLACES Filip's S499 two-area rule)

**Deliberate rule change, so Claude can tell Filip:** the S499 rule was "Enter moves the focus to the actions
on the right; Esc brings it back". With three app cards there is no second area to move to, and picking IS the
main action, so that two-area rule is retired here. The new keys:

- **Left/Right** (Up/Down are aliases) move between the three cards and **stop at both ends** — no wrapping.
- **Enter** opens the focused app: a stopped one is started first (`core.start`) and then the router goes to its
  home page (OM Lite: **Nodes**); a LATER card changes nothing visible except a **3-second notice** in the
  head's right slot: `OPS-TOC: coming later` (the EST-428 notice pattern, `notice` only in the view while live).
- **s** stops the focused running app (`core.stop`; the leak line is logged exactly as today). No confirm — it
  is reversible.
- **Esc** on the menu leaves the app through `leaveToLauncher()` exactly as today (the S501 exit code is not
  touched).
- **a** still forces an advert early here, and now also on the **Nodes** page, whose hint bar carries
  `a advert` (its empty-list text already points at it).
- Hint bar: `←→ select · ↵ open · s stop · esc quit`; the triangle key still says LAUNCHER.

The app still opens on this menu at launch, with the focus on OM Lite.

### 3. Registry: nothing new runs

The registry gets the two LATER modules as real `available = false` entries (`ops-toc`, `mesh-torry`) beside
`om-lite`, so the state the cards draw comes from the registry and not from a string in the shell. Their pages
mask is 0 and every hook is null: they are listed, never startable, and neither app is built.

### 4. The rules are pure, in `core/menu_cards.*`

No UI types, no theme: `menuFocusMove(focused, move, count)` (clamped at both ends), `menuEnterAction(available)`
(`Open` vs `ComingLater`) and `menuStopAction(available, running)` (`Stop` vs `None`). Host suite
`test/host/test_menu_cards.cpp` wired into `test/host/run.sh`; the ARM section below is the evidence that it is
red against a deliberately wrong rule.

### 5. Preview first

`tools/preview_tanmatsu.py` gains the `menu` renderer (same palette parsed from `theme.h`, same metrics table):
800x480, OM Lite RUNNING and focused, OPS-TOC and Mesh-Torry LATER, the focused line under the cards, the new
hint bar → `shots/2026-11-18-menu-cards.png`. One shared tool, no per-ticket script.

### Results (real exit codes)

- **ARM FIRST — the suite is red against a deliberately wrong rule.** The same `test/host/test_menu_cards.cpp`
  was compiled against a sweep-only copy of the rules (`paperclip-pilot/sweep/est432-arm/menu_cards_wrong.cpp`,
  outside the tree and not built by `run.sh`) with the focus WRAPPING at both ends and Enter saying `Open` even
  on a LATER card. **ARM_EXIT=1**, **3 checks, 2 failures**; the FAIL lines, verbatim
  (`paperclip-pilot/sweep/est432-arm.log`):

      FAIL test_menu_cards.cpp:47  tm::menuFocusMove(0, tm::MenuMove::Left) == 0 (got 2, want 0)
      FAIL test_menu_cards.cpp:76  tm::menuEnterAction(false) == tm::MenuAction::ComingLater

  The same file against the real `core/menu_cards.cpp`: **23 checks, 0 failures**. The wrap at the first card
  and the LATER card's `Open` are both caught by a ticket case (the no-wrap rule, Enter's difference).
- `sh test/host/run.sh` → **HOST_SUITE_FINAL_EXIT=0**. Suites: 192 / 91 (clock) / 99 / 65 / 62 / 115 (MeshCore)
  / 115 (the same under ASan+UBSan) / 260 (map) / 82 (tx queue) / 99 (chat cards) / **23 (menu cards, new)** —
  **0 failures** everywhere, **0** `AddressSanitizer` lines. Totals: 1180 checks across 10 suites before,
  **1203 across 11** after. Logs: `paperclip-pilot/sweep/est432-host.log`, final re-run on the finished tree
  `paperclip-pilot/sweep/est432-host-final.log`.
- `make build` → **BUILD_EXIT=0**, **0** `warning:` lines. `application.bin` = **0x15CE00 = 1,428,992 B**,
  **0xA3200 = 668,160 B (32 %) of the 2 MB app partition free**, **−144 B** from v38 (EST-430, 0x15CE90 =
  1,429,136 B). **Stated honestly:** the first build of this run exited 0 but printed one warning
  (`main/launcher_page.cpp:57:25: warning: unused variable 'H'`); the variable was removed and the run above is
  the clean one. Log: `paperclip-pilot/sweep/est432-build.log`.
- **Preview:** `shots/2026-11-18-menu-cards.png`, **800x480**, from
  `python3 tools/preview_tanmatsu.py menu` — OM Lite RUNNING and focused, OPS-TOC and Mesh-Torry LATER, the
  focused card's own line under the row, the hint bar with `←→ select · ↵ open · s stop · esc quit`.
- **One documented difference between the preview and the panel, stated because the ticket quoted the glyphs:**
  the device's hint chips say **`l/r`** and **`ret`**, not `←→` / `↵`. The generated faces are the L2 subset
  (ASCII + Latin-1/Extended-A, `tools/mkfonts.py`); U+2190/U+2192/U+21B5 are not in it and would draw as blank
  advances. Every other device page's hint bar already uses `l/r` / `ret` for this reason, so the menu follows
  that convention; the words (select / open / stop / quit) are exactly the ticket's.
- **Version for Claude's install: 39.**

### Files changed

- `core/menu_cards.h`, `core/menu_cards.cpp` (**new**) — the menu's pure decisions: `menuFocusMove` (clamped at
  both ends, Up/Down aliases), `menuEnterAction` (`Open` vs `ComingLater`), `menuStopAction` (`Stop` vs `None`).
  No UI types, no theme.
- `core/CMakeLists.txt` — `menu_cards.cpp` added to the component.
- `test/host/test_menu_cards.cpp` (**new**) — 23 checks on the ticket's three rules.
- `test/host/run.sh` — the new suite wired in (one stage, one temp binary, cleaned up in the same trap).
- `main/launcher_page.h` — `MenuCard` (name, sub, running, available, memory, uptime, owns), the new `LauncherView`
  (cards/focused/right_head/notice, no rows/detail/two-area fields), and the menu metrics (`kTmMenuGap` 16,
  `kTmMenuCardH` 232, `kTmMenuTop` 22, `kTmMenuRadius` 6, `kTmMenuOwnsGap` 28).
- `main/launcher_page.cpp` — the three equal cards, the Chat cards' plate/ring recipe, state chip derived from
  the two booleans, memory + uptime for the running card, "coming later" for LATER, the focused card's own line,
  the new hint bar.
- `main/chrome.h`, `main/chrome.cpp` — `drawRoundedRect` / `drawCardPlate` moved here from the Chat page, so the
  Chat cards and the menu cards share one rounded-card implementation.
- `main/chat_page.cpp` — uses the shared helpers; its local rounded-fill copy is gone. Drawing unchanged.
- `main/main.cpp` — the two LATER modules added to the registry as `available = false` rows; the cards, focus,
  memory/uptime; the new key branch (arrows clamped, Enter start-then-route, `s` stop, Esc untouched, `a` still
  advert); the 3-second notice; `a` on the Nodes page.
- `main/nodes_page.cpp` — hint bar gains `a advert` (the empty-list text already pointed at it).
- `tools/preview_tanmatsu.py` — the `menu` renderer plus its metrics in the ONE table.
- `shots/2026-11-18-menu-cards.png` (**new**) — the preview above.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked (honest list)

- **Nothing installed, flashed or committed** (ticket terms): the build's exit code and binary size are real,
  the menu has **host-render evidence only** — no glass shot.
- The **two LATER cards are strings and a registry row**, not apps: nothing of OPS-TOC or Mesh-Torry runs.
- Enter's start-then-route path is host-tested as a decision; the panel's own frame timing was not measured.

---

## 2026-11-18 · S513 (EST-430) · the Chat list is two columns of cards: channels left, direct messages right (v38)

> Written by DeepSeek for ticket EST-430 (fenced Paperclip run: no session start/end, nothing installed, nothing
> flashed, nothing committed). Code + host tests + build + one preview PNG only. **Version for Claude's install:
> 38.** This is the ticket's FIRST artefact: the entry goes in before the code, and its Results section is filled
> from the real runs below.

### What was read (this run)
- `BRIEF.md` in full, `docs/PLAN.md` § 4 (the look) and § 5 (keys), and this file's S513 entries (EST-429,
  EST-427, EST-428) plus Claude's S513 review note.
- `main/chat_page.h/.cpp` (the list state only), `main/main.cpp` (`drawChatPage`, the Chat key branch), `main/chrome.*`,
  `main/screen.h`, `main/tm_theme.h`, `main/time_source.h` (`hhmmOf`), `core/mc_store.h`
  (`channelSummary`/`unreadForChannel`), `test/host/run.sh`, `test/host/test_layout.cpp` (the harness style),
  `tools/preview_tanmatsu.py` (the `chat` renderer and the `plate`/`focus_band` recipes), and the L2 wording
  `firmware/src/ui/chat_selector.cpp:119` ("No direct messages yet").

### 1. Layout: two columns of cards (list state only; the open-channel view is untouched)

The Chat page in its LIST state no longer draws one 64 px full-width row per channel. Between the page head and
the hint bar sit **two equal columns of rounded cards**, one card per chat:

| number | value | why |
|---|---|---|
| work box | x 18 … 782, y 70 … 456 | `kTmPad` margins, head line (`kTmStatusH + kTmHeadH`), hint bar (`kTmHintH`) |
| gutter | **16 px** | between the two columns; the odd leftover pixel stays in the gutter, so both columns are exactly 374 px |
| column width | **374 px** each | `(782-18-16)/2`, identical for both (ticket) |
| column header band | **30 px** | label line, then the cards start |
| card height | **68 px** | the ticket's 64-72: two comfortable lines (17 px name, 12 px preview) |
| card gap | **10 px** | vertical space between cards in one column |
| cards that fit | **4 per column** | `(386-30+10)/(68+10)` = 4 (a 5th would end at y=480, under the hint bar) |
| corner radius | **6 px** | rounded card; the accent focus outline follows the same radius |

A card shows: the **name** left (clipped), the **unread count** as the app's existing badge (filled `kBad` disc,
white count, right, drawn only when > 0), and a **second line**: the newest message as `"sender: text"` clipped, in
`kDim`, with its **local `HH:MM`** right-aligned (`time_source::hhmmOf`). No newest message = `"no messages yet"` in
`kDim`. Unfocused card: `kPanel` fill, 1 px `kLine` outline. Focused card: `kSelBg` fill + the app's 2 px `kAcc`
focus ring, both rounded.

The head keeps its existing text (`"N channels · N unread"`) — the ticket asks for nothing new there.

### 2. The DM column carries a MODEL, not messages

No `TXT_MSG` receive or send exists in this app, so **no direct message is invented**: the right column draws
exactly the L2's empty line **"No direct messages yet"** (`firmware/src/ui/chat_selector.cpp`) in `kDim` and no card.
The view model nevertheless carries `dms[kChatRows]` + `dm_count` (empty today) beside `cards[]`/`card_count`, and
the page draws whatever the model holds — so a future DM slice fills the array and never touches the layout.

### 3. Focus and keys (Filip's rules kept: Esc is UP and never undo; Enter opens)

- **Up/Down** move inside the column and stop at its ends (no wrap, same as the rest of the app).
- **Left/Right** switch column and land on the card at the **nearest row**, clamped to that column's last card.
- A column with **no cards cannot take focus** → while DMs are empty, Right does nothing.
- **Enter** on a channel card opens that channel exactly as today (`chat_open`, newest message, marks read).
- A column with more cards than fit **scrolls by itself** to keep the focused card fully visible (each column has
  its own scroll), and the head shows nothing new for it.

### 4. The rules are pure, in `core/` (ticket item 4)

`core/chat_cards.h/.cpp` — no UI types, no theme, numbers passed in:
`cardGrid()` (box → two equal columns, gutter, header band, card height, visible count), `cardRect()` (col, row,
scroll → screen rect), `cardMoveFocus()` (Up/Down/Left/Right with clamping and the empty-column refusal),
`cardScroll()` (scroll the minimum needed to keep the focused row fully visible, clamped). Host suite
`test/host/test_chat_cards.cpp`, wired into `test/host/run.sh`; the ARM section below is the evidence that the
suite is red against a deliberately wrong rule.

### 5. Preview

`tools/preview_tanmatsu.py` gains the `chat2` renderer (same palette parsed from `theme.h`, same metrics from
`tm_theme.h`): 800x480, three channels, one with 2 unread, the DM column empty, focus on the second channel →
`shots/2026-11-18-chat-two-columns.png`. One shared tool, no per-ticket script.

### Results (real exit codes)

- **ARM FIRST — the suite is red against a deliberately wrong rule.** The same `test/host/test_chat_cards.cpp` was
  compiled against a sweep-only copy of the rules (`paperclip-pilot/sweep/est430-arm/chat_cards_wrong.cpp`, outside
  the tree and not built by `run.sh`) with two deliberate mistakes: Left/Right jumps into ANY column (empty or not)
  and keeps the row unclamped, and the scroll only ever moves back towards the top. **44 checks, 3 failures**,
  `ARM_EXIT=1`; the FAIL lines, verbatim (`paperclip-pilot/sweep/est430-arm.log`):

      FAIL test/host/test_chat_cards.cpp:154  g.row == 2 (got 4, want 2)
      FAIL test/host/test_chat_cards.cpp:168  f.col == om::mc::kColChannels (got 1, want 0)
      FAIL test/host/test_chat_cards.cpp:192  om::mc::cardScroll(g, 10, 4, 0) == 1 (got 0, want 1)

  The same file against the real `core/chat_cards.cpp`: **99 checks, 0 failures**. Both wrong rules are caught by a
  ticket case (the clamped landing row, the empty DM column refusing focus, the scroll keeping the 5th card visible).
- `sh test/host/run.sh` → **HOST_SUITE_FINAL_EXIT=0**. Suites: 192 / 91 (clock) / 99 / 65 / 62 / 115 (MeshCore) /
  115 (the same under ASan+UBSan) / 260 (map) / 82 (tx queue) / **99 (chat cards, new)** — **0 failures** everywhere,
  **0** `AddressSanitizer` lines. Logs: `paperclip-pilot/sweep/est430-host.log`, final re-run on the finished tree
  `paperclip-pilot/sweep/est430-host-final.log`.
- `make build` → **BUILD_EXIT=0**, **0** `warning:` lines. `application.bin` = **0x15CE90 = 1,429,136 B**,
  **0xA3170 = 668,528 B (32 %) of the 2 MB app partition free**, **+1,552 B** over v37 (EST-429, 0x15C880).
  Log: `paperclip-pilot/sweep/est430-build.log`. **Stated honestly:** the first link of this run FAILED
  (`undefined reference to om::mc::cardScroll/cardMoveFocus/cardGrid/cardRect`, exit 2) because the new
  `core/chat_cards.cpp` was not yet in `core/CMakeLists.txt`; the line was added and the run above is the green one.
- **Preview:** `shots/2026-11-18-chat-two-columns.png`, **800x480**, from
  `python3 tools/preview_tanmatsu.py chat2` — three channels, `test-a` with 2 unread, the DM column empty with the
  L2's line, focus on the second channel, the hint bar showing `l/r column`.
- **Version for Claude's install: 38.**

### Files changed

- `core/chat_cards.h`, `core/chat_cards.cpp` (**new**) — `cardGrid()` (two equal columns, gutter, header band, card
  height, visible count), `cardRect()` (col/row/scroll → rect), `cardMoveFocus()` (Up/Down/Left/Right, clamping, the
  empty-column refusal, the fallback when a column empties), `cardScroll()` (minimum move keeping the focused card
  fully visible, clamped). No theme, no UI types.
- `core/CMakeLists.txt` — `chat_cards.cpp` added to the component (the missing line that failed the first link).
- `test/host/test_chat_cards.cpp` (**new**) — 7 test functions / 99 checks on the panel's own numbers.
- `test/host/run.sh` — the new suite wired in (one stage, one temp binary, cleaned up in the same trap).
- `main/chat_page.h` — the card metrics (`kTmCardGutter` 16, `kTmCardHeaderH` 30, `kTmCardH` 68, `kTmCardGap` 10,
  `kTmCardRadius` 6, `kTmCardWorkTop`), `chatCardGrid()` (the one grid main.cpp and the drawer share), and the list
  model: `dms[]` + `dm_count` (empty today) beside `rows`/`row_count`, `focus_col`/`focus_row`, `scroll[2]`.
  `focused` is gone.
- `main/chat_page.cpp` — the list state now draws two columns of rounded cards (`fillRound`/`drawCard` per-row span
  fills, no float, no allocation), name + unread badge + `"sender: text"` + `HH:MM`, the L2's
  "No direct messages yet" empty line, the 2 px `kAcc` focus outline on the focused card only; the hint bar gained
  `l/r column`; the unused `kListRowH` is gone. The **open-channel view is untouched**.
- `main/main.cpp` — the card fill (time from `time_source::hhmmOf` instead of an age), the DM count (0), the
  per-column scroll via `cardScroll`, and the list keys routed through `cardMoveFocus` (Up/Down/Left/Right, Enter
  opens a channel only from the Channels column as before).
- `tools/preview_tanmatsu.py` — `chat2` renderer + the card metrics in the tool's ONE metrics table.
- `shots/2026-11-18-chat-two-columns.png` (**new**) — the preview above.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked (honest list)

- **Nothing installed, flashed or committed** (ticket terms): the build's exit code and binary size are real, the
  two-column look has **host-render evidence only** — no glass shot.
- **No DM exists to draw**: the DM column is an empty state plus a model slot; the DM card path is exercised by the
  geometry tests (a non-zero `counts[1]`) but never seen on screen.
- Scrolling in a column is host-tested by rect arithmetic; it has **not** been seen on the glass (needs more
  channels than four).

---

## 2026-11-18 · S513 (EST-429) · a mesh time now needs TWO agreeing nodes, so one wrong clock cannot move ours for good

> Written by DeepSeek for ticket EST-429 (fenced Paperclip run: no session start/end, nothing installed, nothing
> flashed, nothing committed). Code + host tests + build only. **Version for Claude's install: 37** (the same
> install as EST-428; no version bump). **Status: done — host suite, the old-rule arm and the device build are
> green; evidence and real exit codes below.**

### What was read (this run)
- `BRIEF.md` in full, and this file from the top (the S513/EST-427 entry and the S502 clock entry it builds on).
- `core/clock.h/.cpp`, `main/time_source.h/.cpp`, the clock parts of `main/mc_link.cpp` (`nextAdvertTimestamp`,
  the advert branch, the group-text branch, `doText`), the `begin()`/`service()` call sites in `main/main.cpp`,
  `test/host/test_clock.cpp`, `test/host/run.sh`, and `managed_components/badgeteam__badge-bsp/targets/
  tanmatsu/badge_bsp_rtc.c` + `bsp/rtc.h` for the recovery section.

### The defect (Claude's S513 reading; how often real nodes carry a future clock is NOT measured)

`noteSeen()` ran for every verified advert (`mc_link.cpp` ~291) and every channel text whose MAC opened (~328).
`shouldAdopt()` accepted any plausible time (up to 2100) more than 60 s ahead of ours, regardless of where our
clock came from. `service()` then set the system time, wrote it to the CH32 RTC and stored it as the NVS floor.
Nothing ever moves the clock back: `shouldAdopt` only goes forward, `chooseStart` takes the later of RTC and
floor, and the advert floor (`mcid/advts`) is monotonic. So **one** node with its clock in, say, 2030 — or anyone
holding a public channel key, since a valid MAC proves the key, not the time — moved us to 2030 permanently.
Every message and advert we send is then future-dated, and peers drop our adverts as replays until 2030. A
verified signature proves WHO signed; never that their clock is right.

### The rule now implemented (Claude's recommendation; reversible — Filip may change it)

- **(a) RTC boot:** when the boot source is the coprocessor RTC (`Source::Rtc`), mesh times are **never**
  adopted automatically. A peer more than **10 min** off is logged **once per sender** (advert key prefix) and
  nothing else happens.
- **(b) Stored / None boot:** a mesh time is adopted only when **at least 2 DIFFERENT advert public keys** report
  times that agree within **120 s**, after projecting each sample by the monotonic seconds since it was heard.
  **Channel texts do not count** (self-declared sender, channel keys can be public); adverts only.
- **(c) The floor still wins:** an agreed time is never adopted below the stored floor (`mcid/epoch`) and still
  has to beat our clock by the existing 60 s slack (never backwards). Nothing mesh-learned is written to the
  CH32 RTC or to NVS (`mcid/epoch`) unless rule (b) was met.
- **(d) Split unchanged:** the rules are pure in `core/clock.*` (a fixed 8-slot agreement table, one sample per
  key prefix, plus a 16-key "already warned" set); `main/time_source.cpp` is the glue. The **radio task only
  records samples** (`noteAdvert` / `noteText` under the existing spinlock); the **main task decides**
  (`service()` snapshots the table and calls one pure `decideAdoption`).

### Recovery of a device that is ALREADY poisoned (described, NOT run in this ticket)

Two places hold the poison and **both** must be dealt with, because `chooseStart` takes the later of the two:

1. **NVS, namespace `mcid`:** `epoch` (the stored boot floor) and `advts` (the advert timestamp floor). While
   `advts` is in the future, `advertFloor()` keeps stamping our adverts at `floor + 1`, so peers see them as
   replays even after the clock itself is fixed. Clear both.
2. **The CH32 coprocessor RTC:** battery-kept, and the app's only down-write is "stored floor later than RTC"
   (boot) — which cannot happen while the RTC is the later one. So a poisoned RTC needs one deliberate
   `bsp_rtc_set_time(<correct epoch>)` call (a one-off recovery build; `bsp/rtc.h` on the device), or a CH32
   whose backup is allowed to drain (then it reads 0 and the floor/agreement takes over).

The BadgeLink commands (device in **BadgeLink** mode: launcher home → F6, USB `16d0:0f9a`; **not run here**):

    badgelink.sh nvs list mcid
    # clear both floors; the forms exercised in this tree are u8 / str / blob:
    printf '\0\0\0\0' > /tmp/zero4.bin
    badgelink.sh nvs write --file mcid epoch blob /tmp/zero4.bin
    badgelink.sh nvs write --file mcid advts blob /tmp/zero4.bin
    badgelink.sh nvs list mcid          # confirm they are gone/zero

A 4-byte zero **blob** under a key that was `u32` makes `nvs_get_u32()` fail with a type error, and both readers
(`loadStoredEpoch`, `nextAdvertTimestamp`) treat that exactly like "absent" → 0. A `u32` writer
(`nvs write mcid epoch u32 <correct-now>`) may exist in newer BadgeLink tools but is **not exercised in this
tree**, so do not rely on that spelling without checking `nvs list` afterwards. After the clear, boot is
`Source::None`: the fixed app stays at 1970 (safe, visible) until two advert keys agree on a time. Then install
the fixed build (v37+) so the device cannot be pushed forward again.

### Results (real exit codes)

- `sh test/host/run.sh` → **HOST_SUITE_EXIT=0**. Suites: 192 / **91** (clock, was 53) / 99 / 65 / 62 / 115
  (MeshCore) / 115 (same suite under ASan+UBSan) / 260 (map) / 82 (tx queue) — **0 failures** everywhere.
  **0** `AddressSanitizer` lines; the **83** `runtime error:` lines are the same vendored-Ed25519 UBSan noise as
  EST-427 (third-party, not ours). Log: `paperclip-pilot/sweep/est429-host.log`.
- **Armed against the old rule.** The S502 decision ("any plausible timestamp more than 60 s ahead wins") was
  restated behind the same API in `paperclip-pilot/sweep/est429-arm/clock_old.cpp` (outside the tree, not built
  by `run.sh`), and the new tests were made to continue past a failure in a sweep-only copy. Old rule:
  **91 checks, 12 failures**, covering **every** ticket case — RTC boot (lines 164, 165, 172), no source
  (184–186), same node twice (228), two nodes 10 min apart (238), channel text alone (249, 250, 255), and the
  stored floor (265). Case 3 (two agreeing nodes) passes on the old rule too, as expected: the old code adopted
  it for the wrong reason. The **same instrumented file** against the new `core/clock.cpp`: **91 checks,
  0 failures**. Log: `paperclip-pilot/sweep/est429-arm.log`.
- `make build` → **BUILD_EXIT=0**, **0** `warning:` lines. `application.bin` = **0x15C880 = 1,427,584 B**,
  **669,568 B (32 %) of the 2 MB app partition free**, **+1,296 B** over the EST-427 build (0x15C370).
  Log: `paperclip-pilot/sweep/est429-build.log`.
- **Version for Claude's install: 37** — the same AppFS install as EST-428; no version bump on my side.

### Files changed
- `core/clock.h` — the agreement section: `kAgreementSlots`/`kAgreementWindow` (120 s)/`kRtcWarnGap` (10 min),
  `Origin`, `Sample`, `Agreement`, `Warned`, and the declarations (`agreementRecord`, `projectSample`,
  `agreementTime`, `decideAdoption`, `farFromRtc`, `warnOnce`). `shouldAdopt`'s contract comment now says it is
  the last gate, not a time source.
- `core/clock.cpp` — the pure implementation: one sample per advert key prefix (a repeat replaces its own
  slot), texts dropped, projection by monotonic age, pairwise distinct-key agreement within 120 s yielding the
  LATER projection, `decideAdoption` = (a) no RTC adoption + (b) two witnesses + (c) floor/forward gates.
- `main/time_source.h` — `noteSeen` → `noteAdvert(ts, pub, len)` / `noteText(ts)`, with the one-owner contract
  restated.
- `main/time_source.cpp` — the glue: `g_agree` (radio writes under `g_mux`, main snapshots), `g_warned`,
  `g_boot_kind`; `begin()` records the boot source; `service()` logs a >10 min peer once per sender on an RTC
  boot, else adopts only via `decideAdoption` (the only place that writes clock/RTC/NVS).
- `main/mc_link.cpp` — the advert passes its verified public key to `noteAdvert`; the channel text calls
  `noteText`; the three comments say what a signature/MAC does and does not prove.
- `test/host/test_clock.cpp` — `test_mesh_agreement()`: the six ticket cases plus the 120 s boundary, the
  projection case, the floor/never-backwards case, the one-slot-per-key case and the "no time, no key" cases
  (53 → 91 checks). `run.sh` needed no change: the clock suite already compiles `core/clock.cpp`.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked (honest list)
- **Nothing installed, flashed or committed** (ticket terms). The device build's size/exit code are real; the
  adoption path has **no glass evidence** — it needs two real adverts agreeing within 120 s while our RTC is
  empty or behind.
- **How often real nodes carry a future clock is not measured** (the ticket says so); the fix is a rule change,
  not a response to an observed attack.
- The **recovery commands were not run** (BadgeLink not attached to this machine); the `u32` spelling of the
  NVS writer is unverified here, which is why the blob route is documented.
- An RTC-boot device that is ALREADY poisoned stays wrong until the RTC is written by hand (one-off build):
  rule (a) deliberately refuses mesh corrections, which is the trade the ticket asked for.
- The L2 suite was not re-run: no shared L2 file was touched (`core/clock.*` is this tree's own).

---

## 2026-11-18 · S513 (EST-427) · advert parser review fixes: a bounded copy, flags-aware name, and one dead function deleted

> Written by DeepSeek for ticket EST-427 (fenced Paperclip run: no session start/end, nothing installed, nothing
> flashed, nothing committed). Code + host tests + build only. **Status: done — all three items green; evidence
> and real exit codes below.** Read `BRIEF.md` and this file before touching anything.

### What was read (this run)
- `BRIEF.md` in full and this file from the top (the S513/EST-428 entry and the S506/S503 entries it builds on).
- `core/mc_identity.h/.cpp`, `core/mc_store.h`, `core/mc_wire.h`, `test/host/test_mc.cpp`, `test/host/run.sh`,
  `main/map_link.h/.cpp`.
- Upstream, as the ticket names: `firmware/lib/meshcore/Mesh.cpp:262-290` (the advert branch) and
  `firmware/lib/meshcore/helpers/AdvertDataHelpers.cpp` (the `AdvertDataParser` constructor).

### 1. STACK OVERFLOW before the signature check — ARMED FIRST, then fixed

**The defect, as the ticket states it:** `app_len = payload_len - 100` can be 84 (`kMaxPayload` 184), but
`message[]` is `kPubKeyBytes + 4 + kMaxAdvertData = 68`, and the cap was applied only to the length handed to
`identityVerify`, not to the `memcpy` before it.

**Armed on the unfixed code.** A 186-byte frame was fed through `decode` + `advertParse` (header
`advert << 2 | flood`, `path_len 0x40`, 184 payload bytes of `0x41`) in the new ASan stage. First ASan line:

    ==302==ERROR: AddressSanitizer: unknown-crash on address 0x78dbd5ef9c54 ...
    WRITE of size 84 at 0x78dbd5ef9c54 thread T0
        #1 ... meshcore::advertParse(...) ../../core/mc_identity.cpp:103
    ...
    [48, 116) 'message' (line 101) <== Memory access at offset 84 partially overflows this variable
    ASAN_RUN_EXIT=1

The ordinary (non-ASan) build fails the same way through glibc's fortify check:
`*** buffer overflow detected ***: terminated`, `MC_RUN_EXIT=134`. Full logs:
`paperclip-pilot/sweep/est427-asan-unfixed.log`, `est427-mc-unfixed.log`.

**The fix:** `app_len` is clamped to `kMaxAdvertData` **before** the copy, and the clamped value is used for both
the copy and the verify — exactly upstream `firmware/lib/meshcore/Mesh.cpp:269`
(`if (app_data_len > MAX_ADVERT_DATA_SIZE) { app_data_len = MAX_ADVERT_DATA_SIZE; }`), cited in a comment next to
the code. (Status: fixed; green result below.)

### 2. ADVERT NAME IGNORES THE FLAGS — ARMED FIRST, then fixed

**Armed on the unfixed code** (normal build, unbuffered so nothing is lost to the later abort):

    FAIL test_mc.cpp:354  got "p1\x..\x02..." want "EDCT"     <- 8 bytes of lat/lon binary before the name
    FAIL test_mc.cpp:355  pos.has_position
    FAIL test_mc.cpp:356  pos.lat_e6 == 46150000
    FAIL test_mc.cpp:357  pos.lon_e6 == 14990000
    FAIL test_mc.cpp:368  got "ED" want ""                    <- truncated lat/lon was read as the name

**The fix:** the app_data is parsed the upstream way (`AdvertDataHelpers.cpp`, `AdvertDataParser`): flags, then
lat+lon (8 bytes, int32 x1e6) when `ADV_LATLON_MASK 0x10`, then 2 bytes when `ADV_FEAT1_MASK 0x20`, then 2 bytes
when `ADV_FEAT2_MASK 0x40`, then the name when `ADV_NAME_MASK 0x80`. Unlike upstream, **every field is
bounds-checked against `app_len`**: a truncated field yields no name and no position, never an over-read.
`advertParse` gained an optional `AdvertPosition*` output (`lat_e6`, `lon_e6`, `has_position`); existing callers
are unchanged. `core/mc_store.h`'s `Contact` has no spare location fields, so the position is parsed and returned
only (the ticket's permitted alternative) — nothing in the UI or the store changed.

### 3. DEAD `forget()` DELETED — subtraction only

`main/map_link.cpp:forget()` and its declaration in `main/map_link.h` are gone. Nothing called it:

    $ rg -n "forget" main/ core/
    rg_exit=1            # no matches at all, before the deletion it was exactly the definition + declaration

It was not merely unused: it took the cache lock and then called `closePack()`, which `fclose()`s the pack `FILE*`
while the `om-map` worker may be inside a read of that same file outside the lock — a use-after-close waiting for
its first caller. No other line of the map read path was touched.

### Results (real exit codes)

- `sh test/host/run.sh` → **HOST_SUITE_EXIT=0**. Suites: 192 / 53 / 99 / 65 / 62 / **115** (MeshCore, was 94) /
  260 (map) / 82 (tx queue) — 0 failures. The MeshCore suite runs twice: normal (`-O2`) and the new
  **AddressSanitizer + UBSan** stage (`-O1 -g`, Ed25519 C built with the same flags); both report
  **115 checks, 0 failure(s)**. No `AddressSanitizer` error line appears after the fix (the arm run above is the
  "before"). Logs: `paperclip-pilot/sweep/est427-host.log`, and a final re-run on the finished tree
  `est427-host-final.log` → **HOST_SUITE_FINAL_EXIT=0**, **0** `AddressSanitizer` lines.
- **UBSan noise, stated honestly:** the ASan stage prints **83 `runtime error:` lines, all from the vendored
  `core/ed25519/{fe,sc,ge}.c`** (intentional left shifts of negative values in the ref10 constant-time code).
  Grouped: `fe.c` 47, `sc.c` 35, `ge.c` 1; **none from our code or the parser**. They are non-fatal by design
  (UBSan's default), so the gate is ASan (which aborts) plus the 0-failure summary; I did not patch third-party
  code to silence them.
- `make build` → **BUILD_EXIT=0**, no warnings: `application.bin` = **0x15C370 = 1,426,288 B** (32 % of the 2 MB
  app partition free), +128 B over v37's 1,426,160 B. Log: `paperclip-pilot/sweep/est427-build.log`.
- L2 suite, untouched: `cd firmware && sh test/host/run.sh` → **L2_SUITE_EXIT=0**. Log:
  `paperclip-pilot/sweep/est427-l2.log`.

### Files changed
- `core/mc_identity.h` — the position output type + the three flag constants, `advertParse`'s optional
  `AdvertPosition*`; the parsing contract comment.
- `core/mc_identity.cpp` — clamp before the copy (upstream `Mesh.cpp:269`); flags-aware, bounds-checked app_data
  parse; position returned.
- `test/host/test_mc.cpp` — `testAdvertFlags()` (four Python-oracle frames) and `testAdvertOversizeFrame()` (186 B).
- `test/host/run.sh` — the ASan+UBSan MeshCore stage.
- `main/map_link.h`, `main/map_link.cpp` — `forget()` deleted.
- `docs/PROGRESS.md` — this entry.

### What could NOT be checked (honest list)
- **Nothing was installed, flashed or committed** (the ticket's terms): the device build's size and exit code are
  real, but no ASan run happened on the P4 (host only, as asked).
- The position is **parsed and returned, not stored or drawn**: `mc_store.Contact` had no spare location fields,
  so the ticket's "otherwise only parse them" branch is the one taken. The Map still cannot show a marker.
- The **feat2 (0x40)** branch is implemented but not in the four oracle vectors (the ticket named feat1); it is
  the same two-byte skip, covered by code inspection and the shared `fields_ok` path, not by a vector.
- The ASan stage's **UBSan report list is third-party noise** (above); a future UB in our parser is reported but
  does not abort. Making it abort would mean changing the Ed25519 flags the ticket asked for.

### Next
Claude reviews and commits; no device install on my side. The Map can consume `AdvertPosition` later (store the
fields in `Contact` when the Map slice needs them).

---

## 2026-11-18 · S513 (EST-B) · z17 past 2 GiB reads, and the send queue is four slots instead of one (v37)

> Written by DeepSeek for ticket EST-428 (fenced Paperclip run: no session start/end, nothing installed, nothing
> flashed, nothing committed). Code + host tests + build only. **Version for Claude's install: 37.**

### What was read (this run)
- `BRIEF.md` in full, and this file from the top (the S506 entry and the S503/S502 entries it builds on).
- `Projects/pocket-mesh/measurements/tanmatsu-z17-2gib-diag-2026-11-18-S513.log` (the device log behind item 1).
- `core/map_pack.*`, `core/map_view.h`, `main/map_link.*`, `main/mc_link.*`, `main/chat_page.*`, `main/main.cpp`,
  `test/host/run.sh`, `test/host/test_map.cpp`, `core/CMakeLists.txt`, `main/CMakeLists.txt`.
- The drive-prefix evidence: `managed_components/badgeteam__badge-bsp/targets/tanmatsu/badge_bsp_storage.c:248`
  and the IDF it calls, `/home/slofi/dev/esp-idf-v6.0.2/components/fatfs/vfs/vfs_fat_sdmmc.c:116,147-152`,
  plus `ffconf.h:282` (`FF_FS_EXFAT 0` → `fsize` is a 32-bit unsigned DWORD, `ff.h:82`) and `ffconf.h:326`
  (`FF_FS_REENTRANT 1`).

**One thing the ticket expected that was not in the tree:** `docs/PROGRESS.md` had **no S513 entry and no EST-A
entry** — its newest entry was S506 (2026-11-15), and `git log`'s newest commit is `57e9f2a` (S506). So the EST-A
ticket's PROGRESS entry was not on disk when this run started, and item 1's evidence was taken from the ticket text
plus the measurement log above (which **is** on disk). Nothing of EST-A was present to conflict with.

### 1. Z17 PAST 2 GiB: the pack is read through FatFs, and a hole is never silent again

**The defect, as measured** (the log above): `sizeof(off_t) = 4` on the P4 (picolibc, riscv32 → a signed 32-bit
`long`), `slo17/tiles.pack` is 3,590,722,268 B, Ljubljana z17 70817/46603 (offset 1,181,885,988) read fine, Maribor
z17 71232/46338 (offset **2,493,062,600**) failed, and a raw `fseek(f, (long)2493062600, SEEK_SET)` returned -1.
The index is sorted by column, so every z17 record east of roughly 15.35 E was unreadable (~38.6 % of them, the
first past 2 GiB being record 552,148 at x 71125) and they fell into the failed ring, which is why the map showed
void at Maribor, Ptuj, Murska Sobota and Krsko.

**The fix (code):**
- `core/map_pack.*` — new rule `readRangeFor(Entry, ReadRange*)`: an `Entry` (uint64 offset) becomes the two
  **uint32** values a read call takes, and a record whose `offset + length > 0xFFFFFFFF` is **refused** (it cannot
  exist on FAT32) instead of being wrapped. This is where the offset stops being allowed to be signed.
- `main/map_link.cpp` — `tiles.pack` is opened with `f_open(..., FA_READ)` and read with `f_lseek`/`f_read`; the
  `FIL` lives in the same `OpenPack` as the index and is closed in `closePack()`, so the om-map worker stays the
  single owner (still two open files at most). `tiles.idx` stays on stdio: ≤ 21.6 MB and read by record number.
- **No guessed drive letter.** The drive is `'0' + the volume index FatFs handed out at mount` — the BSP's
  `esp_vfs_fat_sdmmc_mount` call is `badge_bsp_storage.c:248`, and that call makes the drive in
  `vfs_fat_sdmmc.c:147-152` from `ff_diskio_get_drive()` (the first free `FF_VOLUMES` slot, `:116`). It is `0:`
  today, but another FAT volume mounted first would move it, so `probeDrive()` **probes** `0:`-`9:` for
  `/maps/<pack>/tiles.pack`, keeps the one that exists, and logs it: `fat: the slo17 pack is on drive 0: (...)`
  and then `z17: pack slo17 open on drive 0:, N tiles, 3590722268 B`.
- **A failure says its offset, once per fill.** `g_read_fail_logged` is reset when a fill starts (and by `probe`),
  and the first bad read logs
  `z17 71232/46338: READ FAILED at offset 2493062600 (26000 B, seek 0 read 0 got 0) on drive 0: - logged once per
  fill`; a refused range logs `offset ... + ... B is past 4 GiB - 1, not a FAT32 tile`. Silence is what hid the
  holes for a whole session, so the log line is the acceptance evidence, not the picture.
- **Second probe point at boot** — `probe()` is now `probeOne(z, lat, lon)` per point, and the second point is
  z17 at **46.5547, 15.6459** (east, past 2 GiB of slo17, per the ticket). Its log line is the same shape as the
  existing ones, so the boot log proves the fix without opening the Map page.

**Test (host):** `test_map.cpp` gains `test_read_range_above_2gib()` — the exact failed record (z17 71232/46338,
offset 2,493,062,600) is built as 24 index bytes, decoded, and must come out of `readRangeFor()` as
`2493062600u` with `(int32_t)` negative (what the old `(long)e.offset` produced on the P4: -1801904696); the four
out-of-range edges (offset 0xFFFFFFF0 + 0x20, 0xFFFFFFFF + 2, 0xFFFFFFF0 + 0x10, length 0) are refused, the
legit last tile just under the limit is accepted intact, and a null output pointer is refused.

### 2. SEND REQUESTS: one unlocked slot → a queue of four copies

**The defect:** `queueText` ran on the main task and wrote `g_pending_text` while the radio task could be inside
`doText` reading the same buffer (≈0.5 s of airtime for 50 B at SF8 / 62.5 kHz), so a second Enter could transmit a
TORN text; and a second request REPLACED the first, so a message could vanish after the composer had cleared, with
only a serial warning.

**The fix (code):**
- `core/tx_queue.*` (**new**) — the rules, free of FreeRTOS so the host tests run the same code: 4 slots,
  FIFO, push **copies by value** (kind, channel, sender, text), `pushText`/`pushAdvert` return **false when full**
  and never replace anything, `pushAdvert` cannot displace a text, `hasText()` lets the schedule stay out of the
  way. Created once in `begin()`.
- `main/mc_link.cpp` — `g_pending*` globals **deleted**; `g_txq` (the ring) plus `g_tx_lock`, a mutex created once
  in `begin()`. `queueText`/`queueAdvert` push under the lock (microseconds — never across a transmission) and
  return false when full; `serviceRequests()` pops **one** request per loop (so a burst cannot starve receive) and
  logs `tx: N more request(s) queued`. The scheduled advert is pushed only when `!hasText()` and only to the back,
  so it can never displace or delay a message. `main/mc_link.h`'s contract comment is updated to match.
  *Deviation, stated on purpose:* the ticket said "a FreeRTOS queue"; this is the same semantics (4, by value,
  non-blocking, refuse-when-full) as a pure ring guarded by a mutex, because an `xQueueCreate` version could not be
  host-tested. If Claude prefers the literal `xQueueCreate`, it is a ~10-line swap and the rules file stays.
- `main/main.cpp` — on `queueText` false the composer is **kept** (it always was) and a 3 s notice says
  **"radio busy, not sent"**; a later successful queue clears it.
- `main/chat_page.*` — `ChatView::notice`, drawn in the page head's existing right-hand status slot (that page has
  no separate toast widget; this is the one place a short-lived line already lives, so the message list and the
  composer keep their geometry).

**Test (host):** `test/host/test_txqueue.cpp` (**new**, 82 checks): four accepted + the fifth refused with the four
first ones intact, FIFO across a ring wrap, a request is a copy (rewriting the caller's buffer after the push
changes nothing), an advert queued behind a text and refused when the queue is full of texts, and the edges (empty
text refused, over-long text cut and terminated).

### Results (real exit codes)
- `sh test/host/run.sh` → **exit 0**: 192 / 53 / 99 / 65 / 62 / 94 / **260** (map, was 245) / **82** (tx queue)
  checks, 0 failures. Log: `paperclip-pilot/sweep/est428-host.log`.
- `make build` → **exit 0**, application.bin **0x15C2F0 = 1,426,160 B** (32 % of the 2 MB app partition free).
  Log: `paperclip-pilot/sweep/est428-build2.log`.

### Files changed
- new: `core/tx_queue.h`, `core/tx_queue.cpp`, `test/host/test_txqueue.cpp`
- changed: `core/map_pack.h`, `core/map_pack.cpp`, `core/CMakeLists.txt`, `main/map_link.cpp`, `main/mc_link.h`,
  `main/mc_link.cpp`, `main/main.cpp`, `main/chat_page.h`, `main/chat_page.cpp`, `test/host/test_map.cpp`,
  `test/host/run.sh`

### What could NOT be checked (honest list)
- **Nothing was built onto the device, flashed, installed or committed.** So the two device claims are unproven by
  me: that `f_lseek(2,493,062,600)` returns FR_OK on the real card, and that the boot log prints the z17 Maribor
  line with real pixels. The second probe point exists exactly so Claude/Filip can read that off the boot log.
- **The drive letter is `0:` on this build only by measurement of the BSP/IDF code paths, not on the device** — the
  probe makes the code right either way, but the line `fat: the slo17 pack is on drive ?:` has never been printed.
- **`FF_FS_EXFAT 0` is a build fact read from `ffconf.h:282`, not a runtime check.** If a future sdkconfig enables
  exFAT, `FSIZE_t` becomes a QWORD and `readRangeFor()`'s 4 GiB refusal becomes stricter than necessary (safe, but
  worth re-reading then). The 32 GB FAT32 card is the intended configuration.
- **The notice is a host-render/intent, not seen on the glass**: the page-head slot is 1 line and "radio busy, not
  sent" fits at the head's hint size, but that is a sizes argument, not a screenshot.
- **No L2 build was run**: nothing under `firmware/` was touched, so the L2 cannot have been affected (the shared
  `theme.h` include is read-only and unchanged).
- **Rule 9 note:** the ticket asked for the Maribor coordinate as a probe point and the tree already carried a real
  coordinate in `main.cpp`'s first Map view; this entry and the code therefore name both, as instructed, and nothing
  else geographic was added.

### Next
1. Claude installs **v37** and reads the boot log for the two lines: `fat: the slo17 pack is on drive ?:` /
   `z17: pack slo17 open ... 3590722268 B`, then `z17 tile 71232/46338 -> 256x256 ... px0 0x....`
2. On the glass: pan east at z17 (Maribor, Ptuj, Murska Sobota, Krsko) and confirm terrain instead of void; then
   type two messages quickly and confirm both leave in order, and that a third shows "radio busy, not sent" with
   the text still in the composer.
3. Then the rest of step 6: RPTR, Settings, the clean card unmount.

---

## 2026-11-15 · S506 · the zoom no longer freezes, and the hardware codec is used only when it proves itself (v34 → v36)

> Written by DeepSeek, S506, with Filip at the device. Two asks in one session: *"also fix the zoom lag problem,
> it takes too long to change zoom on the map"*, then, after v34, *"zoom is good now, but the map colours are weird
> and not correct!"*. The second half of this entry is that defect, its cause, and the check that now decides it
> at every boot.

### Slice 1 (v34): decoding left the drawing task

- **`core/map_view.*`** — the tile cache grew a state machine, because a slot can now be *being written*:
  `SlotState::Empty/Pending/Ready`. `find()` hands out **Ready only**; `take()` never evicts a **Pending** slot
  (the worker holds a raw pointer into that buffer); `commit(slot, ok)` and a 24-slot **failed ring** so a hole at
  the edge of the pack is not asked for on every repaint. New pure function **`planTiles()`**: the tiles still
  missing, in **centre-out** order (so what is under the middle of the screen is decoded first), with the exact
  order pinned by the host tests.
- **`main/map_link.*`** — one **worker task** (`om-map`, priority 2, 12 KiB stack). The mutex covers the cache,
  the queue and the slot array; it is **never held during a decode**. `showView()` plans the missing tiles and
  rings the doorbell; the worker picks and reserves a slot in one critical section (so a re-plan can never pair a
  tile with the wrong zoom's pack); `drawTile()` blits a ready tile under the lock so it cannot be evicted
  mid-copy; `busy()` / `takeLanded()` drive the page's repainting.
- **`main/map_page.*`** — draws cached tiles only, never decodes, and calls `showView()` on every compose (cheap
  and idempotent). Logs `N of M drawn, K queued` so a fill is visible in the log, not just on the glass.
- **`main/main.cpp`** — while the map is filling, the input poll drops **250 ms → 30 ms** and a landing sets
  `dirty`, so a tile reaches the panel within a frame. The panel blit is now measured: `map frame: present N ms`.
- **Host tests: 245 checks, 0 failures** (was 176). Five deliberate mutations, each red at a named line:
  `find()` handing out a pending slot, evicting a pending slot, a row-major plan instead of centre-out, planning a
  known-missing tile, and a failed decode staying drawable. **Two of those first went red by failing to *compile*
  (unused-variable under `-Werror`), which is not a guard** — they were re-armed so the only way to red is an
  assertion.

**On the glass (v34):** `fill: 14 tile(s) in 525 ms`, `12 tile(s) in 551 ms`, `4 tile(s) in 328 ms`;
per frame `composed in 6-26 ms` plus `present 21-41 ms`; steady-state pan frames `12 of 12 drawn, 0 queued,
composed in 14 ms`. The first frame after a zoom reads `0 of 8 drawn, 8 queued` and the map fills in from the
middle with the keys still answering. **Filip: "zoom is good now".**

### The colour defect in v34, and the boot check that caught it

The same log carried, at boot:

    decoder check: tile 256x256, JPEG sampling 942682966
    decoder check: RGB/BT601 -> 2 ms, distance 11 (px0 0xFFFF vs 0xF7DD)
    decoder check: RGB/BT709 -> 2 ms, distance 12 (px0 0xFFFF vs 0xF7DD)
    decoder check: BGR/BT601 -> 2 ms, distance 6  (px0 0xFFFF vs 0xF7DD)
    decoder check: BGR/BT709 -> 2 ms, distance 5  (px0 0xFFFF vs 0xF7DD)
    decoder check: using hardware decode, BGR/BT709 (distance 5, 2 ms vs sw 41 ms)

Two of my own mistakes, found by measurement rather than by eye:

1. **The compressed input buffer was a plain `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.** The P4 codec's DMA reads
   the JPEG back out of PSRAM, and the IDF's own example allocates **both** directions with
   `jpeg_alloc_decoder_mem` (input = `JPEG_DEC_ALLOC_INPUT_BUFFER`), which is what gives the cache-line
   alignment. On v34 the codec was decoding from a buffer it could not safely read: 991 of 1024 sampled pixels
   differed from the software decoder, which is the "weird colours", with the shapes intact.
2. **`rgb_order` was hard-wired to RGB.** Against the software reference, BGR scores 5-6 and RGB scores 11-12,
   i.e. the order that was assumed was the wrong one for this codec.

Driver fact read in the source, not assumed: for RGB565 output the colour conversion is hard-wired to
**YUV420 → RGB565** and is chosen from the *output* format, not from the tile's own sampling
(`esp_driver_jpeg/jpeg_decode.c:544-553`).

### Slice 2 (v35/v36): the software decoder is the referee

- **`verifyDecoders()`** runs at boot: one real tile is decoded **four ways** (RGB/BGR × BT601/BT709), each scored
  against the **software** decoder (the one whose pixels were confirmed on the glass in S503) by mean per-pixel
  channel distance over 4096 samples. The best option is used **only if it is within 12**; otherwise the engine is
  deleted, a flag keeps it deleted, and the log says hardware decode is DISABLED and the software path is in use
  (~41 ms a tile). **A fast wrong map is worse than a slow right one, and the choice is measured at every boot.**
- **The same check caught the defect** before Filip had to describe it, and it is the only reason the cause could
  be named instead of guessed.
- **v35 on the glass: `works!` (Filip).** The card-read number in the same log exposed one more defect of mine:
  **the index hint saved nothing** — `fill: 14 tile(s) in 525 ms (249 card reads)` is ~18 reads per tile, i.e. a
  full binary search each time. It was keyed by `y & 3` while the centre-out plan alternates columns inside a row,
  so each request overwrote the hint it needed. **v36** keys it by column (`x & 7`, 8 slots), expects ~3-4 reads
  per tile, and also drops the sampling field from the boot line (this IDF leaves it unset: it printed
  `942682966`, so only the tile size is reported).

### What could NOT be checked

- **Tile joins are not proven pixel-exact.** In the same tile, the corner pixel still differs between the two
  decoders (`px0 0xFFFF` hardware vs `0xF7DD` software) while the sampled mean distance is 5, which is consistent
  with an edge/padding artefact of the codec. Filip sees no seams, but "no seams seen" is not "no seam exists".
- **The v36 card-read count** was not measured before this entry was written (the install needed Filip's hands).
- The status-refresh compose every 2 s still recomposes the whole page; it is 14 ms and was left alone.

### Next

1. **v36's numbers**: `fill: N tile(s) in X ms (M card reads)` should show ~3-4 reads per tile instead of ~18.
2. The corner-pixel question above, if a seam ever shows up.
3. Then the rest of step 6: **RPTR, Settings**, the clean card unmount, and the advert schedule in Settings.

## 2026-11-15 · S503 (continued) · step 6 slice 2: the Map page draws the offline map (v32)

> Written by DeepSeek, S503, the same night. Slice 1 proved a tile could be found, read and decoded; this slice
> puts tiles on the panel and gives the page its keys. **Filip: "hey, I see the map!"**

### What was built

- **`core/map_view.*`** (rules, host-tested): `viewTilesFor()` (which tiles fill w x h from a world pixel, with the
  shift so the blit can be placed, and **clipping at the last tile** instead of asking for tiles that do not
  exist), `panBy()` with clamping at all four edges, `zoomTo()` that **keeps the centre of the screen on the same
  place** across a zoom change, and a **`TileCache`** of 8 slots with least-recently-used eviction.
- **`main/map_link.cpp` reworked into the page's data source**: one pack open at a time (2 file handles, not 6),
  `tile(z,x,y)` returning a cached decoded 256x256 RGB565 tile, 8 PSRAM buffers (1 MB), and a failed decode drops
  its cache key so the next look retries instead of showing garbage. `probe()` stays as the boot health line.
- **`main/map_page.*`**: status bar + page head (`z16`), the map area 800x386, tiles blitted by `memcpy` per row
  with clipping, a void colour under everything (so a hole is visible as a hole), a crosshair at the centre, the
  **Esri attribution** bottom right (the source's terms require it on screen), and the hint bar
  `dpad pan · +/- zoom · esc back`.
- **Keys**: arrows pan by 64 px (a quarter tile), `+`/`=`/`-` zoom within z9-17, Esc back through the router.

### On the glass (v32, `om-lite | OM Lite | 32 | 1357K`, 1,390,800 B)

    I (5307) tm/app: SD mounted at /sdcard
    I (5647) tm/map: z16: pack slo16 open, 225792 tiles
    I (5737) tm/map: z16 tile 35496/23275 -> 256x256 in 430 ms, px0 0xDEFB
    I (5917) tm/map: z15: pack slo open, 75986 tiles
    I (6017) tm/map: z15 tile 17748/11637 -> 256x256 in 278 ms, px0 0xEF98
    I (10567) tm/map: z16: pack slo16 open, 225792 tiles

**The map is on the panel** and Filip confirmed it. No decode failures in the log. Two measured facts:
- **430 ms for the first tile** (open the pack, search 18 records, read 26 KB, decode), then **~57 ms** once the
  pack is open (measured in slice 1).
- **The first full draw is ~20 tiles**, so it composes in roughly a second; after that a pan only needs the newly
  exposed column or row, which the cache covers.

### What is honest to call unproven

- **Panning and zooming have not been checked by me in the log yet**: the page redraws on a key, and a redraw that
  decodes a new column logs nothing (decode is at DEBUG level). Filip has the page; his word on seams, panning and
  zoom comes next.
- **No seams were verified** by measurement: adjacent tiles come from the same pack and are placed by
  `col*256 - shift`, so a seam would mean an arithmetic error in `viewTilesFor` (which the host tests cover with
  exact numbers, but that is not the same as the panel).
- **Holes at the edge of the data** (outside Slovenia's bounds) draw as void; that is deliberate, and not yet seen.
- **Speed under a held key** is unknown: each pan step is a full page recompose and one panel blit.

### Filip's pass (v32 then v33): panning fixed, zoom still slow, measured

His report on v32: *"moving / zoom kinda works, but it is veeeery laggy"*. **The cause was mine and in the numbers:**
the tile cache held **8** slots while a view needs **up to 15** (5x3, 800x386 starting mid-tile), so every redraw
evicted tiles it was about to ask for again and the whole screen was decoded per press.

v33 raised the cache to **20 slots** (2.5 MB PSRAM) and made the page log what a compose cost. On the glass:

    view z15 4x3 tiles: 0 hit / 12 miss, composed in 1345 ms     <- a zoom press
    view z15 4x3 tiles: 12 hit / 0 miss, composed in 14 ms       <- every pan press, and every 2 s refresh

**Panning: 14 ms.** **Zoom: 1345 ms**, and the reason is structural rather than a bug: a zoom change is a different
zoom level, so all 12 visible tiles miss and each costs ~112 ms (30 ms index search on the card + 57 ms decode +
read). Also visible in that log: the page is recomposed every 2 seconds by the status refresh, which costs 14 ms and
is now the cheapest thing on the screen.

### Next

1. **A non-blocking compose (the zoom fix):** draw the cached tiles, then let a background task decode the rest and
   repaint as each tile lands. Same 1.3 s of work, seen as a map filling in rather than a frozen screen, and it also
   removes the pan hitch on every fourth press (the one that reveals a column). Considered and rejected for now:
   putting the index in RAM (saves ~30 ms of each 112), decoding at half scale first (blocky), scaling the previous
   frame 2x as a stopgap (more code than the task).
2. Filip's remaining pass: seams, zoom centring, the attribution's placement.
2. **Pan performance**: cache-aware redraw (blit the previous frame shifted, decode only the new edge) if a held
   key feels slow.
3. The clean card unmount (still open from slice 1).
4. Then RPTR and Settings.

## 2026-11-15 · S503 (continued) · step 6 slice 1: the map pack reads and decodes on the device (v31)

> Written by DeepSeek, S503, in the hour Filip had. Scope agreed with him up front: **the reader proven on real
> tiles, not a drawn page** — a Map page with pan and zoom in that hour would have been a claim I could not verify.

### What was built

- **`core/map_pack.*`** (rules, host-tested **35 checks**): the 24-byte index record (z u8, x u32, y u32, offset
  u64, length u32), `findTile()` as a **binary search through a record-reader callback** (so the same code reads a
  card in the device and an array on the host), `packDirForZoom()` (9-15 -> `slo`, 16 -> `slo16`, 17-18 -> `slo17`,
  and **nullptr below 9**: the base pack starts at z9, so guessing a pack there only turns a missing zoom into a
  failed search), and the slippy-map coordinate maths both ways.
- **`main/map_link.*`** (device glue): opens `<pack>/tiles.idx`, does the search with ~18 small `fseek`+`fread`
  reads instead of loading a 5.4 MB index into RAM (the L2's approach for its overlay packs), re-reads the found
  record for its offset and length, seeks in `tiles.pack`, reads the tile, and decodes with **`espressif/esp_jpeg`
  1.0.5** into a 256x256 RGB565 buffer in PSRAM.
- **The decoder**, chosen with a reason: no JPEGDEC existed locally (the L2 uses it through PlatformIO) and the IDF
  ships none, so `espressif/esp_jpeg` was added as a managed component. **It cannot use the chip's ROM decoder on
  the P4** (the component refuses to build), so `sdkconfigs/tanmatsu` now sets `CONFIG_JD_USE_ROM=n`,
  `CONFIG_JD_FORMAT_RGB565=y`, `CONFIG_JD_SZBUF=512`. **Version 1.0.1 has an upstream bug** (its output callback is
  declared `unsigned int` where TJpgDec wants `int`, so it does not compile at all); **1.0.5 compiles**.
- Boot calls `om::maplink::probe(46.15, 14.99)` once the card is mounted: central Slovenia, where this device is.

### On the glass (v31, `om-lite | OM Lite | 31 | 1354K`, 1,387,712 B)

    I (5284) tm/app: SD mounted at /sdcard
    I (5284) tm/map: probe: 46.15000, 14.99000
    I (5724) tm/map: z16 tile 35496/23275 -> record 79707 of 225792 (26170 B at offset 648046191)
                    -> 256x256 RGB565 in 57 ms (search 30910 us), px0 0xDEFB, centre 0xDF78
    I (6004) tm/map: z15 tile 17748/11637 -> record 39233 of 75986 (31337 B at offset 569643109)
                    -> 256x256 RGB565 in 63 ms (search 29776 us), px0 0xEF98, centre 0xD79D

Read it as four separate facts: the **card mounts in the device** after the reformat; the **binary search finds a
tile in a 225,792-record index** on the card; the **pack seek returns real JPEG bytes** at the offset the index
names; and **esp_jpeg decodes them to 256x256 RGB565 in 57-63 ms**, with terrain colours at the centre, not black.
The decode is fast enough for a page (three tiles across 800 px is ~170 ms of decode, which is why the L2 caches
eight frames).

### Numbers worth recording

- **Search 30 ms** for ~18 seeks on FAT over SDMMC. Acceptable once per tile, too slow to do per frame: the page
  must cache the decoded tiles (the L2 keeps 8 x 256x256 = 1 MB in PSRAM).
- **Decode 57-63 ms** per tile at 256x256, RGB565 out.
- Tiles are 26-31 KB here (z16/z15), not the 5 KB the earlier hand-checked sample showed: the sample was a dense
  urban tile, these are countryside.

### The unmount, honestly

`sync()` **does not exist** on this target (the link fails), and a clean unmount needs the sdmmc card handle that
`bsp_storage_mount_advanced()` keeps to itself. So the FAT is still left with its dirty bit set. **No written data
is at risk:** every card path we own (history) opens, writes, `fflush`es and closes per record. Closing it properly
is the first task of the next slice: either an unmount wrapper in the BSP, or mounting through the IDF ourselves.

### Next

1. **The Map page itself**: a decoded-tile cache in PSRAM, draw the 800x480 window around a centre tile, arrows to
   pan, `+`/`-` to change zoom (z9-17), the Esri attribution line the licence requires.
2. The clean unmount (above).
3. Then RPTR and Settings.

## 2026-11-14 · S503 (continued) · the card's final layout: three packs, the shape the L2 already expects

> Written by DeepSeek, S503, after Filip picked the layout. **Correction to the section below, dated and signed:**
> it says two packs named `slo-z9-16` and `slo-z17`. That was replaced the same session. Reading the L2's own
> `firmware/src/map.cpp` showed it already expects **three** packs at fixed paths (`/maps/slo` for z9-15 with its
> index in PSRAM, `/maps/slo16` for z16 and `/maps/slo17` for z17, both searched from the file), so the card was
> re-split to match: one card now serves both handhelds and the L2 gains z16/z17 with no code change.

Final layout on the card (FAT32, label `TANMATSU`, 30 GB, 6.0 GB used):

| directory | zooms | tiles | `tiles.pack` | `tiles.idx` | sha256 (pack / idx) |
|---|---|---|---|---|---|
| `/maps/slo` | 9-15 | 75,986 | 945 MB | 1.82 MB | `5a8d464e…9abd87` / `c6a20dc1…10efb1` |
| `/maps/slo16` | 16 | 225,792 | 1,733.6 MB | 5.42 MB | `78de707b…0d024b` / `4d8880cc…ba5c88` |
| `/maps/slo17` | 17 | 899,284 | 3,424.4 MB | 21.58 MB | `ee1d3706…37a226` / `e7e5fb57…a03200` |

Plus `/maps/SHA256SUMS` (the six hashes above) and the restored `/om-lite/history/`. **All fifteen files** (three
packs × pack/idx/manifest/meta/attribution) **matched the source when read from the medium with `O_DIRECT`,
`VERIFY_RC=0`.** The z17 pack was moved by renaming its directory on the card rather than copied again, since it was
already the identical file; only 2.7 GB of new data was written.

The card was then unmounted cleanly and CD was shut down (Filip was leaving; no work is left running there).

### Three things worth keeping from this half of the session

- **Look for the reader before writing one.** The L2 has had a pack reader since EST-357: binary search over
  `tiles.idx`, JPEGDEC into an 8-frame 256×256 RGB565 cache in PSRAM, two overlay packs, and `sd_budget.h` counting
  the open card handles. The Tanmatsu's Map page should be a port of that, not a second design.
- **The naming was mine to get wrong, and it was wrong.** Inventing `slo-z9-16` cost a re-split and 2.7 GB of extra
  writes; the L2's fixed paths were the existing convention. Same lesson as the entry above: read the existing
  artefact first.
- **TestBox's card slot and CD's internal slot both fail to detect this card**; everything went through a USB reader,
  whose quirks (early write acknowledgement, one re-enumeration under load) shaped all of the verification above.

## 2026-11-14 · S503 (continued) · the map packs are on the card: z9-17 Slovenia, two packs, verified from the medium

> Written by DeepSeek, S503, at Filip's instruction: take the "Slovenija - Esri Topo" tiles off CD and put them on
> the Tanmatsu's card. Nothing here is wired into the app yet — step 6 writes the reader that opens these files.

### The source, as the artefact describes it (not as the filename does)

`/home/slofi/maps/mbtiles/slovenija-esri-topo-z9-16.mbtiles` on CD, **7,248 MB**. Its metadata says
`name = Slovenija - Esri Topo`, **minzoom 9 / maxzoom 17**, `format = png` — but the tiles are **JPEG** (the builder
sniffs and says so), and the **filename says z9-16 while the content is z9-17**. Per-zoom counts, read from the file:

| z | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 |
|---|---|---|---|---|---|---|---|---|---|
| tiles | 20 | 70 | 247 | 925 | 3,626 | 14,356 | 56,742 | 225,792 | 899,284 |
| MB | 0.6 | 1.7 | 6.2 | 19.6 | 65.3 | 220.3 | 631.6 | 1,733.6 | 3,424.4 |

**1,201,062 tiles / 6,103 MB of image data**, which matches Filip's numbers exactly.

### Why two packs, and what is on the card now

FAT32 caps a single file at 4 GiB, so the set cannot be one pack. `tools/tiles_export.py --pack` was used on CD:

| pack | zooms | tiles | `tiles.pack` | `tiles.idx` | sha256 (pack / idx) |
|---|---|---|---|---|---|
| `slo-z9-16` | 9-16 | 301,778 | 2,809,209,108 B | 7,242,672 B | `d6d40880…dc38` / `df2e7766…30ce` |
| `slo-z17` | 17 | 899,284 | 3,590,722,268 B | 21,582,816 B | `ee1d3706…a226` / `e7e5fb57…0035` |

On the card: `/maps/slo-z9-16/` and `/maps/slo-z17/`, each with `tiles.pack`, `tiles.idx`, `manifest.json`,
`meta.txt`, `attribution.txt`, plus `/maps/SHA256SUMS`. 6.0 GB used of 30 GB. `om-lite/history/` was restored from
a backup (see below). **All four big files and all six small files were verified equal to the source**, the big ones
read with `O_DIRECT` (`PACKS_VERIFY_RC=0`), and the index is self-consistent: records × 24 = file size, manifest
count matches, and the last tile's `offset + length` lands exactly on the pack's end.

### What the card has to be (measured from the app's own sdkconfig)

`CONFIG_FATFS_*`: **FAT12 / FAT16 / FAT32**, long filenames on the heap. **No exFAT symbol in the build**, no NTFS,
no ext4. The card is now FAT32, label `TANMATSU`. A 4 GiB file cap follows from that, which is the whole reason for
the two packs.

### Three things that cost real time, all mine to own

1. **TestBox's SD slot does not detect cards at all.** Its GL9750 controller is up (`mmc0: SDHCI controller on PCI
   [0000:05:00.0]`), but no card event was ever logged, no `/dev/mmcblk0` appeared, and no USB reader was present.
   That is a hardware fault on this machine: the plan to copy on TestBox was never possible.
2. **This USB reader acknowledges writes before they reach flash.** It reports `write through` and claims no cache,
   but `SYNCHRONIZE CACHE` is not honoured, so a read-back straight after a copy sees stale bytes. Three of my
   verifications failed for that reason and I twice stated the wrong cause out loud ("the card returns a different
   hash on every read", then "the card loses writes"). **Correction (dated, S503):** both were wrong. The same file
   hashes `d6d40880…` on the source and on two card reads once the card has drained, and `O_DIRECT` reads of the same
   blocks were identical every time. **Lesson: verify a copy on this reader only after it has settled, and read the
   card with `O_DIRECT` so the hash comes from the medium.**
3. **The reader also re-enumerated mid-run** (`/dev/sda` vanished, the card came back as `/dev/sdb`), which produced
   `FAT read failed` / `Directory bread failed` in the kernel log and left the old mount pointing at a dead device.
   Under a sustained 3.4 GB read it dropped once. Any tooling here must expect the device name to change and the
   mount to be stale.

### `f3probe` cleared the card, and cost its contents

`f3probe --destructive /dev/sda`: **"The device is the real thing"**, 29.72 GB usable, 29.72 GB announced, module
32 GB, no fake cache. The card is genuine and full-size. The probe is destructive, so the card was wiped: it was
reformatted (`mkfs.vfat -F 32 -n TANMATSU`) and `om-lite/` was restored from
`~/maps/sdcard-backup-2026-11-14-S503/` on CD (the 427-byte chat log, copied before the probe).

### Found in the same pass, for the firmware, not the card

The kernel logged **`FAT-fs: Volume was not properly unmounted`** for the card. The app never unmounts it (no
`f_mount(NULL)` / `esp_vfs_fat_sdcard_unmount`, no sync on the way out), so every power-off leaves the FAT dirty.
With a 6 GB map pack on that card this is a data-integrity risk worth closing next to the Map page.

### Next step

**Step 6, the Map page**, and with it the reader: `tiles.idx` (24 B records: z u8, x u32, y u32, offset u64,
length u32) into `tiles.pack`, **two packs** to pick between by zoom, plus the attribution text the licenses
require. One decision to make with Filip first: whether the reader takes a pack path from the card (two entries) or
scans `/maps/*/manifest.json`.

## 2026-11-14 · S503 (continued) · the seen point, wrapping and scrolling — batch of three, proven on the glass (v29)

> Written by DeepSeek, S503, after step 5 was closed earlier the same session. Filip picked this batch: the
> remaining history gap (**unread after a restart**) plus the two chat gaps he feels first (**wrapping** and
> **scrolling**), in one build so the device is flashed once.

### What was built

- **`core/chat_layout.*`** (new, host-tested): `wrapText` (greedy word wrap through a **measure callback**, so the
  same code runs on the host with a fake 10 px font and on the P4 with the real VLW font; UTF-8 safe, long words
  hard-broken, spaces at a break consumed, at most `kMaxLines` = 4, `clipped` when text is left over),
  `chatWindow` (which messages fit, oldest-first window from the newest end, `scroll` counted back from the
  newest, `hidden_above/hidden_below` for the indicators) and `historyReadCount` (the seen point turned into "how
  many of the loaded messages are read").
- **The Meta record** (the format's `{seenLogId u32, unread u16}`, `history_log.h`): `mc_history` gained
  `historyEncodeMeta` / `historyReadSeen` (last record wins, torn tail respected) and `historyLoad` now reports
  the file's highest log id. `main/sd_history.*` gained `appendSeen` / `loadSeen`.
- **`mc_link`: one call for "the user has seen this channel"** — `noteChannelRead(ch)`: marks the store read AND
  appends a Meta record, **only when the point moved**, so a channel left open does not write on every redraw.
  Replaces the three copies of the mark-read rule that had started to drift (open, refresh, Esc).
- **A real defect fixed on the way:** the record's `logId` used to carry the **ring** sequence number, which
  restarts at 1 on every boot. The L2 keeps those two apart for exactly this reason (`Msg.logId` = the log's id,
  `Msg.seq` = "the RAM ring's per-boot id, kept for the USB dump"). Now the file has its own consecutive id
  (`Message.log_id`), restored at boot as highest + 1, and that is what the Meta record points at.
- **Compatibility:** a log written before this slice has no Meta record, so it takes the old rule (everything
  loaded counts as read). An upgrade therefore cannot show two days of phantom unread.
- **UI:** each message draws its wrapped lines (up to 4), the last one ending in `...` when cut; Up/Down scroll;
  the page head shows `17 older` / `3 newer` instead of a bare count while scrolled; the hint bar gained
  `up/dn scroll`.

### Tests, and what they were armed with

Host suites **192 / 53 / 99 / 65 / 40→65 / 62 / 94, exit 0** (the history suite grew 40 → 65, the new layout
suite is 62). Four deliberate mutations each turned a named line red before the code was restored: no word
breaks (`test_layout.cpp:82`), the file id off by one (`test_history.cpp:156`), the height budget ×3
(`test_layout.cpp:149`), and the seen-point boundary `>` → `>=` (`test_layout.cpp:181`).

### On the glass (v29, `om-lite | OM Lite | 29 | 1335K`, 1,367,664 B)

- First boot after the upgrade: `ch0: 2 message(s), no seen point yet (a log from before S503): all counted as
  read` — the compatibility rule, as designed.
- Opening the channel: `ch0: seen point -> log id 2`. Sending a 119-character message: `chat: queued for channel
  0 (119 chars)`, `TX ch0 "Test Channel" 149 B ts 1794642765 -> ok`, then `ch0: seen point -> log id 3` — two
  writes for three messages, i.e. only when the point moved.
- **The feature itself:** with the channel closed, a message arrived from the L2, the badge showed, and after a
  restart the boot line read **`ch0: 3 of 4 read (seen point 3), 1 unread`** and **Filip confirmed the badge was
  still there**. On v28 it would have been wiped.

### Found on the glass, fixed in v30 (built, not installed when this was written)

- **Scrolling in a channel that already fits did nothing useful:** with three messages the first Up press hid the
  newest one and the head read `2 newer` — history that was not there. `chatWindow` now defines `max_scroll` as
  the point where the OLDEST message reaches the top of a **full** window, so it is 0 when everything fits and the
  key is a no-op; the caller clamps its own counter against it. Two host checks pin both directions.

### Tooling notes (cost real time twice)

- **The P4's tty is re-created on every app start** (`ttyACM0` → `ttyACM2` …). A reader that holds the old fd gets
  0-byte reads for ever instead of an error, so a capture can sit silently dead while the device talks. The
  capture helper now compares `realpath(by-id)` each pass and reopens when it changes.
- **Never `pkill -f tm-s503-cons.py`** — the pattern matches the shell that runs it (SIGTERM to self). Use
  `pgrep -af 'tm-s503-cons[.]py'` / the bracket trick.
- **One reader per port.** Three readers were alive at once at one point; two readers on one tty split the byte
  stream (memory: `reference_two_readers_split_a_stream`). Nothing read that way is affected (the evidence is
  self-consistent), but the rule going forward is: check `pgrep` first, kill strays, then capture.

### What could NOT be checked

- The card still was not read from TestBox (needs the card out); the device-side load validates the bytes.
- **Long-message wrapping beyond ~2 lines** was not seen on the glass: the test message was 119 characters, which
  is two lines at this width. The 4-line case exists only in the host test (`test_clipping_stops_at_the_line_cap`).
- The measured window geometry (22 px line, 8 px gap, 288 px of area) is my reading of the existing layout, not
  something Filip has judged on the glass yet.
- `unread` in the Meta record is always written as 0: the app restores unread from the id distance, so the field
  is redundant here. The L2's own value is 1..n; ours assumes 0. Not wrong for our loader, but it is a place two
  devices could disagree.

### v30 on the glass (same session, right after the batch above)

- Installed as **30** (`om-lite | OM Lite | 30 | 1335K`, **1,367,776 B**, host **192 / 53 / 99 / 65 / 62 / 94**).
- **The scroll clamp works:** on "Test Channel" with everything on screen, Up does nothing — the newest message
  stays and the head keeps saying `N messages` (v29 turned three messages into `2 newer`, i.e. hid the newest to
  show history that was not there).
- **A read channel stays read:** opening it wrote `ch0: seen point -> log id 4`; the badge had already cleared
  and Filip confirmed it stays away when back on the channel list. The boot before that read
  `ch0: 3 of 4 read (seen point 3), 1 unread`, which is the pair: the distance is what unread means.
- **Wrapping in full:** the 119-character message reads complete over two lines (not ellipsised).
- Not re-checked after v30: a channel with **more history than fits** (the `N older` indicator on real content);
  the 4-line wrap; whether Up is reachable from a page where the composer has focus. All three are still
  host-tested only.

### Next step

**Nothing in flight on the device** — v30 is installed and the three checks passed. Next session: **emoji glyphs**
(the generated subset has none, the bot sends emoji), then **step 6** (Map from the SD pack, RPTR, Settings),
reply paths, then DM/Trace/Favourite. Worth one pass on real content first: a channel with more history than fits
(`N older` on real messages) and a message long enough to need four lines. Still open from 3a: RSSI reads 0 dBm.

## 2026-11-14 · S503 (DeepSeek, session with Filip) · step 5 CLOSED: the chat history survives a restart, off the card

> Written by DeepSeek, S503. v28 was built and unproven at the end of S502. It is now on the device and
> **proven on the glass**: the chat written to the card came back after a full app restart, both directions.
> Everything below is from a command run in this session, quoted verbatim; the log is `/tmp/tm-s503-v28c.log`.

### What was installed, and the number that was wrong in S502

- `appfs list` before: `om-lite | OM Lite | 27 | 1328K`. Rebuilt from the current tree (`make build` exit 0,
  binary already up to date), host suites re-run **192 / 53 / 99 / 40 / 94, exit 0**. Uploaded over BadgeLink as
  **28**: `appfs list` after: `om-lite | OM Lite | 28 | 1332K`, upload "Done!".
- **Correction to the S502 entry (DeepSeek, S503):** it says v28 is "1,365,168 B". The real binary is
  **1,364,400 B** (`0x14d1b0`), sha256 `336340a3768e44b18d2cf0b913a7d3d02c049c7f2f159fe0e89bdc4e69b00c27`,
  with a build that was already current for the tree (no source newer than the binary). The size was written
  wrong in S502; the fix, the suite totals and the install were all as recorded.
- Not used: `appfs crc32` as a second opinion. It answers on its 2 s default timeout for a 1.3 MB app and
  times out; the upload's own end-of-transfer CRC is the check that ran.

### The proof, as the device printed it

First run (v28, app started before the capture was listening): the message was typed and sent.

    I (46879) tm/app: chat: queued for channel 0 (4 chars)
    I (47509) tm/mc: TX ch0 "Test Channel" 37 B ts 1794640891 -> ok
    I (51489) tm/mc: MSG ch0 "Test Channel" from "ERA-3 (SI)": [OM Bot] PONG | Indeed.

Then Esc to the stock launcher and the app started again — the boot this whole step was waiting for:

    I (5295) sdcard: Mounted SD at '/sdcard'
    I (5295) tm/app: SD mounted at /sdcard
    I (5295) tm/hist: history: ON at /sdcard/om-lite/history
    I (5435) tm/mc: channels: 1 loaded
    I (5435) tm/mc:   ch0 "Test Channel" hash 0xD7
    I (5435) tm/hist: history: 2 message(s) from /sdcard/om-lite/history/ch-d7-Test_Channel.log

**2** = our `ping` and the bot's reply, i.e. both directions were written and both came back. Filip confirmed on
the glass that both lines are in the channel, his own on the right under "you" and the bot's on the left.

### Why this is stronger than "the file is there"

`mc::historyLoad` walks the file with the L2's own `decodeRecordAt`, which refuses a record whose magic,
version, length or **CRC-32** (`crc32Two(buf+1, 5, payload)` in `firmware/src/history/history_log.h:405-421`)
does not match. Both records had to be byte-valid in the L2's format to be counted and shown, so the log is
already known to be readable by the L2 — not merely present on the card.

- Boot also shows the clock came from the CH32 RTC on its own (`clock: from the coprocessor RTC:
  2026-11-14 07:23:50Z`) and the advert went out with a real `ts 1794641032` — S502's work survived the night
  and a restart without Wi-Fi.

### Found in the log, not chased (new, for the list)

- `W rx 114 B: advert with an INVALID signature, dropped` about 3.5 s after boot. Some peer's advert failed
  Ed25519 verification. It may be a genuinely malformed advert from another node, or our verifier being
  stricter than MeshCore. **Unresolved, and worth one careful look** — a verifier that rejects honest adverts
  would keep other nodes out of our Nodes page with no other symptom.
- `rx 39 B group text, no channel of ours opens it (hash 0xD7, rssi 0 dBm)` — our own echo after `TX 37 B`.
  In S502 the echo was dropped as a duplicate; here it arrived as "no channel opens it", i.e. the MAC did not
  open our own frame. The message clearly worked (the bot answered), so this is about how the echo is
  produced/routed, not about sending. **Unresolved.**
- Still open from before: **RSSI reads 0 dBm** (the field is not populated on the RX path).

### What could NOT be checked

- The file was **not** read from TestBox: that needs the card out of the device. The device-side load (above)
  already validates the bytes, so this is an independent second view, not a missing proof.
- Unread state across a restart: the boot marks what it loads as read, so a message that arrived while the
  channel was closed comes back as already-read. That is the next slice (the format's Meta/seen record).

### Next step

`NEXT-FOR-DS.md` § step 5 is closed. The named next items are: the **Meta (seen-point) record** so unread
survives a restart, then **message wrapping**, **scrolling inside a channel**, **emoji glyphs**, then step 6
(Map from the SD pack, RPTR, Settings).

## 2026-11-13 · S502 (DeepSeek, session with Filip) · the app has its own clock — and the RTC was in the CH32 all along

> Written by DeepSeek, S502. Builds on S501's finding (`ts 6` after a power cycle). The short version: the
> device always had a battery-kept RTC, in the **CH32 coprocessor** (`bsp/rtc.h`: `get_time`, `set_time`,
> `update_time`); the app simply never asked it, so `time(nullptr)` answered 0 and every advert said 1970.

### Where the time comes from, in order

1. **The coprocessor RTC** (`bsp_rtc_get_time`) — the good case. It is the one clock that survives a power loss,
   and the launcher sets it from its own NTP.
2. **The last known time, stored in NVS** (`mcid/epoch`) — a **floor**, not the time: the device was off, so the
   real time is at least this. The log says FLOOR in so many words rather than presenting it as exact.
3. **A timestamp we received and could authenticate** — an advert's (Ed25519 signature verified) or a channel
   text's (the 2-byte MAC opened it with our key). Adopted only when it beats our clock by more than 60 s, and
   then written back to the CH32 RTC and to NVS. **This is the fix that works with no Wi-Fi and no credentials:**
   the mesh already carries a real time, and it is the same time the replay protection compares against.

Both sources are "true at some moment in the past", so at boot the **later** of the two wins (the RTC on a tie).
The rules live in `core/clock.h`/`clock.cpp` as pure functions, so the device and the host run the same decisions.

### Changed

- `core/clock.*` — **new**, host-tested: the plausibility window (2024…2100), `chooseStart`, `shouldAdopt`,
  `shouldWriteBack` (300 s), `shouldPersist` (3600 s, NVS wear), and `advertFloor` (the S501 floor logic, moved
  out of `mc_link.cpp` so it is testable).
- `main/time_source.*` — **new**, device glue: `begin()` (RTC → stored floor → none, TZ `CET-1CEST,M3.5.0,…`),
  `noteSeen()` (radio task, publishes under a spinlock), `service()` (main task: apply, correct the RTC, store
  the floor), `hhmm()` for the status bar.
- `main/mc_link.cpp` — adverts are now handled **before** the channel table is consulted (they are not
  channel-scoped, and S501 dropped them entirely when no channel was provisioned, which also threw away the only
  authenticated time source); `noteSeen()` on a verified advert and on a text whose MAC opened; the advert
  timestamp uses `om::clock::advertFloor`.
- `main/main.cpp` — `time_source::begin()` after `bsp_device_initialize` (the RTC needs the CH32 up) and after
  NVS; `service()` + the real `HH:MM` in the 2 s status refresh. The status bar no longer hardcodes `--:--`.

### The build taught one thing the reading did not

`tm::clock` **cannot exist**: the app's core namespace is `tm`, libc has `struct tm`, and `clock` is libc's
`clock()`. Any translation unit that includes `<time.h>` and a `namespace tm` fails with *"redeclared as
different kind of entity"* — which is exactly the TU a clock lives in. The clock rules are therefore
`om::clock` (`core/clock.h` says why). Renaming the whole core namespace is a later cleanup, not this slice.

### Evidence

- Host: the new suite is **53 checks, 0 failures**, and it is **armed** — three deliberate mutations of
  `clock.cpp` (slack `>` → `>=`, `floor + 1` → `floor`, boot choice always the RTC) each turn it red at a
  named line. The other two suites stay green: **194/0** (core) and **94/0** (MeshCore wire/channels/identity).
- Device: `make build` **exit 0**, no warnings. **1,343,840 B** (+4,880 on v22's 1,338,960), 36 % of the app
  partition free. **Installed as AppFS v23** over BadgeLink (the launcher lists it as `OM Lite | 23 | 1312K`),
  replacing v21.
- Concurrency: the CH32 driver takes an internal semaphore per transaction
  (`tanmatsu_coprocessor.c:167-177`), so the main task's RTC write and the radio task's battery poll cannot
  interleave; the pending-time slot between the two tasks is published under a spinlock.

### Proven on the glass (v23, installed 22:26 CET, console captured)

Three boots, all with Filip at the device:

| what | the line | verdict |
|---|---|---|
| boot 1 (launcher start) | `I (3112) tm/clock: clock: from the coprocessor RTC: 2026-11-13 21:26:28Z (22:26 local)` | the RTC held a real time; **the TZ string works** (21:26Z → 22:26 local) |
| advert 1 | `ADVERT TX "TNMT (SI)" 112 B ts 1794605190 -> ok` (21:26:30Z) | 2 s after the boot read: our own clock, and MeshCore's replay check now passes |
| **power cycle** → boot 2 | `I (3108) tm/clock: clock: from the coprocessor RTC: 2026-11-13 21:28:08Z (22:28 local)` | **the RTC survives a power loss** — the one thing that was a measurement, not a reading |
| forced advert (`a`) | `ts 1794605358` = **21:29:18Z**, on a console line stamped 21:29:20Z | the app's clock is within ~2 s of true time |
| **the L2** (Filip, on its screen) | `22:29:21 adv TNMT (SI) flood` | **the node that used to reject us now accepts our advert** |

Details worth keeping:

- **The app never had to write the RTC** in these runs: every boot read it (source `rtc`), so the launcher's NTP is
  what set it. The write-back path (adopt a peer's time → correct the CH32) is built and host-tested but has not
  fired on the glass yet, because no peer was more than 60 s ahead.
- **Correction of my own earlier reading (same session):** from the port-open timestamps I first estimated the RTC
  ~20 s ahead of true time. The stamped capture shows ~2 s. The estimator was wrong, not the clock: the USB CDC
  only delivers the boot lines once a reader attaches, so "port opened" is later than the boot it prints.
- The 3 h schedule was not waited for: the `a` key forced the second advert 68 s after the first, which also
  re-proves the test hook still works.

### Still not checked

- Nothing on the L2 was touched this round, and its suite was not re-run.
- The write-back path (peer time → CH32 RTC) has no glass evidence yet; it needs a device whose clock is
  > 60 s behind a peer's.
- RSSI still reports 0 dBm (from 3a, unchanged).

### Next: step 4 (Chat + Nodes)

Chat with a composer (retires the `t` test hook) and a Nodes list built from the adverts we now hear — the
advert parse already produces name + key prefix + timestamp; the list needs the contact store behind it.

## 2026-11-13 · S502 (continued) · step 4: the Nodes and Chat pages, and two rules the device taught me

> Written by DeepSeek, S502, after the clock slice. Same session, second slice: the app stopped being a Launcher
> with a radio behind it and became a mesh handheld you can read and type on.

### What was built

- **`core/mc_store.*` — the store the pages read** (host-tested, **99 checks**). Contacts keyed by public key
  (a repeat advert REFRESHES the row, it never adds a second), a 64-message ring that drops the OLDEST, per
  channel filtering, a channel summary for the list, and ages. Unread is counted by **sequence number**: my first
  attempt counted by message count and my own test caught it — with a full ring a new message does not change the
  count, so unread stayed at zero exactly when it mattered. That is the same bug class the L2 shipped once (a
  count of 4294967295), so it is pinned now.
- **`main/chrome.*`** — status bar, page head, hint bar and the five symbol sigils moved out of
  `launcher_page.cpp` into one place, so three pages cannot drift apart. The numbers are the approved ones,
  moved, not changed.
- **`main/nodes_page.*`** — the L2's two-pane shape at 800x480: type dot (client blue / repeater green / room
  amber), hops, age, SNR, a gold focus ring, a scroll thumb, and a detail pane with type, path, SNR/RSSI, last
  heard, the key prefix and the path. No DM/Trace/Favourite buttons yet — they belong to the reply-path slice,
  and a button that does nothing would be a lie.
- **`main/chat_page.*`** — channel list with unread badges and a last-line preview; an open channel with its
  history, **our own messages on the right under "you" and everyone else's on the left** (Filip, this session),
  the time and age columns unchanged; a composer that takes the physical keyboard, Enter sends, Backspace
  removes a whole UTF-8 letter, Esc goes up one level.
- **`mc_link.cpp`** — writes contacts and messages into the store (under a recursive mutex, one writer/one
  reader), handles adverts **before** the channel check, and on a successful send registers the frame's own
  payload fingerprint in the seen-ring **and** stores the outgoing line.
- **The router** — Esc is **UP**, not undo (Filip: *"folder up button, layer up, not just back"*). Every module
  page's parent is the Launcher, so Esc goes there in one step; the visited-page history is gone rather than
  disabled. Inside Chat the level belongs to the app: open channel → list → Launcher.
- **Honest placeholders** for Map, RPTR and Settings ("not built yet — step 6") instead of keys that do nothing.

### Two defects his feedback found

1. **My messages were not mine.** The send path never stored the outgoing line; what appeared on screen was the
   mesh echoing our own frame back, i.e. an INCOMING message from our own node name. Every message looked like
   somebody else's. Fixed by storing the send and registering its fingerprint before the echo can return — which
   also removes the duplicate line that would otherwise appear.
2. **Esc retraced pages.** It walked the visit history (Rptr → Map → Chat → Nodes → Launcher), which reads as
   undo. Replaced by the hierarchy rule above.

### Evidence (AppFS v24, then v25; console captured)

```
I (3100) tm/clock: clock: from the coprocessor RTC: 2026-11-13 21:58:25Z (22:58 local)
I (15360) tm/app: chat: queued for channel 0 (1 chars)
I (16130) tm/mc: TX ch0 "Test Channel" 37 B ts 1794607117 -> ok
I (21820) tm/mc: MSG ch0 "Test Channel" from "ERA-3 (SI)": [OM Bot] ... One dot, no context. Noted.
```

Note what is NOT there any more: after `TX` there used to be a `MSG ... from "TNMT (SI)"` (our own echo). It is
gone in v25, which is the duplicate fix working. Host suites: **192 (core) / 53 (clock) / 99 (store) / 94 (meshcore)**; the core count dropped by two because
four Esc steps became two checks. Build exit 0, no
warnings, **1,353,776 B** (AppFS v25).

Filip, on the glass: *"send a '.' out and got a bot response. All seems good."*

### Still open, and named on purpose

- **No wrapping inside a message**: a long text is cut with an ellipsis at the row width. The brief asks for the
  full message on Chat, so this is a known gap, not a decision. Filip's messages are short in practice.
- **Emoji**: the bot's replies carry emoji, and the DejaVu subset `tools/mkfonts.py` generates has no emoji glyphs
  — they will not draw. Not fixed; the text around them reads.
- **Scrolling**: the message window shows the newest 8 and there is no key to scroll back yet (Up/Down inside a
  channel are swallowed on purpose rather than switching pages).
- **No SD history** (step 5), no reply paths, no favourites, no DM/trace — the Nodes actions wait for reply paths.
- The store is RAM only: a restart empties the chat and the node list.

## 2026-11-13 · S502 (continued) · step 5: chat history on the SD card — built, and NOT proven yet

> Written by DeepSeek at the end of S502 (Filip went to bed). This entry is deliberately half a result: the code
> is done and green, the install is not, so nothing here claims the card works.

### What was built

- **The L2's record format, reused, not reinvented.** `firmware/src/history/history_log.h` is plain C++ and
  already host-tested (10-byte header, CRC-32, torn-tail rule, message payload), so the Tanmatsu writes the same
  bytes the L2 writes. `core/mc_history.*` maps our `Message` onto that payload and reads a log back.
- **`main/sd_history.*`** — `/sdcard/om-lite/history/ch-<hash>-<name>.log`, one file per channel. An append opens,
  writes ONE record, flushes and closes, so a power cut costs at most the record being written; a boot reads the
  newest 32 KB (heap, freed again) and starts at the first valid record with the L2's own `findWindowStart`.
- **`mc_link`** appends every message (both directions) and loads each channel's history at boot, marking it read
  so a history you already saw does not return as 9 unread.
- **Host tests: 40 checks** — the round trip (Slovenian letters included), a log longer than the ring keeps the
  NEWEST window, a power cut in the middle of the last record loses only that record, and a corrupted MIDDLE
  record stops the walk there (nothing after a bad record is trusted).

### The bug the first install hid, found in the log

v27 reported `W (5290) tm/hist: history: cannot create /sdcard/om-lite/history`. `mkdir` on FATFS creates ONE
level, the parent `/sdcard/om-lite` did not exist, `begin()` gave up, and the whole feature ran RAM-only while
the chat still looked perfect — the worst kind of failure: the feature is absent and the screen agrees with you.
Fixed in **v28**: the parent is created first, and a failure now logs the path AND the errno.

### State at the end of the session

- **v27 installed**: clock, Nodes, Chat, the own-right/others-left layout, Esc as layer-up, and the unread fix
  (Filip: *"good, works"*). Verified on the glass this session.
- **v28 built, NOT installed**: `make build` exit 0, 1,365,168 B, host **192 / 53 / 99 / 40 / 94**, 0 failures.
  It contains ONLY the mkdir fix, and it is the first thing to install next session.

### Next session, in order

1. **Install v28** (F6 -> BadgeLink -> `badgelink.sh appfs upload om-lite "OM Lite" 28 build/tanmatsu/application.bin`,
   then F6 back to the bug and start the app). Then: `history: ON at /sdcard/om-lite/history` at boot, send a
   message, restart the app, and `history: N message(s) from ch-d7-...log` with the lines back in the channel.
   The card is visible from TestBox at `/media/slofi/<card>`; `ch-d7-Test_Channel.log` should exist.
2. If history works: a **Meta record** for the seen point (the format already has it) so unread after a restart
   matches what was read, instead of "everything loaded counts as read".
3. Then the UI gaps that are known and named: message **wrapping** (long texts are ellipsised), **scrolling**
   inside a channel (Up/Down are swallowed today), **emoji glyphs** (the generated DejaVu subset has none, and the
   bot's replies carry emoji).
4. **Step 6**: Map from the SD pack (`tiles.pack`/`tiles.idx`, the same pack the L2 uses), then RPTR, then
   Settings (radio region/preset, node name, time sources, path hash). One page at a time, previews first.
5. Reply paths, then DM/Trace/Favourite on the Nodes page (the buttons are deliberately not drawn until they work).
6. Still open from step 3a: **RSSI reads 0 dBm**, and the desk-range SNRs look low (-9 to -15 dB).

## 2026-11-13 · S501 (DeepSeek, session with Filip) · addendum: the node is named TNMT (SI), it announces itself, and its clock is a hole

> Written by DeepSeek, S501, after the rename Filip asked for. Three things: a rename with one home, a real
> behaviour (announce on start), and a defect the auto-advert exposed that would otherwise have hidden for months.

### Renamed: EDCT -> "TNMT (SI)"

`EDCT` was a placeholder from S500. The name now lives in **one place**, `kNodeName` in `mc_link.cpp`, and both
the advert and the message sender read it — they used to be two literals that could drift apart. `queueText()` no
longer takes a sender and `queueAdvert()` no longer takes a name, so a message and an advert cannot announce two
different identities. On the glass: `ADVERT TX "TNMT (SI)" 112 B` (five bytes longer than `EDCT`), and the L2
showed the same node renamed (same public key `42a1607...`, so it updated the contact rather than adding one).

The host test still signs the vector name `EDCT` **on purpose**: those bytes are Python's signature over exactly
that name, and a fixed vector must not change with the product's naming. The comment in `testIdentityAdvert()` says so.

### It announces itself now

    I (6574) tm/mc: ADVERT TX "TNMT (SI)" 112 B ts 6 -> ok
    I (6574) tm/mc: radio task stack: 9388 B free at the low-water mark

One advert as soon as the radio is up, then every 3 hours (`kAdvertIntervalUs`); the `a` key only forces one
early. No key press was needed for that line — it is the difference between a demo and a node other people can
find.

### The defect the line above exposed: `ts 6`

The timestamp is **six seconds after 1970**. The power cycle emptied the P4's RTC, and MeshCore **rejects an
advert whose timestamp is not newer than the one it stored for that node** (replay protection,
`BaseChatMesh.cpp:133-137`). So a node that already knew us ignores our adverts until our clock passes its stored
value, while **channel texts keep working** — a silent, one-sided failure.

What is in so far (v22, built, **not yet verified on the device**): `nextAdvertTimestamp()` floors the timestamp
by the highest value we have ever sent, kept in NVS `mcid/advts`, so a reboot can never send an *older* advert
than the last one. That fixes monotonicity from now on; it **cannot repair a clock that is already wrong**, and
right now our RTC is at 1970 while the L2 has us at 1794603776.

**Workaround until the app has a clock:** exit to the launcher once (Esc) and let it run its NTP, then start the
app — the RTC is inherited. **The real fix is a time source of our own** (SNTP over the C6's Wi-Fi, or a
deliberate RTC-sync handshake with the launcher): a slice of its own, not a patch.

### What could NOT be checked

- The floor is **built, not proven on the glass** (it needs a device round).
- Whether the L2 now ignores our adverts, as predicted: it was shown the renamed advert **before** the power
  cycle emptied the clock, so the prediction is from the source, not from a rejected advert.
- Nothing else changed in the MeshCore path this round.

## 2026-11-13 · S501 (DeepSeek, session with Filip) · step 3b slice 3: the node has an identity — and the advert found a real defect

> **Written by DeepSeek, S501, from the device's console and Filip's eyes.** The Tanmatsu now signs its own adverts:
> it created a keypair, `EDCT` appeared in the L2's node list, and its own advert came back and verified. Getting
> there cost one panic and one power cycle, and both taught something worth keeping.

### What it is

| file | what |
|---|---|
| `core/mc_identity.h/.cpp` | keypair from a 32-byte seed, Ed25519 sign/verify, and the advert: payload = `pub(32) + ts(4 LE) + sig(64) + app_data`, signed over `pub || ts || app_data`, frame = `0x11` + `path_len 0x40` (flood, 2-byte hashes). Sources: `Mesh.cpp:404-434`, `Mesh.cpp:262-276`, `AdvertDataHelpers.cpp:4-29`. |
| `core/ed25519/` | **orlp/ed25519, vendored** (zlib licence) from `firmware/lib/meshcore_ed25519` — the same code the L2 signs with, so signatures match by construction. mbedTLS's PSA has no Ed25519 in either of our builds (measured: `PSA_ERROR_NOT_SUPPORTED`). |
| `main/mc_link.*` | the seed lives in NVS (`mcid/seed`), is created once from `esp_fill_random()`, and an advert is queued with `queueAdvert()`; adverts heard are verified and logged. |

### Proven

    I (5414)  tm/mc: identity: 42a160702470b52b  public 42a160702470b52bd751c0072e61c933f3cdf116f88200e2452adb31fd5a10f5
    I (56034) tm/mc: ADVERT TX "EDCT" 107 B ts 1794603429 -> ok
    I (56034) tm/mc: radio task stack: 9304 B free at the low-water mark
    I (59904) tm/mc: ADVERT: our own advert came back

- **`EDCT` is in the L2's node list** (Filip, on its screen). A signature that the other end rejects would have
  produced either no node or a node without a name; it produced the named node.
- **The same public key after an app reinstall** (`42a1607…` before v19 and after) — the seed really is stored and
  read back, so the identity is the node's, not the boot's.
- **Our own advert came back and verified** — the flood was repeated by the mesh, and `advertParse` checked the
  Ed25519 signature in the returning frame. That line only prints on a valid signature.
- Host tests: **101 checks / 0 failures**, including the advert bytes being **identical to Python's** `cryptography`
  (OpenSSL Ed25519) for the same seed, plus three tamper cases (name, signature, timestamp) all refused.

### The defect it found, and the fix

The first advert **panicked the device**: `Guru Meditation Error: Core 0 panic'ed (Stack protection fault)`. Two
mistakes of mine, one visible only because the flood comes back:

1. the advert was built and **signed on the main task**, from the key handler, while the radio task drove the same
   link — two writers to one radio;
2. the returning advert was then **verified on the radio task's 4 KB stack**, and Ed25519 verification needs about
   3 KB — measured now: with a 12 KB stack the low-water mark is **9,304 B free**, i.e. ~3 KB used.

Fix, structural rather than a bigger number alone: **one owner of the radio**. Key handlers only queue a request
(`queueText`/`queueAdvert`), and the radio task (`serviceRequests()`) builds, signs and transmits. The task's stack
is 12 KB and it **logs its own low-water mark after the work**, so the figure is measured, not assumed.

### Recovery, now written down (RECOVERY.md § 5)

A panic skips our exit path, and **our exit path is what power-cycles the C6**. The next boot therefore hung ~1.6 s
in, with the old launcher frame still on the glass and Esc dead (the app had not reached its input loop). Only a
**full power switch cycle** clears it — a P4 reset does not, because the C6 keeps its state. After the cycle the
app booted with identity and channels intact. Two independent symptoms (panic, then the hang) can look like one
fault; the console told them apart.

### Cost

`application.bin` 1,264,768 -> **1,338,960 B** (+74 KiB, almost all of it Ed25519); 36 % of the app partition free.
AppFS **v19** installed and running.

### What could NOT be checked

- Whether a **third-party node** (not our own L2) lists `EDCT` — only the L2 was looked at.
- **Reply paths**: our advert is a flood with no stored return path, so a direct reply to us is not possible yet;
  group messages work because they are floods.
- The advert carries **no position** (the Tanmatsu has no GPS) — `app_data` is type+name only.
- **RSSI still reads 0 dBm**; SNR looks real.
- Anything about advert **timing policy** (MeshCore re-adverts on a schedule; here it is one press).

### Next

Step 4 makes this usable: Chat (a composer, which retires the `t` hook) and Nodes (from the adverts we now hear),
then the advert's automatic schedule and reply paths.

## 2026-11-13 · S501 (DeepSeek, session with Filip) · step 3b slice 2: the Tanmatsu is ON the mesh — it sent a channel text and decoded one

> **Written by DeepSeek, S501, from the device's own console, with Filip pressing the keys.** A MeshCore channel
> text left this device on the SLO settings, came back decoded, and the L2's own message was decoded here. This
> closes the "can it talk on the mesh" question; what is still missing is the UI (step 4) and the node identity.

### The proof, as the device printed it (AppFS v17, one console capture)

    I (5394)  tm/mc: channels: 1 loaded
    I (5394)  tm/mc:   ch0 "Test Channel" hash 0xD7
    [key] kbd ascii=116 't'                                     <- Filip's keypress
    I (45834) tm/mc: TX ch0 "Test Channel" 53 B ts 1794602323 -> ok
    I (47164) tm/mc: MSG ch0 "Test Channel" from "EDCT": hello from the tanmatsu  (0 dBm, snr -18 dB, 55 B)
    I (66734) tm/mc: MSG ch0 "Test Channel" from "ERA-3 (SI)": Hello from ERA-2!   (0 dBm, snr -12 dB, 53 B)
    I (68954) tm/mc: rx 55 B on ch0: duplicate, already shown

What each line is worth:

- **`hash 0xD7` is the same byte the host test derives from Filip's key**, so the key that reached NVS through
  BadgeLink is byte-correct and the device derives the channel hash identically to the host — two independent
  paths to the same number.
- **Our own text came back as 55 B instead of the 53 we sent.** Two extra bytes = a **2-byte path hash appended by
  the mesh**, which means the flood was repeated and the frame came back through a real route: this is the first
  test of the path parsing (`path_len` high bits, count > 0) on live traffic, and it decoded.
- **The L2's message decoded here**: `from "ERA-3 (SI)": Hello from ERA-2!` — the sender name and text come out of
  MeshCore's `"name: text"` plaintext, so the whole channel cipher (hash, MAC, AES-128-ECB) matches the L2's.
- **`duplicate, already shown`** is the seen-ring doing its job on the second copy of the same flood.
- **The timestamp is real**: 1794602323 is 2026-11-13. The app shows `--:--` (no clock of its own), but the RTC the
  launcher set is being read correctly, so messages carry a sane time to the other node.
- **And the L2 SHOWED it** — Filip, looking at the L2's "Test Channel" chat a moment later: *"sure it did! :)"*. So the
  other end is confirmed by the only witness for that screen, not inferred from our own echo. Both directions are
  now proven, not just "the mesh carried our bytes".

### What this slice added

| file | what |
|---|---|
| `core/mc_channels.h/.cpp` | the channel table (add / de-duplicate by key / find by key / find by hash) and **routing**: a received group text is tried against every channel whose 1-byte hash matches and the 2-byte MAC decides, which is MeshCore's own rule and what makes a 1-byte hash safe. |
| `main/mc_link.h/.cpp` | the NVS table (namespace `mcch`: `cnt`, `n<i>`, `k<i>` — the L2's own naming), the receive path (decode → route → log), the send path, an 8-slot payload-hash seen-ring, and a status string. |
| `main/main.cpp` | OM Lite starts the channel table with the radio; the receive task decodes instead of logging raw bytes; **the 3a beacon is gone** (a non-MeshCore 11-byte packet is only noise on the channel); a temporary test hook sends a message on `t` until the Chat page exists (step 4); the Launcher's OWNS row now says how many channels it can read. |
| `test/host/test_mc.cpp` | 31 more checks (49 → **80**): table add/dupe/find, routing to the right channel of two, refusal of another channel's packet, and a full table refusing rather than overflowing. |

### Provisioning: the channel key never touched the tree

The key was written straight into NVS over BadgeLink, from a 16-byte temp file:

    nvs write mcch cnt u8 1
    nvs write mcch n0 str "Test Channel"
    nvs write --file mcch k0 blob /tmp/mcch-k0.bin      # blob = raw bytes, so not even in argv

That is deliberate: **no channel key in the repo, the notes or the image.** The device self-test still uses the
public `00..0f` vector key. The consequence to remember: a re-flashed NVS loses the channel, and this is the
command that puts it back.

### Build + install

`make build` exit 0 -> `application.bin` 0x134C80 = **1,264,768 B** (+3.6 KiB over slice 1; 40 % of the app
partition free). Host suites: **194/0** (core) and **80/0** (codec + channels; 42/0 + explicit skip without the
real key). AppFS **v17** installed and running.

### What could NOT be checked

- ~~Whether the L2 displayed our message~~ — **confirmed by Filip** (see above). What remains unverified is anything
  about the other nodes: whether a repeater forwarded us deliberately or we simply heard our own flood return, and
  how the message looked among the L2's other traffic.
- **RSSI is still 0 dBm** (the known defect from 3a): SNR looks real (-18 / -12 dB at desk range), RSSI does not.
- **No identity, no advert**: `EDCT` is the name inside the text, not a signed node identity, so other nodes do not
  see us in their contact list. That is the next piece after the UI.
- Nothing was checked for interference with the L2's own functions (it kept working, but no deliberate test).
- Only one channel was provisioned; multi-channel routing is only host-tested, not on-air tested.

### Next slice (3b, part 3)

1. **Advert + identity**: an Ed25519 keypair on the device, a signed `PAYLOAD_TYPE_ADVERT` with the name `EDCT`,
   so the node appears in other nodes' contact lists (MeshCore: pubkey + timestamp + signature, `Mesh.cpp:250-...`).
2. **Path-hash storage for replies** (the advert reply path) once adverts exist.
3. Then step 4 turns this into screens: Chat with a composer (and the `t` hook disappears), Nodes from the contacts.

## 2026-11-13 · S501 (DeepSeek, session with Filip) · step 3b slice 1: the MeshCore wire codec, three implementations agreeing

> **Written by DeepSeek, S501, with Filip present.** This is the first slice of 3b: the packet frame and the
> channel-text encryption, proven by three independent runs. **Nothing has been transmitted yet** and no channel
> is configured on the device — that is slice 2.

### What it is

| file | what |
|---|---|
| `core/mc_wire.h/.cpp` | the packet frame (header, `path_len` with the 2-bit hash size, transport codes, path, payload) and the group-text payload (channel hash + 2-byte MAC + AES-128-ECB ciphertext over `ts, type, "name: text"`). Portable C++, no IDF, no allocation; every constant carries the source line it came from in `firmware/lib/meshcore` (v1.18.0). |
| `core/mc_crypto.h`, `core/mc_crypto_psa.cpp` | the four primitives behind one interface, implemented with **PSA Crypto** so the host test and the device compile the SAME file. |
| `test/host/test_mc.cpp` | 49 checks: the fixed vector, six refusals, and an optional real-key round trip from `MC_TEST_CHANNEL_KEY`. |

### Two findings that only appeared by building

1. **The ESP-IDF's mbedTLS is 4.x; the host's is 3.6.5.** A classic-API backend (`mbedtls/cipher.h`) compiled on the
   host and failed on the device with `mbedtls/cipher.h: No such file` — IDF 6.0.2 defines
   `MBEDTLS_MAJOR_VERSION=4`, where that header no longer exists. Fix: the primitives use **PSA Crypto**, which
   both versions provide (`psa/crypto.h`, `PSA_ALG_ECB_NO_PADDING`), so there is still ONE implementation rather
   than a host one and a device one that can agree with each other and disagree with the mesh.
2. **`mbedtls_cipher_set_padding_mode(..., MBEDTLS_PADDING_NONE)` returns `-0x6100` for AES-ECB** (measured, mbedTLS
   3.6.5): ECB's cipher info has no padding field, so the call itself is invalid. Guarding on it made the block
   cipher refuse every block, which surfaced three layers up as only "buildGroupText returned 0". The AES was
   right all along (FIPS-197 C.1: `69c4e0d86a7b0430d8cdb78070b4c55a`). Both are now written next to the code.

Also corrected in my own code, from MeshCore's `Packet::readFrom`: a frame that ends exactly at the path is
**invalid** (`if (i >= len) return false`), not an empty packet. My first decoder accepted it; the test caught it.

### Two MeshCore details worth keeping

- MeshCore keys the HMAC with the **32-byte secret buffer** (`16-byte key + 16 zeros`), not with the 16-byte key.
  For HMAC-SHA256 both pad to the same 64-byte block, so the MAC is identical either way — but only because
  `setChannel()` zeroes the upper half (`BaseChatMesh.cpp:938`). With a 256-bit channel it hashes all 32 bytes.
- 2-byte path hashes are `path_len = 0x40` with a count of 0 (the SLO mesh's setting, confirmed by Filip from the
  L2's own Settings -> Radio, and by his "2B hash, yes").

### Proven — three implementations, same bytes

Vector: key `00 01 .. 0f`, ts 1750000000, sender `EDCT`, text "hello from the tanmatsu", 2-byte path hashes.

    python    (hashlib + hmac + cryptography)   1540be2b1d0b22ff389168fd798902c8cb...b8f52   53 B
    host C++  (mbedTLS 3.6.5 / PSA)             1540be2b1d0b22ff389168fd798902c8cb...b8f52   53 B
    P4        (mbedTLS 4.x / PSA, AppFS v16)    1540be2b1d0b22ff389168fd798902c8cb...b8f52   53 B

The device's own boot console (compared programmatically against the oracle, not by eye):

    I (1384) tm/app: mc codec: hash 0xBE frame 53 B 1540be2b1d0b22ff38...2f4cb6bacb8f52
    I (1384) tm/app: mc codec: open ok, hash_size 2, from "EDCT", ts 1750000000, text "hello from the tanmatsu"

Host suites: **194/0** (core) and **49/0** (codec, with the real key; 42/0 with an explicit `skip` without it).

### The real channel, without writing it down

`MC_TEST_CHANNEL_KEY` carries Filip's "Test Channel" key at test time only: the codec derives the channel hash from
it and round-trips a message. **Neither the key nor the derived hash is written into the repo, the notes or the
image** — the on-device self-test uses the public vector key. Filip's channel list for reference: CH0 Public,
CH1 #Slovenija, **CH2 Test Channel** (the test channel), CH3 #Slovenia, CH4 #Test.

### Cost

`application.bin` 1,174,688 -> **1,261,152 B** (+84 KiB for the codec + PSA glue); 40 % of the app partition still
free, ~1231 KiB of the launcher's 3200 KiB AppFS ceiling. **AppFS v16 installed and running.**

### What could NOT be checked

- **Nothing has been transmitted.** The radio still sends step 3a's 11-byte beacon; the codec has not touched
  `radio_link`, and no MeshCore packet from this device has crossed the air.
- **No channel is configured on the device** (no NVS channels, no key on the device): slice 1 is the codec only.
- Whether the L2 accepts our flood, and whether the L2's own messages decode on the Tanmatsu — that needs a real
  on-air test with Filip sending on "Test Channel".
- The advert/identity path (`EDCT` as a *node*: Ed25519 keypair + advert signing) is not built. `EDCT` is so far
  only the name inside the message text.

### Next slice (3b, part 2)

1. Channels in NVS on the device (name, 16-byte key, derived hash) — the L2's `src/channels.cpp` is the model.
2. `main/mc_link.*`: on RX try `decode` + `openGroupText` per channel; on TX put `buildGroupText` into the radio
   instead of the beacon.
3. On the glass with Filip: send from the Tanmatsu on "Test Channel", see it on the L2, and decode the L2's reply.

## 2026-11-13 · S501 (DeepSeek, session with Filip) · the exit: it works, and S500's top entry was wrong

> **Written by DeepSeek, S501, after the device ran it.** Filip said "lets go on"; the S500 save pointed at step 3b,
> but the entry written *after* that save said the exit was still broken, so he chose the exit first. This entry
> corrects the record and closes it.

### Verdict

**Esc works. The app does not come back, and the reboot is the platform's own way of switching images.** Three
claims in the S500 entry below are wrong; each was checked where it lives, and the behaviour was then measured on
the device three times.

### The three corrections (S500 → what is true)

| S500 said | What is true, and where it was read |
|---|---|
| "BUG: the app comes back by itself after the launcher boots" | **It does not.** 75 s of launcher console after a reset: zero app starts. In the very capture S500 quoted, the **second** Esc behaved identically. The one app boot there has **no ROM banner** (`load:` lines only), i.e. the capture lost its first bytes at a USB re-enumeration, so it cannot be attributed to the launcher. |
| "there is no Tanmatsu implementation of `bsp_device_restart_to_launcher()`, only a weak stub" | **It is implemented** in `badge-bsp/stub/badge_bsp_device.c:50`: it zeroes `mem->custom[0..8)` (the boot-select magic) and calls `esp_restart()`. Only the RTC-register variant for other chips is an ifdef. |
| "the launcher sets `app_fav/autostart` whenever an app is started from its Apps menu" | **It does not.** `app_autostart_set()` has exactly one caller: the explicit F3 + FUNCTION toggle (`main/menu/apps.c:573-577`). `execute_app()` does `appfsBootSelect()` + `esp_restart()` and touches no NVS. On this unit `badgelink.sh nvs list app_fav` shows the namespace **empty**. |

### The mechanism, read from the sources

- The launcher starts our app with `appfsBootSelect(fd, path)` + `esp_restart()` (`apps.c:170-177`). There is no
  "return to the caller"; a reboot is how an image is swapped, so Esc → reboot → launcher is correct, not a defect.
- The bootloader decides on `bootsel.magic` **only** (`appfs bootloader_start.c:173-177`) and writes `valid=false`
  after loading; `valid` is a crash marker and never read as a switch.
- The launcher's autostart fires only when `esp_reset_reason()` is `ESP_RST_POWERON` or `ESP_RST_JTAG`
  (`main.c:587`). Our exit is `SW_CPU_RESET`, so it cannot restart us.

### Changed (v13 → v15, each installed and confirmed on the glass)

1. **The exit says what it is doing.** Esc draws "Back to the menu / The device restarts to switch apps." on the L2
   palette (`kBg`/`kInk`/`kDim`, parsed from `firmware/src/ui/theme.h`, not copied) **40 ms after the keypress**
   (measured 40622 → 40662), does the C6 power cycle under the words, holds 1.5 s more, hands the panel over as the
   proven black frame, then resets. Whole exit ≈ 2.6 s, every part of it explained on screen.
   - v13 drew the message *after* the C6 cycle and held it 600 ms. **Filip did not see it**: "didn't notice the
     'Back to menu' screen.. There was a pause with 'coloured' screen, than reboot". The frozen app page was the
     950 ms cycle standing in front. That is the ordering fix, and it is the whole reason this is not just a string.
2. **The autostart erase is scoped to us.** It used to `nvs_erase_key` unconditionally. Now it reads the slug, erases
   only when it is `om-lite`, logs `not set` / `unreadable (<err>)` / `not us, left alone`, and is **not** marked done
   when the namespace cannot be opened, so the exit retries. A setting the user made in the stock launcher is his.
3. The false rationale comments in `main.cpp` were replaced by the evidence above, so the next reader does not have
   to re-derive them.

### Proven on the device (v15, console captured by USB serial, `/tmp/tm-s501-exit3.log`)

    I (1372) tm/app: launcher autostart: not set
    I (86382) tm/app: leaving to the stock launcher
    I (86382) tm/app: cleared the AppFS boot-select (256 bytes of RTC-retained custom)
    I (86422) tm/app: back-to-the-menu frame drawn (panel blit ok)      <- 40 ms after Esc
    I (87372) tm/app: C6 power-cycled through the CH32
    rst:0xc (SW_CPU_RESET),boot:0x1c (SPI_FAST_FLASH_BOOT)
    I (1418) app_init: Project name: launcher
    I (9145) main: NTP time sync succesful ...                          <- launcher up, no app boot; recorder ran on

Filip's own read of the sequence: **"Same as before"** (message → launcher), after v14's "black screen with the
text, than colour screen (cca blue-ish), than restart".

### The blue screen, so nobody files it again

The "colour screen, cca blue-ish" that appears **after** our message is the **launcher's own boot art**. On the
Tanmatsu `startup_dialog()` calls `synthwave()` (`main/menu/message_dialog.c:423-430`), which paints
`pax_background(fb, 0xFF552075)`, a purple sunset with a grid (`main/synthwave.c:9`), while it initialises. Between
our reset and the launcher's first frame, the only thing we put on the glass is the black handover frame.

### Build + install

`make build` exit 0 → `application.bin` 0x11E8A0 = **1,174,688 B** (44 % of the app partition free; ~1147 KiB of
the launcher's 3200 KiB AppFS ceiling). Host suite `sh test/host/run.sh`: **194 checks, 0 failures**.
Installed with `badgelink.sh appfs upload om-lite "OM Lite" <13|14|15> build/tanmatsu/application.bin` and started
with `badgelink.sh start om-lite` (BadgeLink mode needs **F6** on the launcher: one USB PHY is shared, so no console
exists while it is in that mode).

### What could NOT be checked

- **Filip pressed Esc** in all three runs; a key cannot be injected. The runs are his.
- The 950 ms C6 power cycle on exit was **not** removed and re-tested. It is load-bearing: without it the launcher
  crashed in `xQueueGenericSend` right after its display init (captured today 17:31, `/tmp/exit-transition.log`).
  Its 600 ms settle is inherited from the reference; shortening it is an unproven idea, not a saving.
- A **power-on** with `app_fav/autostart` pointing at us was not exercised (read, not run: this unit's namespace is
  empty). That is the only path by which autostart could bring us back.
- Battery/heap over a long run, SD throughput, and RSSI (still 0 dBm — step 3b).

### Next step

**Step 3b — MeshCore**, unchanged (`NEXT-FOR-DS.md`): it needs Filip's "Test Channel" channel key, the node name
`EDCT`, path-hash 2B confirmed, and the AppFS budget measured for `arduino-esp32` + MeshCore.

## 2026-11-13 · S500 (DeepSeek) · exit on Esc: the reset is clean, but the app comes BACK (open, next session)

> **Written by DeepSeek at the end of S500, from a console capture, not from reasoning.** One Esc press, captured
> by USB serial. This is the start of the launcher bug sweep Filip asked for; it is NOT finished.

Filip's report: *"I can't normally exit from our menu to the default one — it restarts the Tanmatsu every time."*
The capture says the reset is **not** the fault:

    I (32982) tm/app: leaving to the stock launcher
    I (32982) tm/app: cleared the AppFS boot-select (256 bytes of RTC-retained custom)
    I (33932) tm/app: C6 power-cycled through the CH32
    rst:0xc (SW_CPU_RESET)                     <- clean software reset, no assert, no panic
    I (1418) app_init: Project name: launcher  <- the launcher really boots
    I (1419) app_init: App version:  v0.3.1
    ...
    I (1356) app_init: Project name: application   <- ~1.3 s later: the launcher starts OUR APP AGAIN

**Findings, in priority order:**

1. **BUG (the one Filip feels): the app comes back by itself after the launcher boots**, so the user never gets to
   stay in the default launcher. This violates his "always the menu" rule (S499) even though our exit already
   clears the AppFS boot-select, calls `clearLauncherAutostart()` and clears the RTC block. The launcher is
   getting the signal from somewhere else — candidates: the `app_fav` key expects a DELETE not an empty write,
   or `badgelink start` leaves its own "start this app now" marker that we never clear. **First check after an
   exit: `badgelink.sh nvs list app_fav`.** Acceptance test: Esc → launcher → stays put for 10 s.
2. **950 ms of the pause is ours**, measured: `leaving` at 32982 → the C6 power cycle at 33932, before the reset
   even begins. That cycle was added in S499 against a launcher `xQueueGenericSend` assert on a stale C6; it must
   be re-tested (remove it, press Esc, watch for the assert) rather than kept on faith.
3. **Doc drift, confirmed:** `PROGRESS.md` § S499 "bug 4" says the exit was moved to the BSP's
   `bsp_device_restart_to_launcher()`. There is **no Tanmatsu implementation** of it — only a weak stub
   (`badge-bsp/stub/badge_bsp_device.c:50`) — and `main.cpp` still calls `esp_restart()` at two sites
   (`main.cpp:138`, `main.cpp:361`). So "fixed by using the vendor helper" was a rename at best. Correct the claim.
4. **Not yet swept** (next session): both restart sites, `Stop`'s memory claim (the radio task's stack is outside
   the core allocator's count — already noted in the source as not honest), the inner Launcher's focus/key
   edge cases, and what Start/Stop does while the radio is in `FAIL`.

**Do not** change the exit path without a capture: S499 took three wrong guesses at this same exit before the
console settled it, and the two failure modes (clean reset vs launcher crash) look identical from the outside.

## 2026-11-13 · S500 (DeepSeek) · step 3a: the SX1262 is up, TX and RX both proven on the device

> **Written by DeepSeek, 2026-11-13, session with Filip present.** Both directions of the radio path now work
> from our AppFS app: four/eleven beacons transmitted (each ACKed by the C6) and a real MeshCore packet
> received from the L2 Pro with per-packet RF stats. Two defects were found on the glass and fixed in the same
> session; one number is still wrong and is called out below rather than quoted as a measurement.

### What step 3a added

| file | what it is |
|---|---|
| `core/radio_plan.h/.cpp` | **the numbers, in the units the component speaks**, plus the 11-byte beacon format. Portable C++ (no IDF) so the host test owns them: `kSloPlan` = 869.618 MHz / 62 / SF8 / CR8 / sync 0x12 / preamble 32 / 22 dBm, `radio_plan_valid()`, `radio_ldro_recommended()`, `radio_beacon_build/parse()`. |
| `main/radio_link.h/.cpp` | the only file that touches the driver (PLAN §2 rule 1): `lora_init_remote` once per boot, config push, RX, `send()` (which re-enters RX afterwards), `poll()` with RSSI/SNR conversion, live `statusText()`. |
| `main/main.cpp` | OM Lite's `start()` now takes the Radio lease and brings the radio up; a task drains RX and beacons every 10 s with the real battery millivolts; the status bar shows `radio 869.618 · RX · tx/rx · N dBm` and the OWNS row names the resources it really owns. |
| `test/host/` | **194 checks, 0 failures** (was 162). The new ones assert the SLO plan field by field and are **mutant-armed**: setting the preamble back to 8 turns the suite red at `test_core.cpp:413`. |
| `main/CMakeLists.txt`, `core/CMakeLists.txt`, `main/idf_component.yml` | `radio_link.cpp`, `radio_plan.cpp` as its own translation unit, `nicolaielectronics/tanmatsu-lora: "=0.6.0"`. |

`make build` → **exit 0**, `application.bin` 1,173,120 B (0x11E480, 44% of the partition free), ~1146 KiB against
the launcher's 3200 KiB AppFS ceiling. **Only +21 KiB over v9**: the LoRa component is light. The MeshCore
weight for step 3b is still unmeasured.

### Two of my own values were wrong, and the sources caught them before the air did

1. **Preamble: 8 → 32.** I took 8 from the L2's bring-up probe (`firmware/src/radio.cpp:47`). MeshCore's own
   rule is `preambleLengthForSF(sf) { return sf <= 8 ? 32 : 16; }`
   (`firmware/lib/meshcore/helpers/radiolib/RadioLibWrappers.h:56`), and MeshCore is what the mesh runs.
2. **Bandwidth is an INTEGER kHz switch.** `tanmatsu-lora` 0.6.0 `lora.c:322` maps 62 → the chip's
   62.5 kHz step and **anything unmapped silently becomes 125 kHz**. Writing 62500 or 62.5 would have moved us
   to another channel width with no error at all.

Both are now host-test facts, not comments. (CR is 8, i.e. 4/8 — not RadioLib's usual default — and the sync
word is MeshCore's 0x12; both read from the same sources.)

### Defect 1 (found on glass, v10, FIXED in v11): a C6 power cycle is not enough

v10 configured the radio and every custom RPC timed out:

    I (4001) tm/app: C6 power-cycled through the CH32
    E (4001) Tanmatsu coprocessor: tanmatsu_coprocessor_get_interrupt(309): Communication fault
    W (9511) rpc_core: Timeout waiting for Resp for [0x184](Req_CustomRpc)
    E (14511) tm/radio: set_config failed: ESP_FAIL

**Cause:** esp-hosted is brought up by a **constructor before `app_main`**, so the host↔C6 link already exists
when our `powerCycleC6()` cuts the C6's power. Cycling it invalidates that link, and nothing re-attaches it.
**Fix:** `hostedConnectC6()` — `esp_hosted_connect_to_slave()`, 4 tries 250 ms apart — right after the cycle, which
is exactly what the reference does and says it must precede radio init (WadaMesh `tanmatsu/main/main.cpp:295-300`
and `326-340`). v11 log:

    I (4004) tm/app: C6 power-cycled through the CH32
    I (4134) transport: Waiting for esp_hosted slave to be ready
    I (4734) tm/app: C6 hosted link: UP

### Defect 2 (found on glass, FIXED in v12): S499's 90 s dev guard was ending every radio test

The guard ("90 s with no input → leave to the launcher") existed so a build that could not see keys could not
own the device. The key path is proven, and the guard was silently killing the app in the middle of a listen —
which is indistinguishable from a radio that stopped working. **Removed in v12**, source comment keeps the
reason. Leave by Esc.

### PROVEN on the device (v12, console captured by USB serial)

    I (5364) tm/radio: chip SX1262, version SX1261 V2D 2D02, errors 0
    I (5374) tm/radio: configured 869.618 MHz bw 62 kHz sf 8 cr 4/8 sync 0x12 preamble 32 pwr 22 dBm
    I (5384) tm/radio: listening
    I (8964) tm/app: TX 11 B seq 1 4144 mV -> ok          … up to seq 12, every 10 s, each ACKed
    I (116924) tm/app: RX 101 B rssi 0 dBm snr -12 dB  (not our beacon)

- **TX proven**: twelve beacons, the C6 ACKing each transaction; the payload carries the live battery
  (4144 mV), so the value is real and not a constant.
- **RX proven**: 101 bytes arrived in the same second Filip sent a message from the L2 Pro on the "Test Channel"
  channel, at the SLO settings. We do not decode MeshCore yet (step 3b); we do prove the receiver hears it.

### ⚠️ Known defect, NOT fixed: RSSI reports 0 dBm

`RX … rssi 0 dBm snr -12 dB`. **0 dBm is impossible for LoRa**, so the RSSI field is not populated on the receive
path (SNR, by contrast, looks like a real measurement). Do not quote RSSI from this build. To fix: read how
`lora.c` fills `lora_packet_stats_t` on the RX path (whether the C6 sends stats for queued packets, or only the
payload) before changing the conversion — the conversion itself is the component's documented one
(`rssi_dbm = -raw/2`, `snr_db = raw/4`). Also open: **−12 dB SNR at desk range is lower than expected**, which
deserves a two-unit distance test rather than a conclusion.

### How the L2's settings were confirmed (they match the source)

Filip read them off the L2's own Settings → Radio: **869.618 MHz / 62.5 kHz / SF8 / CR8**. That matches
`firmware/src/ui/settings_prefs.h:70` and `firmware/src/radio.cpp`, so the plan in `core/radio_plan.h` is not
built on a stale source.

### For step 3b — what Filip gave this session, and what is still open

- **Test channel: "Test Channel"**, and its **key was given to me in the session** (32 hex chars = 16 bytes). It is
  NOT written into these notes: Filip typed it, and a channel key is a shared secret. Ask him for it when 3b
  starts, or he may put it in a local file. The SLO radio settings he read off the L2's own Settings -> Radio
  **match the source** (869.618 MHz / 62.5 kHz / SF8 / CR8), which is the confirmation that mattered.
- **Node identity: `EDCT`** (Filip, S500). Its own node on the mesh, not a copy of the L2.
- **Still open:** path-hash mode (2B expected on the public SLO mesh) — confirm on the L2 when 3b starts.

### What could NOT be checked

- Any MeshCore decoding/encoding (that is 3b, by design).
- RSSI, see above.
- Whether TANMATSU→L2 traffic is *understood* by the L2: our beacon is deliberately not a MeshCore packet, so no
  peer can decode it yet. TX is proven at the radio/C6 level, not at the mesh level.
- Card throughput, the no-card branch, and the app's own heap under a long run.
- Nothing is committed: `tanmatsu/` is still not a git repo (vendor template copy).

## 2026-11-13 · S500 (session with Filip) · the SD card: root cause, BSP fix, build green

> **Written by DeepSeek, 2026-11-13, session with Filip present.** Read-only work on the sources plus one build;
> nothing installed yet at the time of writing (the install is the next decision below).

### Root cause — proven in source, not guessed

`mountSd()` could never have worked. The chain, each line read this session:

1. `nicolaielectronics/esp-hosted-tanmatsu` installs a **constructor**:
   `host/port/esp/freertos/src/port_esp_hosted_host_init.c:17` → `esp_hosted_init()`.
2. `esp_hosted_init()` brings up the SDIO transport → `host/port/esp/freertos/src/port_esp_hosted_host_sdio.c:356`
   calls `sdmmc_host_init()`.
3. On the ESP32-P4 there is **one** SDMMC host controller (`sd_host_claim_controller()` scans
   `s_platform.controllers[]`), so it is claimed **before `app_main` ever runs**.
4. Our own `esp_vfs_fat_sdmmc_mount()` then failed inside
   `esp_driver_sdmmc/src/sd_host_sdmmc.c:84` → *"no available sd host controller"*.

So the error was never about pins or the slot config: the controller was already taken, by design, by the Wi-Fi
link to the C6. Our `sdkconfig` agrees: `CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y`, `CONFIG_ESP_HOSTED_SDIO_SLOT=1`.

### The fix — use the BSP's Tanmatsu storage driver

`badge-bsp` already solves exactly this combination in `targets/tanmatsu/badge_bsp_storage.c`: slot 0 with the
native IOMUX pins (**CLK 43, CMD 44, D0-D3 39-42**, 4-bit, 40 MHz), the on-chip **LDO channel 4** as the card's
power control, a power cycle before every mount, and `host.init = sdmmc_host_init_noop` +
`host.deinit_p = sdmmc_host_deinit_slot_hosted` so esp-hosted keeps the controller. It even does the CMD52
dummy transaction that keeps the SDMMC ISR pointed at a live slot.

`bsp_device_initialize()` already calls `bsp_storage_initialize()` (`common/badge_bsp_device.c:98`), so the app
only has to mount. `main/main.cpp` now:

- `mountSd()` = `bsp_storage_get_status()` (so a missing card reads **NOT_PRESENT**, not as a failure) +
  `bsp_storage_mount_advanced(BSP_STORAGE_TYPE_SDCARD, "/sdcard", 10, false)`, and the reason is written next
  to the code so nobody re-derives it.
- dropped our hand-rolled `sdmmc_host_t` / `sdmmc_slot_config_t` / `esp_vfs_fat_sdmmc_mount` and their includes
  (`esp_vfs_fat.h`, `sdmmc_cmd.h`, `driver/sdmmc_host.h`); added `bsp/storage.h`.
- the Launcher's **OWNS** row now states the truth instead of a fixed string:
  `radio 868 · SD mounted` / `radio 868 · no SD card` (Filip's data-honesty rule: the UI shows ground truth).

### Build — real exit code

    make build   -> exit 0
    application.bin = 1,149,888 B (0x118BC0); generator: "0xe7440 bytes (45%) free" of the 0x200000 partition
    vs the launcher's 3200 KiB AppFS ceiling: ~1123 KiB, comfortable

### ON GLASS — v9 installed, and the mount is PROVEN (the same session)

    appfs upload om-lite "OM Lite" 9 build/tanmatsu/application.bin  -> Done!
    appfs list  -> om-lite | OM Lite | 9 | 1122K        appfs usage -> 1152K / 8128K

Console, captured **by USB serial into a file** (the P4 port disappears on every app start/exit, so the capture
reopens the port instead of opening it once):

    I (4021) sdcard: Initializing SD card
    I (4021) sdcard: Power cycling SD card...
    I (4321) sdcard: Allocating SD DMA buffer
    I (4511) sdcard: Mounted SD at '/sdcard'
    I (4511) tm/app: SD mounted at /sdcard
    I (4581) tm/app: first frame drawn (800x480)

Those two lines are the whole ticket: the BSP mounted a real card into our app, and the Launcher still drew.
Before this, the same boot said *"SD not mounted: no available sd host controller"*.

⚠️ **The `ldo: The voltage value 0 is out of the recommended range [500, 2700]` warning is the BSP's own**
(`reset_sd_card()` deliberately drops the LDO to 0 to power-cycle the card, `badge_bsp_storage.c:102`). It is
vendor code, it is harmless, and it will appear on every mount. Do not "fix" it in our tree.

⚠️ **A trap found on the way: the app descriptor's `Compile time` LIES after an incremental build.** The device
logged `Compile time: Nov 13 2026 14:03:48` — the S499 build — while it was running the 19:30 binary.
`build/tanmatsu/esp-idf/esp_app_format/…/esp_app_desc.c.obj` keeps its old timestamp and is not rebuilt. So the
running version can only be identified by **content** (a string that exists only in the new build) or by the
AppFS version, never by that stamp. When the stamp has to be true, build clean.

### How to capture the P4 console (written down, it cost time twice)

The launcher **swaps one physical USB PHY** between two controllers (`main/usb_device.c:212-241`:
`usb_serial_jtag_ll_phy_select(1)` = USB_DEVICE/BadgeLink, `(0)` = USB_DEBUG), so the two are mutually exclusive,
and in BadgeLink mode the launcher also silences logging (`dummy_log_vprintf`). Therefore:

| want | mode | reachable |
|---|---|---|
| `appfs upload/list` (BadgeLink) | **BadgeLink** `16d0:0f9a` | no P4 serial at all |
| the app's console | **Debug** `303a:1001`, serial `30:ED:A0:E0:F2:91` | no BadgeLink |

**F6 toggles it**, sometimes live, sometimes with a reboot. An AppFS app that never calls `usb_mode_set()` runs
with the PHY in its reset default, which is why the app's own log *is* readable in Debug mode.
Ports: `/dev/serial/by-id/…_30:ED:A0:E0:F2:91-if00` (P4) and `…_98:A3:16:A6:9D:40-if00` (C6) — **never ttyACM
numbers**. Hold **DTR/RTS deasserted** when opening: a DTR pulse reboots a chip (S499 incident).

### What could NOT be checked (honest list)

- Card **speed / throughput**, and whether the L2's FAT32 map pack (`tiles.pack` + `tiles.idx`, 948 MB) reads on
  this board — that waits for step 5 (Map), where the reader is written.
- Filesystem-level write/read from the app beyond the mount (nothing writes to the card yet).
- The **no-card** branch: Filip's card was in the slot, so `NOT_PRESENT` was not exercised on glass. It is a
  one-line status check, not a code path with logic, but it stays unproven until a card is pulled.
- Not committed: the Tanmatsu app folder is still not a git repo (vendor template copy), same as before.

### Next step

**Step 3 — OM Lite core on the real radio**: LoRa through the Tanmatsu's own path (the `tanmatsu-lora`
component esp-hosted exposes via RPC to the C6, and WadaMesh's variant as the reference), MeshCore with the L2's
identity rules and SLO settings (869.618 MHz, BW 62.5, SF 8), channels, send/receive. Per `NEXT-FOR-DS.md` step 3,
and it must not disturb the L2 (`cd ../firmware && sh test/host/run.sh` stays green).

## 2026-11-13 · S499 (session with Filip) · step 2: the app skeleton, the toolchain, the fence, the look freeze

> **Written by DeepSeek, 2026-11-13, in a session with Filip present** (not a fenced run). Decided with him
> this session: **the six previews PASS** (plus a fix, below); **the sandbox is widened**; **the toolchain
> goes to `~/dev/esp-idf-v5.5.1`**, outside the synced tree.

### Filip's decisions and what each one changed

| decision | done |
|---|---|
| **The look passes** — and: the OM Lite app must match the *L2's* OM Lite, the Launcher must follow the **CD (Cyberdeck) launcher** | palette stays parsed from `firmware/src/ui/theme.h`; the CD launcher's model (app table + running state + Start/Stop, `cyberdeck/launcher/app.py:188-235`) is what the inner Launcher implements |
| **Fix the cloud symbol** | it was drawn FILLED while the other four were outlines, so it read as "active" on every page. Now an outline (union silhouette eroded by the stroke, so no internal arcs). Verified pixel-wise: **only the hint bar changed** in all six PNGs (`/tmp/cmp_shots.py`: diff box x 679-703, y 458-476) |
| **Widen the fence** (`tanmatsu/` RW, like `firmware/`) | `paperclip-pilot/ds-fence.sh`: `TM` bind RW + `.git` RO if present, + **RO binds the build also needs**: `~/dev/wadamesh` (the sideload tooling), `~/dev/esp-idf-v5.5.1` and `~/.espressif` (toolchain, RO for the same reason PlatformIO is), and `IDF_PATH`/`IDF_TOOLS_PATH` exported in the `--setenv` block because HOME inside the fence is the pilot home. `bash -n` clean; backup `ds-fence.sh.bak-2026-11-13-S499-tanmatsu`. **Not yet exercised by a real fenced run** (see below) |
| **Toolchain** | ESP-IDF **v5.5.1** cloned to `~/dev/esp-idf-v5.5.1` + `install.sh esp32p4` → `~/.espressif`. Kept out of `Projects/` so Syncthing never replicates ~3 GB |

### New files this session (all under `tanmatsu/`)

| file | what it is |
|---|---|
| `main/tm_theme.h` | **the ONE metrics table** for 800x480 (the preview tool's TM dict moved here, as its own comment demanded). Palette is NOT here — it stays the L2's `theme.h` |
| `tools/mkfonts.py` | generates the six Tanmatsu faces by **calling the L2's `firmware/tools/mkvlw.py`** (one format owner) and extracting the byte array into `main/fonts/*.bin`; verifies against mkvlw's own printed length + parses the header back. `--check` mode for CI-style re-verification |
| `main/font.h/.cpp` | VLW decoder (24-byte header, 24 B/glyph records, 8-bit coverage), UTF-8 input, alpha-blended text into the framebuffer. Format read from `mkvlw.py`'s writer, not guessed |
| `main/screen.h/.cpp` | 800x480 landscape RGB565 framebuffer in PSRAM + the **proven rotation** (`panel_y = lx`, `panel_x = 479 - ly`) and band blit (40 rows), rects/lines/rings/polylines/masked blends |
| `main/launcher_page.h/.cpp` | the approved Launcher home: status bar, MODULES list with dot + badge + gold band/ring, detail pane with KV rows + Open/Stop, the note, and the hint bar with the five symbol sigils |
| `main/main.cpp` | app entry: `bsp_device_initialize`, input queue, battery, SD mount, **every key logged** (`[key] nav key=… state=…` / `[key] kbd ascii=…`), the core wired (start/stop through `tm::Core`), Esc/Home = back to the stock launcher (`esp_restart()`) |
| `CMakeLists.txt`, `core/CMakeLists.txt`, `main/CMakeLists.txt`, `main/idf_component.yml`, `build.sh` | the IDF project; `build.sh` sources IDF from `~/dev`, regenerates a stale `sdkconfig` (PLAN § 6.3 trap), builds **`gen_project_binary`** (not `build`: no OTA size check for an AppFS app) and prints the size against the launcher's 3200 KiB ceiling. **Never flashes** |
| `partition_tables/16M.csv`, `sdkconfigs/{general,tanmatsu}` | board facts copied from the WadaMesh reference (`53d17d0`) — 16 MB flash, custom table, custom CA |
| `docs/RECOVERY.md` | **written before any install** (BRIEF § 8): removal via the launcher's own **F5 Remove** (verified in launcher v0.3.1 source `main/menu/apps.c:586-598`), metadata-sector restore, full AppFS restore, and the C6-wedge → full power cycle rule |

### What was read vs assumed

- **Read (source-verified):** the launcher v0.3.1's BSP usage (`bsp_device_initialize`, `bsp_input_get_queue`,
  `bsp_display_blit`, `bsp_display_get_parameters`, `bsp_power_get_battery_information` with
  `battery_available`/`battery_charging`/`voltage`/`remaining_percentage`, `INPUT_EVENT_TYPE_NAVIGATION` +
  `args_navigation.key/state`, `INPUT_EVENT_TYPE_KEYBOARD` + `args_keyboard.ascii`); the navigation key names
  (`BSP_INPUT_NAVIGATION_KEY_UP/DOWN/LEFT/RIGHT/RETURN/ESC/HOME/BACKSPACE`); the panel's native **480x800
  portrait** orientation and the rotation formula (WadaMesh `variants/tanmatsu` + its app `main.cpp`); the
  AppFS sideload order and the version rule (`TANMATSU_SIDELOAD.md`, `tan_deploy.py`).
- **Assumed, and marked so in the code:** the `bsp_configuration_t` initializer field names — if the BSP
  header disagrees the build will say so in one line; the SD card needs the board's SDMMC pins, so
  `mountSd()` uses `SDMMC_SLOT_CONFIG_DEFAULT()` and **logs what happens** instead of guessing pins.
- **Not done (deliberately):** the five symbol keys are logged but **not mapped** in `core/keys.h` — their
  codes are unknown until this logger has seen them on the glass (BRIEF § 5 forbids guessing).

### Build status — BLOCKED inside IDF 6.0.2's mbedtls (nothing of ours has compiled yet)

Toolchain facts (all measured this session):

- First build against **IDF 5.5.1 + BSP 1.6.3** failed in dependency solving: *"no versions of idf match
  >=6.0.2"* → PLAN § 6.2 corrected (see the note in `docs/PLAN.md`); 5.5.1 deleted, **IDF v6.0.2** cloned to
  `~/dev/esp-idf-v6.0.2` + `install.sh esp32p4` (exit 0), BSP pin set to **2.0.0**.
- Our project **configures completely** with 6.0.2 + `badge-bsp 2.0.0` + `pax-gfx 2.2.2` (the whole managed
  dependency tree resolves: BADGE_BSP 2.0.0, TANMATSU_COPROCESSOR 0.8.0, ESP_LCD_ST7701 2.0.2, pax-gfx …),
  and the toolchain compiles — a stock `hello_world` for `esp32p4` builds green (exit 0) with the same
  environment, so **the install is healthy**.
- The build then dies in **make**, not in compile:

      make[3]: *** No rule to make target 'esp-idf/mbedtls/mbedtls/tf-psa-crypto/drivers/builtin/CMakeFiles/builtin.dir/src/aes.c.obj',
               needed by 'esp-idf/mbedtls/mbedtls/tf-psa-crypto/core/libtfpsacrypto.a'.  Stop.

  What is established: the rule for that object **exists** (`drivers/builtin/CMakeFiles/builtin.dir/build.make`
  has 13 references to `aes.c`), the source file exists (46 `.c` in `builtin/src/`), the tree is clean, and the
  **consumer's `core/CMakeFiles/tfpsacrypto.dir/build.make` does not include the produces' build.make** — i.e.
  `core/CMakeLists.txt:130-135` puts `$<TARGET_OBJECTS:builtin>` into `add_library(...)` from a *sibling*
  directory and the generated Makefiles do not carry the rule across. Not caused by CMake version: fails
  identically on **CMake 4.2.3** and **3.31.6** (3.31 is now pinned inside `build.sh`).
  Not our code: none of `main/*.cpp` is reached before this point.
- **SOLVED — the cause is our own project skeleton, not the toolchain (DeepSeek, S499, same day).** After the
  official docs (`https://docs.tanmatsu.cloud/software/`, page *Compiling the template app*):
  `git clone https://github.com/Nicolai-Electronics/tanmatsu-template` + `.IDF_PATH`/`.IDF_TOOLS_PATH`
  pointing at `~/dev/esp-idf-v6.0.2` / `~/.espressif`, then **`make build` → exit 0**,
  `build/tanmatsu/application.bin` **979,744 B**, and `libtfpsacrypto.a` + `aes.c.obj` **built fine** in it.
  Same IDF, same toolchain, same CMake 3.31.6, same machine → the toolchain was never the problem; the
  hand-written CMake/sdkconfig of this folder is. The template also **confirms IDF v6.0.2** (`Makefile:
  IDF_VERSION ?= v6.0.2`) and the current BSP (**2.0.3**, with `tanmatsu-wifi =1.3.2`, `pax-gfx =2.2.2`), and
  its `main/CMakeLists.txt` adds `esp-hosted-tanmatsu` via `extra_requires` for `esp32p4` — a piece our
  `main/CMakeLists.txt` lacks. Small sdkconfig diffs found by diffing the template's overlays against ours
  (copied from WadaMesh): the template sets `CONFIG_ESP32P4_REV_MIN_1/FULL=1`,
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`, `CONFIG_ESP_REV_MIN_FULL=1` and uses
  `partition_tables/esp32p4-16mb.csv`; it panics with HALT, ours with REBOOT.
- **The way forward (decided): rebase this app onto the official template** instead of debugging a
  hand-rolled IDF project — it is the documented, vendor-supported path, and it is proven to build here.
  Concretely: take the template's `Makefile`, root `CMakeLists.txt`, `sdkconfigs/`, `partition_tables/`; move
  our sources into its `main/` (`main.cpp`, `font.*`, `screen.*`, `launcher_page.*`, `tm_theme.h`,
  `fonts/*.bin`) with `core/` kept as a component; merge the dependency pins. `make build` is then the ONE
  build command (drop our `build.sh`, no second way), output `build/tanmatsu/application.bin`.
  **Install path also improves**: the docs document **BadgeLink** for putting an app into AppFS
  (`docs.tanmatsu.cloud/software/badgelink/`) — use that first, keep the sector-diff esptool route in
  `RECOVERY.md` as the fallback (AppFS component tools: `appfs_generate.py`, `appfs_add_file.py`).


### Rebase onto the vendor template — DONE, and the app BUILDS (DeepSeek, S499, 2026-11-13)

`make build` → **exit 0**, `build/tanmatsu/application.bin` = **1,119,936 B (0x1118C0)**; the generator's own
line: *"application.bin binary size 0x1118c0 bytes. Smallest app partition is 0x200000 bytes. 0xee740 bytes
(47%) free."* Against the launcher's **3200 KiB** AppFS ceiling that is ~1094 KiB — comfortable.

What changed (scaffolding only; no product code was rewritten):

- **Taken from `tanmatsu-template`** (documented at `docs.tanmatsu.cloud/software/`): its `Makefile` (respects
  `.IDF_PATH`/`.IDF_TOOLS_PATH`, `IDF_VERSION ?= v6.0.2`), root `CMakeLists.txt` (`project(application)`),
  `sdkconfigs/` (incl. the `ESP32P4_REV_MIN_1` block our WadaMesh copy lacked), `partition_tables/`,
  `esp_idf_project_configuration.json`. `.IDF_PATH` → `~/dev/esp-idf-v6.0.2`, `.IDF_TOOLS_PATH` → `~/.espressif`.
- **Our own**: `EXTRA_COMPONENT_DIRS core` in the root CMakeLists, and `main/CMakeLists.txt` re-shaped on the
  template's (SRCS ours + `EMBED_FILES` the six `.bin` faces + `PRIV_REQUIRES … core` +
  `esp-hosted-tanmatsu` via `extra_requires` for esp32p4 — the piece our old file was missing).
- `main/idf_component.yml` = the template's pins (**badge-bsp 2.0.3**, tanmatsu-wifi 1.3.2, pax-gfx 2.2.2).
- **`build.sh` deleted** — `make build` is the one build command (no second way).
- Old scaffolding kept at `/tmp/pocket-mesh-scaffolding-before-rebase/` (scratch, not in the repo).

**The compiler then corrected my guesses against the real BSP/core headers** (this is the step-2 value): missing
`<stdio.h>` in `font.cpp`/`launcher_page.cpp`, `Face::Label` does not exist (the enum is
Title/Row/Body/Meta/Hint/Kv), 24-bit colour literals need `rgb565(...)`, `esp_ptr_external_ram` needs
`esp_memory_utils.h`, and the core's real API is `Allocator::alloc(owner,bytes)`/`release(owner,p)`,
`Module{id,name,meta,pages,available,owns,start,stop,tick,draw,onKey}`, `StartResult::code/busy/leaked`,
`StopResult::code/leaked/leak_stats`. **All fixed; nothing was assumed a second time.**

### FIRST INSTALL ON GLASS — done, and what it taught us (DeepSeek, S499, 2026-11-13)

**It is installed and it runs.** BadgeLink route, exactly as documented:

    appfs upload om-lite "OM Lite" 1 build/tanmatsu/application.bin   -> Done!
    appfs list   ->  om-lite | OM Lite | 1 | 1094K
    appfs usage  ->  Usage: 1152K / 8128K (14.2%)
    start om-lite -> the app booted from AppFS

Boot log from the device (proves the platform path): `app_init: App version 1`, `ESP-IDF: v6.0.2`,
`esp_psram: Adding pool of 32768K`, all six `[tm] font … ok`, `tm/screen: display parameters: rc=0
480x800 format=10 endianness=1`, `landscape framebuffer 800x480 in PSRAM`, `first frame drawn (800x480)`.

**Confirmed CORRECT on glass** (Filip's eyes): the rotation/blit mapping, the whole Launcher layout (vertical
pane separator, three module rows with the first selected, the two buttons on the right, the five-symbol hint
bar), and the pixel format — the BSP header decodes our parameters as **format 10 =
`BSP_DISPLAY_COLOR_FORMAT_16_565RGB`, endianness 1 = `BSP_DISPLAY_ENDIAN_LITTLE`**, i.e. exactly the RGB565/LE
buffer `screen.cpp` writes. No colour or rotation change needed.

**Four real bugs the glass found, all fixed in v2:**

1. **No text at all** — `font.cpp` walked glyph records with a **24-byte stride; mkvlw.py packs 28**
   (`>IIIIiiI` = 7 × u32). Every lookup landed on the wrong record → no glyph ever matched → only advance
   widths, invisible text. Fixed with `kRecordBytes = 28`. *The host-side length check in `tools/mkfonts.py`
   could not catch this (it verified the blob length, not the record stride) — a host test that decodes one
   glyph and compares it to mkvlw.py's own numbers is the missing guard.*
2. **The circle key looked like a sun** — `Screen::ring()`'s coverage treated "inside the hole" as "inside the
   ring" (`inner < 0 ? 1 : inner`), so it painted a solid disc. Now an annulus: `clamp(r_out-d+.5) *
   clamp(d-r_in+.5)`.
3. **Esc never reached the app** — on this board Esc arrives as an **`INPUT_EVENT_TYPE_KEYBOARD`** event
   (ASCII 0x1B), not as a navigation key; the first mapping only looked at `INPUT_EVENT_TYPE_NAVIGATION`.
   `mapKey()` now maps keyboard events too (0x1B Esc, 0x0D/0x0A Enter, 0x08/0x7F Backspace, else `Key::Char`).
4. **The exit path** — `esp_restart()` replaced by the BSP's own **`bsp_device_restart_to_launcher()`**
   (what the vendor template uses).

**Plus a dev guard, because of how #3 and #4 stranded us:** v1 could not see keys *and* could not exit, so it
owned the device (a reset does nothing; only a full power cycle gets out). v2 exits to the launcher by itself
after **90 s with no input event** (`kDevLeaveAfterS`). Remove it once the key path is proven.

**Still failing, known and next:** `mountSd()` → `E SD_HOST: sd_host_create_sdmmc_controller(84): no available
sd host controller` — our `SDMMC_SLOT_CONFIG_DEFAULT()` is not the board's slot; the pins/host must come from
the BSP (the template does not show it either). Board support for the card is the next ticket.

### Step 2 ACCEPTED ON THE DEVICE — and the key codes are READ (DeepSeek, S499, 2026-11-13)

Filip, on the unit, with v2 installed: *"OM lite is in the apps! And I can launch it. And the text is there
now!! I can select options on the left side, OM lite, Mesh-Torry, OPS-TOC."* That is step 2's acceptance:
it installs as an app, launches from the launcher, draws the approved page **with text**, and the keyboard
drives it.

**The five symbol keys and the basics, read off the glass** (`[key] nav key=N state=S`, one press each, P4
console captured over USB):

| key | `bsp_input_navigation_key_t` | key | |
|---|---|---|---|
| Up | **4** | triangle | **21** |
| Down | **5** | square | **22** |
| Enter | **13** | circle | **23** |
| | | cloud | **24** |
| | | diamond | **25** |

Written into **`core/keys.h`** (`kKeys[]` codes, each with the log line that proves it) and the host test was
raised from "every code is kUnlogged" to the stronger "these are the codes, `key_from_code()` agrees, and the
still-unlogged keys (Left, Right, Backspace) remain placeholders that resolve to `Key::None`" — **162 checks,
0 failures**. `mapKey()` now falls back to `tm::key_from_code()` for any navigation key, so the symbol keys
reach the router through the same single table (v3 build, 1,122,128 B). Esc is the exception: it arrives as a
**KEYBOARD** event, ASCII 0x1B, and never as a navigation key.

**Filip's FOCUS RULE, given on the glass (S499) — implemented in v4 (1,122,560 B, 162 host checks green):**

> *"when selecting the apps on the left, pressing enter should switch you to the right side, where the apps
> can be started/stopped. Pressing esc brings you back to the left side."*

That is now the model, replacing the older list-only focus:

| where | key | what happens |
|---|---|---|
| list (left) | `↑` `↓` | move the selection |
| list | **Enter** | **focus moves to the actions on the right** |
| list | Esc | leaves the app (back to the stock launcher) |
| actions (right) | `←` `→` | pick **Open** or **Stop** |
| actions | Enter | run the picked one |
| actions | **Esc** | **back to the left side** |

The 2 px gold ring marks the focused AREA: the row when the list has the focus, the button when the actions
do (the row keeps its selection band with a plain outline, so two things can never look focused at once),
and the hint bar swaps to `pick / do it / back`. **Open** on a stopped module starts it; on a running one it
logs *"its pages arrive in steps 3-7"* instead of looking inert. Start/Stop still goes through the core
(`tm::Core`), so the registry's resource lease and the leak report stay the only path.

**Still open from the same round:** `mountSd()` — `no available sd host controller`; the card needs the
board's own SDMMC pins/host from the BSP (the vendor template does not show it either).

### The EXIT bug, closed (v8, S499) — it was never the exit

Filip's report: *"after the last ESC it reboots, it doesn't go to the default launcher."* Three wrong guesses
(boot-select not cleared → autostart → both) before the console said what was really happening. The recorded
transition (`/tmp/exit-transition.log`) settled it:

    rst:0x17 (CHIP_USB_UART_RESET)
    app_init: Project name: launcher   App version: v0.3.1     ← the launcher DID boot
    main_task: Calling app_main() → ST7701 panel → ES8156
    assert failed: xQueueGenericSend queue.c:936 (pxQueue)      ← and then it died

So the exit worked from v5 on; the **launcher** crashed. Three real causes, all now fixed:

1. **A stale C6.** Our app had attached to the C6 (esp-hosted/SDIO) and left it in its own state; the
   launcher then attached to that and asserted. The reference documents it (*"Power-cycle the C6 through the
   CH32 so the app ALWAYS gets a fresh C6"*, WadaMesh `main.cpp:326-338`) — `powerCycleC6()` now runs at
   **startup and on exit** (`tanmatsu_coprocessor_radio_disable` → 150 ms → `radio_enable_application` →
   600 ms).
2. **"Always the menu" is Filip's rule (S499).** The launcher sets its own `app_fav/autostart` whenever an app
   is started from its Apps menu, so the app clears that key **at startup and on exit** — every boot lands on
   the menu, and a power-cycle from inside the app cannot autostart it either. (Esc leaves; verified on the
   unit: `badgelink nvs list app_fav` → empty.)
3. **The panel handover.** The launcher ran but its screen stayed **black** until a power cycle, so the exit
   now paints a settled black frame and waits 250 ms before resetting.

Verified by Filip on the unit with v8: **Esc → stock launcher, screen fine.**

⚠️ **And the reboot itself is EXPECTED, not a bug** (Filip, S499, after asking twice): an AppFS app *is* the
running firmware — the bootloader boots it from AppFS instead of the launcher — so returning to the launcher
can only happen by resetting the chip. Every app on this platform exits that way. The exit is therefore
"reset → launcher boots → menu" (`rst:0xc (SW_CPU_RESET)` → `app_init: Project name: launcher`), which takes
as long as a normal boot. Nothing to fix; do not chase it. Diagnostics that made this
possible, worth keeping: the P4 console must be captured **by serial** and **into a file** (the port vanishes
on every app start/exit; a pipe lost three captures), and `rst:0x17 (CHIP_USB_UART_RESET)` is what a
`bsp_device_restart_to_launcher()`/`esp_restart()` looks like.

**Launcher behaviour worth remembering while iterating:** after an app is started once (BadgeLink `start`),
the launcher **auto-starts it after every reset**, so a power cycle does not get you back to the menu — the
way in is `appfs delete` (or, with the app running, Esc → it exits, and v2's 90 s dev guard is the fallback).
The P4's USB port also disappears on every app start/exit, so any console capture must reopen **by serial**.

### Next step — install with Filip present

**Route: BadgeLink (the documented one).** Get the tools from the BadgeLink repo releases (`tools.zip` + the
`install.sh` venv script), install `60-badgelink.rules` into `/etc/udev/rules.d` (needs sudo), then:

    ./badgelink.sh appfs upload om-lite "OM Lite" 1 build/tanmatsu/application.bin
    ./badgelink.sh start om-lite          # launches it without touching the keyboard
    ./badgelink.sh appfs list             # slug/title/version/size as the launcher sees it
    ./badgelink.sh appfs delete om-lite   # the clean removal

**The device must be in USB-device (BadgeLink) mode** — the docs: press the **purple diamond key**; the icon
top-right changes from a bug (debug) to a USB symbol. (Our `DEVICE.md` knew the F6 menu toggle; the diamond key
is the quick one.) Fallback if BadgeLink misbehaves: the esptool sector-diff route in `RECOVERY.md` § 3.

After it launches: read the `[key]` log (`[key] nav key=… state=…`), fill `core/keys.h`, then step 3.

⚠️ From the reference: a re-launched app can wedge esp-hosted on the C6, so step 3 must power-cycle the C6
through the CH32 at startup, and **not** call `Serial.begin()` on the P4 (WadaMesh `main.cpp:303-320`).



## 2026-11-13 · EST-426 · the app core as portable logic + host tests (no screen code, no device)


> **Written by DeepSeek, EST-426 ticket run, 2026-11-13.** Fenced Paperclip run: no session start/end, no
> device, no install, no commits. Everything below was run in `paperclip-pilot/sweep/tanmatsu-work/`; the
> only other folders touched are the read-only `sweep/tanmatsu-ref/` (not edited) and `firmware/` (read +
> its own suite run, nothing edited — `git status` is 0 lines).

### Re-seed first (as the ticket says), commands as run

    cd /home/slofi/paperclip-pilot/sweep/tanmatsu-work
    cp -a /home/slofi/Projects/pocket-mesh/tanmatsu/BRIEF.md .
    cp -a /home/slofi/Projects/pocket-mesh/tanmatsu/docs/.  docs/
    cp -a /home/slofi/Projects/pocket-mesh/tanmatsu/shots/. shots/
    cp -a /home/slofi/Projects/pocket-mesh/tanmatsu/tools/. tools/
    diff -rq . /home/slofi/Projects/pocket-mesh/tanmatsu     # -> identical (BRIEF.md was missing here)

### 1. The Settings preview fix (one PNG re-rendered)

The bug: the TX-power row behind the open confirm sheet already read **20 dBm** — the NEW value — while the
sheet asked "Change TX power to 20 dBm?". That contradicts the sheet's own last line ("Apply … shows it in
the row") and the EST-422 rule that **Cancel changes nothing** (`ui/settings_actions.h:1-30`).

The fix is the same trick as the `NODE_KEY` one from EST-425: **one pair of constants**, so the row and the
question cannot disagree again (`tools/preview_tanmatsu.py`):

    TX_POWER_OLD = '22 dBm'      # the L2's seed/SLO value (firmware/src/ui/settings_prefs.h:38,42)
    TX_POWER_NEW = '20 dBm'
    CONFIRM_Q = 'Change TX power to %s?' % TX_POWER_NEW
    ... setrow(..., 'TX power', TX_POWER_OLD, focused=True)   # the COMMITTED value until Apply

`python3 -B tools/preview_tanmatsu.py settings` → exit 0, 800×480. **Only that shot changed**: md5 of the
other five PNGs is identical before/after the re-render (checked with `md5sum shots/*.png`).

### 2. `core/` — the app core as plain C++ (900 lines, no hardware, no drawing)

| file | what it is |
|---|---|
| `core/pages.h` | `Page` (Launcher, Nodes, Chat, Map, Rptr, Settings) and `page_bit`/`covers`: a module declares the pages it draws as a bitmask (OM Lite owns all five), so there is no second page table. |
| `core/keys.h` | the **ONE key table** (PLAN §5): names + `kUnlogged` codes. `page_for_symbol()` is the only switch; `key_from_code(kUnlogged)` returns `Key::None`, so a placeholder can never act as a real key. **No hardware code is written anywhere** — step 2's on-glass logger fills it. |
| `core/module.h` | the PLAN §2 `Module`: id/name/meta, `pages`, `available` (false = `LATER`), `owns` (the resources it asks for), `start/stop/tick/draw/onKey`. `Canvas` is only a type — no drawing code. |
| `core/alloc.{h,cpp}` | the counted allocator: every block carries its owner, bytes and blocks are counted per module, `leak(i)`/`leakCount()`/`total()`; heap source is a function pair (malloc/free here, PSRAM on the device). A wrong-owner or foreign free is refused, so it shows up as a leak instead of corrupting the count. |
| `core/registry.{h,cpp}` | states Running/Stopped/Later; `start` refuses `AlreadyRunning`/`NotAvailable`/`ResourceBusy` (naming the resource) and stops report `leaked` + numbers; `drawPage()` draws only the module that owns the page. |
| `core/router.{h,cpp}` | the five symbol keys + Esc: `Switched` / `OfferStart` (stopped target — never auto-starts) / `Noop`; an 8-deep back stack that skips pages whose module was stopped. |
| `core/core.{h,cpp}` | the facade: owns allocator+registry+router, consumes symbol keys and Esc **before** a module sees them, gives the screen to exactly one owner (`"launcher"` or the module id), drops back to the Launcher when the on-screen module stops, and prints the one-line leak (`leak: om-lite still holds 2 blocks / 3000 bytes after stop`). `ownsRadioDriver()` is the core's, always (PLAN §2 rule 1). |

### 3. `test/host/` — five ARMED tests, real exit codes

    cd paperclip-pilot/sweep/tanmatsu-work && sh test/host/run.sh
    test  start-stop-start keeps the same counted memory
    test  a module that leaks on stop is reported by name
          leak: leaky still holds 2 blocks / 3000 bytes after stop
    test  a second owner of the radio is refused
    test  symbol keys route to their page and no module swallows them
    test  states (running/stopped/later) and one drawing owner

    **152 checks, 0 failure(s)          EXIT=0**

Build: `g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core` (the L2 runner's own flags); the runner is
`test/host/run.sh`, one binary, `mktemp` + trap, nothing left behind.

Coverage beyond the ticket's four: `AlreadyRunning`/`NotRunning`/`NotAvailable`, the five key names + the
`kUnlogged` invariant, the full Esc walk (Rptr→Map→Chat→Nodes→Launcher and nothing at the root), a non-symbol
key reaching the module only while its page is on screen, and the module keeps running (not drawing) while
the Launcher is on screen.

**Armed — each test was shown RED on a deliberately broken copy, then reverted** (backups in /tmp, files
compared byte-identical afterwards):

| mutation | result |
|---|---|
| `Registry::stop` no longer calls the module's `stop()` | EXIT=1 — `off.leaked == nullptr` (154) failed, 4 failures |
| the leak check is removed from `StopResult` | EXIT=1 — `off.leaked != nullptr` (197) failed |
| the one-owner-per-resource check is disabled | EXIT=1 — `second.code == ResourceBusy` (242) failed |
| `SymSquare` routed to Chat instead of Nodes | EXIT=1 — `page_for_symbol(SymSquare) == Nodes` (283) failed |
| the running module draws even when the Launcher is on screen | EXIT=1 — `draw() == "launcher"` (371) failed |

After the revert the same five tests are green again (152 checks, EXIT=0), and `diff -q` against the backups
is silent for `registry.cpp`, `core.cpp` and `keys.h`.

### 4. The L2 is untouched and still green

    cd /home/slofi/Projects/pocket-mesh/firmware && sh test/host/run.sh     # L2 SUITE EXIT=0
    git status --short                                                     # 0 lines

No L2 file was included or edited by this run (the core is self-contained C++), so there was nothing to
re-run for a shared file — the suite was run anyway, as the ticket asks.

### What could NOT be checked (honest list)

- **No build for the device and no device**: still no ESP-IDF/`esp32p4` toolchain here (PLAN §6.7), so this
  is host-tested logic only — nothing was compiled into an app, nothing was installed.
- **The key codes remain unknown by design**: every code is `kUnlogged`; the five-key route is pinned by
  NAME. If the BSP renames or renumbers the keys, `keys.h` is the one file that changes.
- **The allocator counts requested bytes only** (its per-block header is not billed to the module, because
  that is bookkeeping, not the module's memory). A real start/stop test on the glass will compare heap
  numbers with `mem_stats`/`psram_alloc`, which this tree does not have.
- **No real allocation pressure**: no PSRAM source is wired yet (the function-pair hook is there), and no
  module in this tree allocates more than a few KB.
- **The Launcher page itself is not drawn here**: `Core::draw()` returns `"launcher"` and the shell owns
  those pixels. The screen code waits for Filip's approval of the six previews.
- Two design choices worth Claude's eye: (a) **several modules may run at once** as long as their
  resources do not clash (the radio test is specific only because of that); (b) `Core::stop` sends the page
  back to the Launcher when the stopped module was on screen — the alternative (a dead page) cannot happen.

### Next step

Filip's look decision on the six PNGs, and the three items in `NEXT-FOR-DS.md` (the look, the sandbox, the
toolchain). After that, the core is ready to be dropped into the step-2 app skeleton: `main.cpp` owns a
`Core`, registers OM Lite (`pages` = all five, `owns` = Radio|SdCard), draws the Launcher page, and calls
`core.handleKey()` on every key the BSP reports — with `keys.h` filled from the on-glass log. Nothing else
in this run's code needs to change for the device.

### Copy-in for Claude (updated for core/ and test/)

    cp -a paperclip-pilot/sweep/tanmatsu-work/BRIEF.md Projects/pocket-mesh/tanmatsu/
    cp -a paperclip-pilot/sweep/tanmatsu-work/docs/.   Projects/pocket-mesh/tanmatsu/docs/
    cp -a paperclip-pilot/sweep/tanmatsu-work/tools/.  Projects/pocket-mesh/tanmatsu/tools/
    cp -a paperclip-pilot/sweep/tanmatsu-work/shots/.  Projects/pocket-mesh/tanmatsu/shots/
    cp -a paperclip-pilot/sweep/tanmatsu-work/core/.   Projects/pocket-mesh/tanmatsu/core/
    cp -a paperclip-pilot/sweep/tanmatsu-work/test/.   Projects/pocket-mesh/tanmatsu/test/

## Copy-in: the working copy this file lives in

The sandbox still cannot write `Projects`, so EST-425 was edited in a **working copy with the same layout**,
seeded from `Projects` first (commands as run, 2026-11-13):

    mkdir -p /home/slofi/paperclip-pilot/sweep/tanmatsu-work
    cd /home/slofi/paperclip-pilot/sweep/tanmatsu-work
    cp -a /home/slofi/Projects/pocket-mesh/tanmatsu/docs  /home/slofi/Projects/pocket-mesh/tanmatsu/shots \
          /home/slofi/Projects/pocket-mesh/tanmatsu/tools  .

Copy-back for Claude (when the write fence opens):

    cp -a paperclip-pilot/sweep/tanmatsu-work/docs/.  Projects/pocket-mesh/tanmatsu/docs/
    cp -a paperclip-pilot/sweep/tanmatsu-work/tools/. Projects/pocket-mesh/tanmatsu/tools/
    cp -a paperclip-pilot/sweep/tanmatsu-work/shots/. Projects/pocket-mesh/tanmatsu/shots/

The WadaMesh reference (`paperclip-pilot/sweep/tanmatsu-ref/wadamesh-53d17d0/`, commit `53d17d0`) is read-only
and was **not** copied in, not edited, and nothing was installed from it.

---

## 2026-11-13 · EST-425 · step 1b previews (Map, RPTR, Settings) + PLAN §6 written from WadaMesh

### What was read (this run), in this order
- `tanmatsu/BRIEF.md` and `docs/PLAN.md` + `docs/PROGRESS.md` (the EST-424 work, now in Projects).
- **The WadaMesh reference, read-only:** `TANMATSU_PORT.md` (235 lines), `TANMATSU_SIDELOAD.md` (143),
  `platformio.ini` (grepped for `tanmatsu`: no env — the PIO route is dropped), and `tanmatsu-project/`:
  `build.sh`, `fetch-deps.sh`, `Makefile`, `main/{CMakeLists.txt,idf_component.yml}`, all five
  `components/*/CMakeLists.txt`, `partition_tables/16M.csv` (+ the other four tables, for comparison),
  `sdkconfigs/tanmatsu`, `tools/{tan_flash.sh,tan_deploy.py,fetch-appfs.sh,dump-pristine.sh}`, and
  `variants/tanmatsu/{TanmatsuLoraRadio.h,.cpp,idf_component.yml,target.cpp}`.
- L2 sources used for the three fixes/rules: `ui/scroll_indicator.h` + `ui/theme.h`
  (`kScrollTrackW=4`, `kSelPadX=kRowPadX=7`), `ui/settings_actions.h` (EST-422: the confirm list, the
  question wording, Cancel-left/Apply-right, red key only for destructive), `ui/settings_layout.h`
  (the five tabs General/Nodes/Chat/Map/RPTR), `rptr_page.h` (page names, console, remembered-password key).

### PLAN §6 is now evidence, not a shape
Rewritten end to end with `file:line` for every claim: the model (AppFS app under the badge.team launcher,
never `idf.py flash`), the toolchain (ESP-IDF **5.5.1**, project-local, `esp32p4`), the component set and
how **arduino-esp32** (a managed IDF component, 3.3.10) and **MeshCore** (vendored by `fetch-deps.sh` from
an S3 `pio` build, own component wrapper) get in, the build command (`./build.sh build` →
`build/tanmatsu/application.bin` via `gen_project_binary`), the **50 × 64 KB AppFS mapping ceiling** the
launcher imposes, the partition table (**`partition_tables/16M.csv`**: `appfs` 8 MB @ `0x420000`,
`locfd` FAT, the two 2 MB OTA slots being the launcher's), the sideload (deploy two blobs: app data first,
metadata sector last, read back and compare; `make install`/BadgeLink as the second route), and the radio
path (**SX1262/1268 on the C6, `lora_init_remote()` over the C6↔P4 link — not SPI from the P4**, sharing
the C6/esp-hosted with Wi-Fi+BLE, with microSD deliberately on SDMMC slot 0).

### What is NOT installed here (checked, nothing installed)
`idf.py` absent, `IDF_PATH` unset, no `~/esp*`/`~/.espressif`, **no `riscv32-esp-elf-gcc`**, no
`pio`/`platformio`, no `arduino-cli`, no `esptool` Python module, no `ninja` (cmake + git + Python 3.14 +
PIL are present, which is why the previews run). The reference tree also ships only the committed component
*wrappers*: `components/{meshcore/core,ardlibs/upstream,lvgl/upstream}` do not exist there, so **this
checkout alone cannot build** either. The build is documented and cited; it was not run and nothing was
installed.

### The three preview detail fixes (step 2 of the ticket)
1. **Nodes header key vs the PUBLIC KEY row.** The header said `key 4b 91 2e` while the row said
   `a1 b2 c3 ... 9f`. There is now **one fixture constant** `NODE_KEY` and both places read it
   (`tools/preview_tanmatsu.py`, the `NODES` block), so they cannot disagree again.
2. **Launcher Mesh-Torry chip vs subtitle.** The row said `STOPPED` next to `game · Wi-Fi · later` — the row
   is `LATER` now, its detail says `not built`, and it has no `Start` action. The chip is drawn from the
   fixture's own `state` field, which is the only place a row's truth comes from (OPS-TOC Lite was already
   `LATER`). The header count `3 · 1 running` stays correct.
3. **Nodes scrollbar vs the focus ring.** It ran through the ring because both ended at the pane's right
   edge. Fixed with the **L2's own rule**, read from `ui/scroll_indicator.h:8-11` and
   `ui/chat_selector.cpp:65`: the band + ring are inset from the pane's right edge by `kSelPadX` and the
   track is the last `kScrollTrackW` px of the viewport, in that same padding (800 px scale: 7 → 14,
   4 → 6, min thumb 8 → 24). New shared helpers `focus_band()` / `scroll_indicator()` in the tool implement
   it once, and all three lists (Launcher, Nodes, Chat) now use them — so the rule is one rule, not a Nodes
   patch. Nodes also gained the L2's "no track when the content fits" behaviour.

### Step 1b previews (same tool, same style, 800×480)
`tools/preview_tanmatsu.py` renders **six** pages now (`python3 tools/preview_tanmatsu.py`, exit 0):

| shot | what Filip should check |
|---|---|
| `2026-11-13-launcher-home.png` | unchanged look; Mesh-Torry row now honest (`LATER`) |
| `2026-11-13-nodes.png` | unchanged look; scrollbar clear of the ring; one key in both places |
| `2026-11-13-chat.png` | unchanged look; badge nudged off the age line |
| `2026-11-13-map.png` | **new**: map area + marker/trace panel, invented grid, zoom chip, `invented test grid` caption |
| `2026-11-13-rptr.png` | **new**: saved repeaters (key icon = remembered password), Scan chip, status card, pages, console line |
| `2026-11-13-settings.png` | **new**: the L2's five tabs, two-column rows, and the EST-422 confirm sheet **shown open** |

- **The confirm sheet follows the L2's EST-422 rule** (`ui/settings_actions.h:1-30, 116-160`): the question
  is `Change X to Y?` (here `Change TX power to 20 dBm?`), **Cancel is on the LEFT (the default side) and
  Apply on the right in gold**, and Cancel changes *nothing*. The keyboard translation is on the keys
  themselves (`esc` on Cancel, `↵` on Apply) so a mistaken press cannot change a setting (BRIEF § 5). One
  sheet serves every important setting (radio preset, TX power, path hash, node name, the Wi-Fi/BT/keyboard
  switches, every Delete/Forget), so one approval covers the rule.
- **Rule 9 in the map**: it is an *invented test grid* — drawn from scratch, no real geography, no
  coordinates, no place names; the marker labels are the same fake `TEST-A1 / RPT-Sever / RPT-Jug` used in
  Nodes, and the render says `invented test grid` on its face. No SSIDs, keys or real messages anywhere.

### What could NOT be checked
- **Nothing was built and nothing was installed** (see § above): the build/sideload facts come from the
  reference's files and READMEs, not from running them. That is the honest limit of PLAN §6.
- Still `[unverified]` from this machine: the partition table on Filip's own v0.2.8 unit, whether current
  BadgeLink speaks AppFS v3 to it, our final binary size against the 50 × 64 KB mapping ceiling, the
  launcher's uninstall wording, and which LoRa modem is fitted.
- **Key codes** for the five symbol keys and the arrows remain unknown (no BSP here); PLAN §5 still makes
  step 2's on-glass key logger the source of truth.
- The previews remain **host renders**: desktop DejaVu, design-intent sizes, no VLW faces yet
  (`mkvlw.py`), and no panel gamma. Look only, not metrics.
- One style wart left deliberately: the `cloud` sigil is drawn **filled** while the other four are
  outlines, so it reads heavier (and on a page whose key is not current it can look gold-ish). Filip may
  want all five outlined; it is one function (`sigil()`).
- Not touched on purpose: no device, no serial port, no flash, no server, no commit, nothing outside
  `sweep/tanmatsu-work/` and the read-only `sweep/tanmatsu-ref/`.

### Next step
**Filip looks at the six PNGs** (BRIEF work order: the look is approved before any UI code). If he says
yes, step 2 is bring-up "hello": app starts from the stock launcher, status bar + Launcher home at 800×480,
the key logger that fills `keys.h`, SD mount, battery — and the first real build, which needs an ESP-IDF
5.5.1 + `esp32p4` toolchain installed on a machine that is allowed to have it. If he says no, the fixes are
in `TM`/the fixtures, not in screen code.

---

## 2026-11-13 · EST-424 · step 0 + step 1 (plan and the Launcher preview)

### Sandbox note first (it shapes everything below)
This run had **read-only access to `~/Projects`**: `touch tanmatsu/docs/.wtest` →
`Read-only file system`. The deliverables were therefore written to
`/home/slofi/paperclip-pilot/sweep/est424-tanmatsu/` in the intended layout (`docs/`, `shots/`, `tools/`)
and **Claude moves them into `Projects/pocket-mesh/tanmatsu/`** on review. Nothing was written into
`Projects`, `firmware/` or the L2 tree; no commits, no device, no install.

### What was read (all of it, in this order)
- `tanmatsu/BRIEF.md`, in full (113 lines).
- `firmware/src/ui/theme.h`, in full — every colour and size token; the palette in the previews is
  **parsed out of this file**, not retyped, so it cannot drift.
- `firmware/tools/preview.py` (head + docstring) — the one-shared-tool rule and the "host render, not a
  screenshot, invented fixture text" convention the Tanmatsu tool copies.
- Real L2 screenshots, looked at as images: `docs/shots/2026-11-11-nodes.png`, `-settings.png`,
  `-selector.png`. These are the look authority; the Launcher card/row/ring/bars copy their structure.
- `ui-mock/index.html` — the `.dev-tan` block (800×480 form factor: status 26, sidebar 150, list 280,
  hints 20, detail 18/16, `.row.sel` gold outline) and the palette custom properties.
- `pocket-mesh/research-2026-11-11.md` § Tanmatsu shape (S497) and § 7 Tanmatsu.
- `pocket-mesh/pocket-mesh.md` (Tanmatsu rows, the S459 stack spike, the 2026-11-13 arrival entry) —
  this is where the only recorded build facts for the Tanmatsu port live.
- `cyberdeck/launcher/app.py` + `templates/index.html` — the CD launcher: a grid of cards, each with a
  **status dot**, a label and a **stop ×**, disabled cards for unavailable apps, and one "release" bar for
  the shared resource (the RTL-SDR). That bar is the model for our "the core owns the radio" line.

### What could NOT be checked (honest list)
- **`/home/slofi/dev/wadamesh` does not exist on this machine.** `ls ~/dev` shows only `cockpit-harness`;
  `find` over `~` finds no `TANMATSU_PORT.md`, `TANMATSU_SIDELOAD.md` or `variants/tanmatsu/`. The
  S459 spike's clone was in `/tmp/pm-spike/wadamesh` and `/tmp/pm-spike` is gone. So **step 0's build,
  sideload and key-code sections are written from what was already recorded in `pocket-mesh.md`, not from
  a fresh read of WadaMesh**, and every unverified item is marked `[unverified]` in `PLAN.md`. This is
  the one real gap in this run: the brief says "read it, do not assume SPI from the P4" and I could not.
- **Key codes for the five symbol keys: unknown.** Nothing in the notes records them (grepped
  `keycode|key code|hid key|keyboard matrix`). `PLAN.md` therefore makes logging them on the glass —
  in work-order step 2 — the source of truth, with one table in one header as the brief demands.
- No device was opened, no serial port was touched (opening one can reset an ESP32), no server was
  queried for this ticket — nothing on the servers was needed.
- `[unverified]` also covers: the exact AppFS package/sideload command, whether the stock launcher can
  start a default app at boot, and the LoRa bridge's host interface.

### What was produced
- `docs/PLAN.md` — module/core structure, L2 reuse table (file by file, include-vs-copy per file), build +
  AppFS sideload method with its `[unverified]` marks, the five-symbol-key + arrows input plan, the
  recovery path, 11 open questions split into "for Filip" and "for Claude".
- `tools/preview_tanmatsu.py` — the ONE Tanmatsu preview tool (same spirit as `firmware/tools/preview.py`:
  **palette parsed out of `theme.h`**, not retyped; metrics in one `TM` table; invented fixture text only).
  Renderers: `launcher`, `nodes`, `chat`. `PREVIEW_OUT=` still writes elsewhere. Run: exit 0.
- `shots/` — three host renders at **800×480** (verified 800x480 RGB):
  - `2026-11-13-launcher-home.png` — **the step-1 deliverable**: Launcher home in the L2 OM Lite clothes.
    Status bar (clock, radio 868, battery, gear) → page header → module list (`RUNNING` / `STOPPED` /
    `LATER` chips, gold 2 px focus ring on the selected row) + detail pane (state, PSRAM, tasks, what the
    module owns, history) → `↵ Open` / `s Stop` → the stop rule in words, and the key-hint bar whose right
    cluster is the five symbol keys, current page gold.
  - `2026-11-13-nodes.png` — list + detail two-pane; the "expanded" step: hops + SNR + last heard per row,
    gold scroll thumb (EST-414 rule), detail pane carries the Info page.
  - `2026-11-13-chat.png` — chat list (unread dots/badges, gold ring) + open chat + composer with the gold
    caret and **no on-screen keyboard** (the physical keyboard types into it).
  **Not rendered** (time): Map, RPTR, Settings.
- **Rule 9 fix during the render:** the first pass used node names taken from the L2 screenshots
  (`ERA-3 (SI)`, `SI-KP-Koper`, …). Those are **other people's real infrastructure**, so every node,
  channel, key and message in the previews is now invented (`TEST-A1`, `RPT-Sever`, `#test-a`, `a1 b2 c3`).
  The tool's docstring says so next to the fixtures.

### Look decisions worth Filip's eye (all reversible in one table)
- The mock's **sidebar of icon buttons is gone** (BRIEF § 2: those are wrong); page switching is the five
  physical symbol keys, shown as **vector sigils** in the hint bar — a font may not have ☁, so nothing is a
  glyph. The triangle key is gold on the Launcher page because that is where you are.
- The **status bar has no GPS**: the device has none. The satellite slot is drawn only when an external
  module is present (the L2's own "icons only when on" rule). Radio `868` + a state dot took its place.
- Sizes: status 26 / hint 24 / row 64 / page title 22 / row name 16-17 / body 13 / meta 12 mono /
  section labels 11 mono caps. These follow the mock's `.dev-tan` block (approved form factor) nudged up
  for a 3.97" 201 dpi panel; they are **design intent until the VLW faces exist** (`mkvlw.py`).

### Files, in the layout they belong in
```
paperclip-pilot/sweep/est424-tanmatsu/
  docs/PLAN.md  docs/PROGRESS.md
  tools/preview_tanmatsu.py
  shots/2026-11-13-{launcher-home,nodes,chat}.png
```
Copy-in for Claude (nothing was written into `Projects`, the firmware tree or the L2):

    cp -r paperclip-pilot/sweep/est424-tanmatsu/docs/*  Projects/pocket-mesh/tanmatsu/docs/
    cp -r paperclip-pilot/sweep/est424-tanmatsu/tools     Projects/pocket-mesh/tanmatsu/
    cp -r paperclip-pilot/sweep/est424-tanmatsu/shots     Projects/pocket-mesh/tanmatsu/

(The brief's § 4 says `pocket-mesh/tanmatsu-shots/`; the ticket says `tanmatsu/shots/`. I followed the
ticket and kept the previews next to the app they belong to — if Filip wants the brief's folder, it is a
move, and the tool's `OUT_DIR` is one line.)

### Next step
Filip looks at `shots/2026-11-13-launcher-home.png` and says whether the look is the L2's. If yes:
work-order step 2 (bring-up "hello"), whose first job is the on-glass key log that fills the symbol-key
table. Before any of that, Claude should move these files into `tanmatsu/` and get the WadaMesh
`TANMATSU_PORT.md` / `TANMATSU_SIDELOAD.md` / `variants/tanmatsu` back on disk — the plan's build and
sideload sections need a real read before step 2 can be trusted.
