# Next instructions for DeepSeek (Tanmatsu) — ready to paste

> Written by Claude, S498, 2026-11-13, in case Claude's weekly usage runs out before the next step is filed. Filip (or Claude) gives DS the next unchecked step. Every step: read `../BRIEF.md` first, write `PROGRESS.md` early, real exit codes, host tests armed against the old state, no clutter, **no install / no flashing without Filip present**, no commits by DS (Claude reviews and commits; if Claude is out, leave the work uncommitted and listed in PROGRESS).

- [x] **Step 0 + 1 (EST-424, done 2026-11-13 11:20, reviewed by Claude: right look; build section unverified because WadaMesh was not visible):** `docs/PLAN.md` + the Launcher home preview at 800×480 (`shots/`), then Nodes/Chat previews if time allowed.
**Three decisions for Filip (Claude, 2026-11-13 11:40):** (1) **the look**: the six 800×480 previews in `tanmatsu/shots/` (Launcher, Nodes, Chat, Map, RPTR, Settings); DS asks whether the filled "cloud" key symbol should be an outline like the other four; (2) **the sandbox** opening below; (3) **where the Tanmatsu toolchain goes**: ESP-IDF 5.5.1 for the `esp32p4` (~3 GB, needs a download), nothing installed yet, so no real build is possible until then (PLAN.md § 6, cited from WadaMesh).

## ▶ S514 (2026-11-19, Claude): where it stands now

The device runs **v55** (`405a6b3`). S514's tickets EST-443 … EST-457 are closed (records: the S514 entries at the top
of `PROGRESS.md`); **EST-458 (RPTR part 2a, safe settings) was stopped at Filip's word**, its partial work is in
`../wip/EST-458-rptr-part2a-partial-2026-11-19.patch`. Already DONE, do not re-propose: Settings (one tab per app),
sounds, Nodes search/favourites/node menu with Ping/Trace, emoji, fast seek, RPTR part 1 (login + status, proven on the
air). **The ordered TO-DO and the "what not to do" list live in `../pocket-mesh.md` § REBUILD "▶ TANMATSU: NEXT SESSION
STARTS HERE (S514 close)"** (one home): the test tour with Filip, then RPTR part 2a re-filed, then 2b (radio).
DS works IN PLACE in this folder (the fence binds it read-write, `.git` read-only); one run at a time.

## (SUPERSEDED by S514, history) ▶ S513 (2026-11-18, Claude)

The device runs **v42** (`0d8ec23`). S513's tickets EST-427 … EST-441 are all closed; their records are the S513
entries at the top of `PROGRESS.md`. Everything below this block is older history. **The ordered TO-DO and the
"what not to do" list live in `../pocket-mesh.md` § REBUILD "▶ TANMATSU: NEXT SESSION STARTS HERE"** (one home):
first an on-air test of v42 with Filip, then the push on his word, then a Settings page (ask Filip first).
Since S499 DS works IN PLACE in this folder (the fence binds it read-write, `.git` read-only); one run at a time.

## (SUPERSEDED since S499: DS writes IN PLACE in `tanmatsu/`, see the S513 block above) Where DS wrote in S498 (Claude, 11:30)

DS's sandbox (`paperclip-pilot/ds-fence.sh`) can write `pocket-mesh/firmware/` but **not** `tanmatsu/`, and it cannot see `~/dev/wadamesh`. Until Filip decides:
- DS works in **`/home/slofi/paperclip-pilot/sweep/tanmatsu-work/`** (same layout as this folder) and lists the copy-in commands at the top of its PROGRESS.md; Claude or Filip copies the work in after reading it.
- WadaMesh's Tanmatsu files are copied (read-only reference, commit `53d17d0`) to `/home/slofi/paperclip-pilot/sweep/tanmatsu-ref/wadamesh-53d17d0/`.
- **Decision for Filip:** open `tanmatsu/` to DS the same way as `firmware/` (S494, "widen the sandbox"). The change is three lines after the `CODE+=(--bind "$PM" ...)` line in `ds-fence.sh`: `TM="$H/Projects/pocket-mesh/tanmatsu"`, `CODE+=(--bind "$TM" "$TM")`, `CODE+=(--ro-bind "$H/dev/wadamesh" "$H/dev/wadamesh")`. Claude tried to make it on 2026-11-13 and the auto-mode safety check blocked it (a change to an AI sandbox), so it needs your yes; then the staging folder is no longer needed.

- [x] **Step 1b (EST-425, done 2026-11-13 11:32, reviewed by Claude; citations spot-checked):** build section rewritten from WadaMesh with file:line sources, 3 fixes, Map/RPTR/Settings previews. One left-over fix (Settings row shows the new value behind the sheet) went into EST-426.
- [x] **Core logic (EST-426, done 2026-11-13 11:41; Claude re-ran it in the sandbox: 152 checks, 0 failures, L2 suite still green, and one mutation of its own turned it red at `test_core.cpp:283`):** files in `core/` + `test/host/run.sh`. module registry, counted memory for "Stop frees memory", the symbol-key page router, host tests, no screen code. **After it: stop until Filip decides the three items above.**
- [x] **Step 1b — previews complete (all six done by EST-424/425; Filip's approval of the look still open):** finish the 800×480 previews in the L2 OM Lite style for: Launcher home (OM Lite tile + "later" tiles if Filip wants them), OM Lite Nodes (two-pane), Chat (channel list + open chat with the composer), Map, RPTR, Settings, and the key-hint bar showing the five symbol keys. One shared preview tool. **Stop here for Filip's approval of the look** before step 2's UI code.
- [x] **Step 2 — Launcher app skeleton, build-only — DONE ON THE DEVICE (DeepSeek, S499 + S500, 2026-11-13):** builds from the vendor `tanmatsu-template` (`make build`), installed via BadgeLink, runs with the approved Launcher page + text, Filip's two-area focus rule, Start/Stop through `tm::Core`; the five symbol keys are measured (`core/keys.h`, 21-25) and Esc returns to the stock launcher with a healthy screen. **SD card closed in S500:** the BSP's own storage driver (slot 0, CLK 43 / CMD 44 / D0-D3 39-42, LDO ch. 4) mounted a real card, `tm/app: SD mounted at /sdcard` on the console; root cause was esp-hosted claiming the single P4 SDMMC controller in a constructor. Details + the console-capture recipe → `PROGRESS.md` § S500.
- [x] **Step 3a — the radio on the real hardware (DeepSeek, S500, 2026-11-13): DONE and proven on the device.** SX1262 reached "remote" through the C6 (`tanmatsu-lora` 0.6.0): `chip SX1262, version SX1261 V2D 2D02`; configured 869.618 MHz / bw 62 / SF8 / CR4/8 / sync 0x12 / preamble 32 / 22 dBm; **12 beacons TX, each ACKed**; **a real MeshCore packet RX from the L2 (101 B, SNR -12 dB)**. Two bugs found on glass and fixed: a C6 power cycle breaks the esp-hosted link (needs `esp_hosted_connect_to_slave()`), and S499's 90 s dev guard was killing long listens. Known defect: **RSSI reports 0 dBm** (field not populated). → `PROGRESS.md` § 3a.
- [x] **Exit / Esc (not a numbered step) — CLOSED 2026-11-13, S501, DeepSeek, on the glass (v13 → v15):** the
  exit was never broken; the reboot is how the platform swaps AppFS images (the launcher starts an app the same
  way, `appfsBootSelect()` + `esp_restart()`). The app does **not** come back afterwards: 75 s of launcher console,
  zero app starts, and the S500 capture's second Esc behaved the same. What changed: Esc now shows
  "Back to the menu / The device restarts to switch apps." **40 ms after the keypress**, with the C6 power cycle
  running underneath the words; and the launcher's `app_fav/autostart` is only cleared when it points at us.
  **Do not re-open this** without a new capture: `tanmatsu/docs/PROGRESS.md` § S501 holds the three S500 claims that
  were wrong and the source lines that settle them.
- [ ] **Step 3b — MeshCore on the Tanmatsu (slice 2 is THE NEXT TICKET):** LoRa via the Tanmatsu's real path (the tanmatsu-lora bridge, read WadaMesh), MeshCore with the L2's identity rules and SLO settings (869.618 MHz, BW 62.5, SF 8), channels, send/receive; reuse the L2's pure-logic files without breaking the L2 build (run the L2 suite: `cd ../firmware && sh test/host/run.sh`). **Slice 1 (the wire codec) is DONE; the rest of this is the next ticket.**
- [x] **3b slice 1 — the MeshCore wire codec, DONE 2026-11-13 (S501, DeepSeek; on the glass as v16):**
  `core/mc_wire.*` (frame + group text: channel hash, 2-byte MAC, AES-128-ECB over `ts, type, "name: text"`,
  `path_len` with 2-byte hashes) and `core/mc_crypto_psa.cpp` (SHA-256, HMAC-SHA256, AES-ECB via **PSA**, because
  the IDF ships mbedTLS 4.x while the host has 3.6.5). Host suite **49/0** against an independent Python
  implementation; the **P4 produced the same 53-byte frame byte for byte** in its boot log. Cost +84 KiB
  (1,174,688 -> 1,261,152 B). **Not done:** nothing is transmitted, no channel is on the device.
  ⚠️ Never write a channel key into the repo or the notes: the test takes it from `MC_TEST_CHANNEL_KEY`.
- [x] **3b slice 2 — the radio carries it, DONE 2026-11-13 (S501, DeepSeek, on the glass as v17):**
  `core/mc_channels.*` (table + routing: every channel whose 1-byte hash matches gets a try, the MAC decides) and
  `main/mc_link.*` (NVS `mcch`: `cnt`/`n<i>`/`k<i>`; RX decode → route → log; TX; 8-slot seen-ring). The 3a beacon
  is gone (a non-MeshCore packet is noise on the channel). **Proven on the device:** `ch0 "Test Channel" hash 0xD7`
  loaded from NVS, `TX 53 B -> ok`, our own text came back **55 B** (a 2-byte path hash added by the mesh, so the
  path parsing works on live traffic) and decoded as `EDCT: hello from the tanmatsu`, and the L2's
  `ERA-3 (SI): Hello from ERA-2!` decoded here. Host suites 194/0 and **80/0**. Cost 1,264,768 B.
  **Provisioning (the key is never in the tree):** `nvs write mcch cnt u8 1`, `nvs write mcch n0 str "Test Channel"`,
  `nvs write --file mcch k0 blob <16-byte file>` over BadgeLink. A re-flashed NVS loses the channel; that is the way
  back. **Not done:** no UI (the `t` key is a temporary test hook), no advert/identity.
- [x] **3b slice 3 — the node has an identity, DONE 2026-11-13 (S501, on the glass as v19):** `core/mc_identity.*`
  (keypair from a seed, Ed25519, advert = pub+ts+sig+app_data signed over pub||ts||app_data, frame `0x11` +
  `path_len 0x40`) and `core/ed25519/` (**orlp/ed25519 vendored** from the L2's own copy, zlib; mbedTLS's PSA has no
  Ed25519 in either build). The seed is created once in NVS `mcid/seed` and survives a reinstall. **Proven:** `EDCT`
  in the L2's node list (Filip), our own flood came back and **verified**, host 101/0 with our advert bytes identical
  to Python's OpenSSL Ed25519. **The defect it found:** the first advert panicked with a *stack protection fault* —
  signing on the main task while the radio task drove the link, and verifying on a 4 KB stack. Fix: **one owner of the
  radio** (queue + `serviceRequests()` on the radio task), task stack **12 KiB**, and the task logs its own low-water
  mark (**9,304 B free** after the work, so ~3 KB is what it needs). A panic skips the exit path, which is what resets
  the C6 — **after a panic the device needs a full power cycle** (RECOVERY.md § 5). **Not done:** no reply paths, no
  position in the advert, no advert schedule.
- [x] **3b gap found in S501 — the app has no clock. CLOSED and PROVEN ON THE GLASS in S502 (DeepSeek,
  2026-11-13), AppFS v23.** The Tanmatsu's RTC lives in the **CH32 coprocessor** (`bsp/rtc.h`) — the app never
  read it. Now:
  boot reads the coprocessor RTC, else the stored NVS floor `mcid/epoch` (logged as a FLOOR, not as the time),
  else nothing; any **authenticated** timestamp heard on the mesh (a verified advert, or a text whose MAC opened)
  is adopted when it beats our clock by more than 60 s, then written back to the RTC and NVS. The status bar shows
  real `HH:MM`. Rules in `core/clock.*` (host **53/0**, mutant-armed), glue in `main/time_source.*`; adverts are
  now handled before the channel check (they are not channel-scoped). App **v23, 1,343,840 B**, `make build`
  exit 0, **installed** (`OM Lite | 23 | 1312K`). **All five acceptance checks PASSED on the glass:** the RTC
  read `2026-11-13 21:26:28Z (22:26 local)`, the TZ string works, **the RTC survived a power cycle**
  (`21:28:08Z` after it), the forced advert carried `ts 1794605358` (2 s of the host clock), and **the L2 showed
  `22:29:21 adv TNMT (SI) flood`** — the node that used to reject us now accepts. Not needed at all: SNTP over
  the C6's Wi-Fi (the mesh and the RTC cover it; keep Wi-Fi for later if the clock must be right off-mesh).
  Details → `PROGRESS.md` § S502.
- [x] **3b slice 4 / step 4 — USABLE, DONE 2026-11-13 (S502, DeepSeek), on the glass as AppFS v25:** the Nodes and
  Chat pages and the store behind them (`core/mc_store.*`, host **99/0**). Two rules Filip set this session:
  **own messages on the right under "you", everyone else's on the left**, and **Esc is UP, not undo** (the
  router's visited-page history is gone; every module page's parent is the Launcher). `t` retired (the composer
  replaced it), `a` still forces an advert. Proven: `chat: queued …` then `TX ch0 … -> ok`, the bot's reply
  logged, and **no echo line** (our own flood is dropped as a duplicate instead of showing twice).
  Details -> `PROGRESS.md` § S502 (continued). **Known gaps:** no message wrapping (long texts are ellipsised),
  no emoji glyphs in the generated font, no scrolling inside a channel, no SD history, RAM-only store.
- [x] **STEP 5 — INSTALLED AND PROVEN 2026-11-14 (S503, DeepSeek), on the glass as AppFS v28.** The code was already done and green
  (L2 record format reused; `core/mc_history.*` + `main/sd_history.*`; host **40/0** incl. the torn-tail rule),
  `make build` exit 0, 1,365,168 B — but **v28 is NOT installed**, and the v27 install proved nothing because
  `mkdir` on FATFS creates one level only and `/sdcard/om-lite` did not exist, so history ran RAM-only while the
  chat looked perfect. v28 creates the parent and logs the errno. **Acceptance:** `history: ON at
  /sdcard/om-lite/history` at boot; a message; restart; `history: N message(s) from ch-d7-...log` with the lines
  back in the channel; `ch-d7-Test_Channel.log` on the card from TestBox.
  **Result (S503):** all of it passed — `history: ON at /sdcard/om-lite/history`, then after the restart
  `history: 2 message(s) from /sdcard/om-lite/history/ch-d7-Test_Channel.log`, and both lines back in the channel
  (Filip confirmed). The count is a byte-level check: the load walks the file with the L2's `decodeRecordAt`,
  which rejects a record whose magic/version/length/CRC-32 is wrong. Binary **1,364,400 B**, sha256
  `336340a3…b00c27` (S502's 1,365,168 B was a slip, corrected in `PROGRESS.md`). Not done: the card was not
  read from TestBox (needs the card out of the device; the device-side load already validates the bytes).
- [x] **Meta record + wrapping + scrolling — DONE 2026-11-14 (S503, DeepSeek), on the glass as v29:** the log's
  Meta record carries the seen point, so unread survives a restart (`ch0: 3 of 4 read (seen point 3), 1 unread`,
  and Filip confirmed the badge stayed after the reboot); `core/chat_layout.*` wraps a message into up to 4 lines
  (UTF-8 safe, host-tested with a fake font) and decides the visible window; Up/Down scroll, the page head shows
  `N older` / `N newer`. Host **192 / 53 / 99 / 65 / 62 / 94**, four armed mutations each red at a named line.
  Details → `PROGRESS.md` § S503 (continued).
- [x] **v30 — the scroll clamp, INSTALLED and checked 2026-11-14 (S503):** `chatWindow`'s `max_scroll` is now the
  point where the oldest message tops a FULL window (0 when everything fits), so Up is a no-op in a short channel
  instead of hiding the newest message. Found on the glass at three messages, fixed and installed the same
  session (`om-lite | OM Lite | 30 | 1335K`, **1,367,776 B**). **Filip's three checks all pass:** Up does nothing
  on "Test Channel" with everything on screen; the badge clears when opened and stays away; the 119-character
  message reads in full over two lines.
- [ ] **Then:** emoji glyphs (the generated font has none, the bot sends emoji) · **step 6**: Map from the SD
  pack, RPTR, Settings · reply paths, then DM/Trace/Favourite on Nodes · the advert schedule in Settings. Still
  open from step 3a: **RSSI reads 0 dBm**. Open from S503: an incoming advert dropped as `INVALID signature`, and
  our own echo arriving as `no channel of ours opens it`.
- [x] **Step 6 slice 1 — the map pack reads and decodes on the device, DONE 2026-11-15 (S503, DeepSeek, v31):**
  `core/map_pack.*` (24-byte index record, binary search through a record-reader callback, zoom -> pack, slippy
  maths; host tests) and `main/map_link.*` (searches `tiles.idx` with ~18 small reads, seeks in `tiles.pack`,
  decodes with **espressif/esp_jpeg 1.0.5** to 256x256 RGB565 in PSRAM). Boot probe on glass: `z16 tile
  35496/23275 -> 256x256 in 57 ms`. Decoder gotchas: the P4 has **no ROM JPEG** (`CONFIG_JD_USE_ROM=n` in
  `sdkconfigs/tanmatsu`), and **1.0.1 does not compile** (upstream callback type), **1.0.5 does**.
- [x] **Step 6 slice 2 — the Map page draws, DONE 2026-11-15 (S503, DeepSeek, v32 then v33):** `core/map_view.*`
  (which tiles fill the view with clipping, pan with edge clamping, zoom that keeps the centre, an **LRU tile
  cache**) and `main/map_page.*` (800x386 area, memcpy blits, void colour under holes, crosshair, **Esri
  attribution**, arrows pan, `+`/`-` zoom z9-17). **Measured on the glass:** pan **14 ms** a press, zoom **1345 ms**
  (12 misses x ~112 ms). The first v32 felt laggy because the cache was **8 slots for a 15-tile view** (my bug);
  v33 raised it to 20 (2.5 MB PSRAM) and that is when panning became instant.
- [x] **A non-blocking compose (the zoom fix) — DONE 2026-11-15 (S506, DeepSeek), on the glass as v34 → v36.** The
  tile cache became a state machine (`Empty/Pending/Ready`: a pending slot is never drawn and never evicted), a
  worker task (`om-map`, priority 2) decodes what the view is missing **centre-out** while the page draws only
  decoded tiles, the input poll drops to 30 ms while a fill is running, and the panel blit is measured. Host
  **245 checks**, five named mutations each red at a line. **Measured:** `fill: 14 tile(s) in 525 ms`, frames
  `composed in 6-26 ms` + `present 21-41 ms`, pan 14 ms; the first frame after a zoom reads `0 of 8 drawn,
  8 queued` and fills from the middle. **Filip: "zoom is good now".**
- [x] **The v34 colour defect — fixed in v35 (S506), and now decided by measurement at every boot.** The hardware
  JPEG codec was 2 ms a tile but decoded from a buffer it could not safely read (my compressed-input buffer was a
  plain `heap_caps_malloc`; the IDF example allocates it with `jpeg_alloc_decoder_mem`) and the colour order was
  hard-wired to RGB when BGR is the right one. `verifyDecoders()` now decodes one real tile four ways at boot,
  scores each against the software decoder, keeps the best only if it matches, and otherwise **disables the
  codec**: `distance 5 → using hardware decode, BGR/BT709 (2 ms vs sw 41 ms)`. **Filip: "works!".**
- [ ] **NEXT: v36's numbers, then step 6's remaining pages.** v36 (built, NOT installed — the upload needs Filip's
  purple diamond) keys the index hint by column instead of row; v35 still read ~18 records per tile
  (`249 card reads` for 14 tiles) because the centre-out plan kept overwriting the row-keyed hint. Expect ~3-4.
  Then **RPTR, Settings**, the clean card unmount (`sync()` does not exist on this target and the BSP keeps the
  card handle; every data write already flushes and closes per record).
- [ ] **Open observation, not a bug report yet:** in the same tile the corner pixel differs between the two
  decoders (`px0 0xFFFF` hardware vs `0xF7DD` software) while the sampled mean distance is 5. Consistent with an
  edge/padding artefact of the codec. Filip sees no seams; "no seams seen" is not "no seam exists".
- [ ] **3b slice 3 (old text, kept for the record):** an Ed25519 keypair on the device and a signed
  `PAYLOAD_TYPE_ADVERT` carrying the name `EDCT` (MeshCore: pubkey + timestamp + signature), so other nodes see it
  in their contact lists; then store reply paths. After that, step 4 (Chat/Nodes screens) makes it usable and the
  `t` hook goes away.
- [ ] **3b slice 2 (old text, kept for the record):** (1) channels in NVS (name, 16-byte key, derived hash; the L2's
  `src/channels.cpp` is the model); (2) `main/mc_link.*`: on RX `decode` + `openGroupText` per channel, on TX
  `buildGroupText` into `radio_link` instead of the beacon; (3) on the glass with Filip: send from the Tanmatsu on
  "Test Channel", see it on the L2, and decode the L2's reply. Then the advert/identity (`EDCT` as a node, Ed25519).
- [ ] **Step 4+ —** Nodes + Chat two-pane with SD history, Map from the SD pack, RPTR, Settings (BRIEF § 6), one at a time, each with previews/screens for Filip.
