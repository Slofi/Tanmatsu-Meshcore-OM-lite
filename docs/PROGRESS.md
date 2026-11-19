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
