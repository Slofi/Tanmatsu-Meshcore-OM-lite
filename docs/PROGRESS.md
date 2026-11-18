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
