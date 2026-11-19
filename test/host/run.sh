#!/bin/sh
# Host tests for the Tanmatsu app core. Real exit code: non-zero on any failure.
# Needs the system C++ compiler, libmbedtls-dev (the MeshCore channel crypto uses mbedTLS's PSA API, the
# same library the ESP-IDF provides on the device) and a C compiler for the vendored Ed25519 sources.
# Same spirit as the L2's firmware/test/host/run.sh (system g++, -Wall -Wextra -Werror).
set -eu
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CC="${CC:-gcc}"
CORE="$(mktemp /tmp/tm-core.XXXXXX)"
MC="$(mktemp /tmp/tm-mc.XXXXXX)"
CLK="$(mktemp /tmp/tm-clock.XXXXXX)"
STORE="$(mktemp /tmp/tm-store.XXXXXX)"
HIST="$(mktemp /tmp/tm-history.XXXXXX)"
LAYOUT="$(mktemp /tmp/tm-layout.XXXXXX)"
MAP="$(mktemp /tmp/tm-map.XXXXXX)"
TXQ="$(mktemp /tmp/tm-txq.XXXXXX)"
CARDS="$(mktemp /tmp/tm-cards.XXXXXX)"
MENU="$(mktemp /tmp/tm-menu.XXXXXX)"
DM="$(mktemp /tmp/tm-dm.XXXXXX)"
ADV="$(mktemp /tmp/tm-advert.XXXXXX)"
MCASAN="$(mktemp /tmp/tm-mc-asan.XXXXXX)"
DMASAN="$(mktemp /tmp/tm-dm-asan.XXXXXX)"
DMSEND="$(mktemp /tmp/tm-dm-send.XXXXXX)"
DMSENDASAN="$(mktemp /tmp/tm-dm-send-asan.XXXXXX)"
CONTACT="$(mktemp /tmp/tm-contact-blob.XXXXXX)"
SCOPE="$(mktemp /tmp/tm-channel-scope.XXXXXX)"
GUARD="$(mktemp /tmp/tm-dm-guard.XXXXXX)"
SET="$(mktemp /tmp/tm-settings.XXXXXX)"
CHANADMIN="$(mktemp /tmp/tm-channel-admin.XXXXXX)"
NODESMAP="$(mktemp /tmp/tm-nodes-map.XXXXXX)"
NODESSEARCH="$(mktemp /tmp/tm-nodes-search.XXXXXX)"
NODEMENU="$(mktemp /tmp/tm-node-menu.XXXXXX)"
SOUND="$(mktemp /tmp/tm-sound-events.XXXXXX)"
RPTR="$(mktemp /tmp/tm-rptr.XXXXXX)"
RPTRASAN="$(mktemp /tmp/tm-rptr-asan.XXXXXX)"
NODEOPS="$(mktemp /tmp/tm-node-ops.XXXXXX)"
NODEOPSASAN="$(mktemp /tmp/tm-node-ops-asan.XXXXXX)"
EMOJI="$(mktemp /tmp/tm-emoji.XXXXXX)"
NODESFRESH="$(mktemp /tmp/tm-nodes-fresh.XXXXXX)"
NODESFRESHASAN="$(mktemp /tmp/tm-nodes-fresh-asan.XXXXXX)"
RPTRED="$(mktemp -d /tmp/tm-rptr-ed.XXXXXX)"
CHANORACLE="$(mktemp -d /tmp/tm-chan-oracle.XXXXXX)"
ED="$(mktemp -d /tmp/tm-ed25519.XXXXXX)"
EDASAN="$(mktemp -d /tmp/tm-ed25519-asan.XXXXXX)"
trap 'rm -f "$CORE" "$MC" "$CLK" "$STORE" "$HIST" "$LAYOUT" "$MAP" "$TXQ" "$CARDS" "$MENU" "$DM" "$ADV" "$MCASAN" "$DMASAN" "$DMSEND" "$DMSENDASAN" "$CONTACT" "$SCOPE" "$GUARD" "$SET" "$CHANADMIN" "$NODESMAP" "$NODESSEARCH" "$NODEMENU" "$SOUND" "$RPTR" "$RPTRASAN" "$NODEOPS" "$NODEOPSASAN" "$EMOJI" "$NODESFRESH" "$NODESFRESHASAN"; rm -rf "$ED" "$EDASAN" "$RPTRED" "$CHANORACLE"' EXIT

# -O2 and -Werror matter: the device build is stricter, so a host build must not be looser.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_core.cpp \
    ../../core/alloc.cpp ../../core/registry.cpp ../../core/router.cpp ../../core/core.cpp \
    ../../core/radio_plan.cpp \
    -o "$CORE"
"$CORE"

# The clock's rules (S502): boundaries only, because a wrong decision there fails silently on the mesh.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_clock.cpp \
    ../../core/clock.cpp \
    -o "$CLK"
"$CLK"

# The store the screens read (S502, step 4): contacts, the message ring, unread, ages.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_store.cpp \
    ../../core/mc_store.cpp \
    -o "$STORE"
"$STORE"

# The card history (S502, step 5): our mapping onto the L2's record format, and the torn-tail rule.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core     test_history.cpp     ../../core/mc_history.cpp ../../core/mc_store.cpp     -o "$HIST"
"$HIST"

# The chat layout (S503): wrapping into lines, the visible window, and the seen-point read prefix. A fake 10 px
# font goes through the same measure callback the device's real font uses.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_layout.cpp \
    ../../core/chat_layout.cpp \
    -o "$LAYOUT"
"$LAYOUT"

# Ed25519 (vendored from firmware/lib/meshcore_ed25519, third-party C): compiled as C, WITHOUT -Werror —
# it is not our code and we do not patch it, so its warnings must not gate our suite.
for f in ../../core/ed25519/*.c; do
    "$CC" -O2 -c "$f" -o "$ED/$(basename "$f" .c).o"
done

# The MeshCore wire codec + channels + identity: checked against independent (Python) implementations.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../core/ed25519 \
    test_mc.cpp \
    ../../core/mc_wire.cpp ../../core/mc_channels.cpp ../../core/mc_crypto_psa.cpp \
    ../../core/mc_identity.cpp \
    "$ED"/*.o \
    -lmbedcrypto \
    -o "$MC"
"$MC"

# Direct messages (S513/EST-433): open a TXT_MSG against a contact list and build the ACK / path return. Every
# expected frame comes from the independent Python oracle quoted in test_dm.cpp, so this compares two
# implementations, not one with itself.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../core/ed25519 \
    test_dm.cpp \
    ../../core/mc_dm.cpp ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp \
    ../../core/mc_identity.cpp \
    "$ED"/*.o \
    -lmbedcrypto \
    -o "$DM"
"$DM"

# The SAME MeshCore suite under AddressSanitizer + UBSan (S513/EST-427). The advert parser copies lengths taken
# straight off the air, so a bounds mistake there is a stack overflow reachable by any transmitter — this stage
# turns that class into a red test instead of a field report. -O1 -g keeps the report's line numbers useful; the
# vendored Ed25519 C is built with the same flags so the two objects agree on the instrumented ABI.
for f in ../../core/ed25519/*.c; do
    "$CC" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -c "$f" -o "$EDASAN/$(basename "$f" .c).o"
done
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I ../../core -I ../../core/ed25519 \
    test_mc.cpp \
    ../../core/mc_wire.cpp ../../core/mc_channels.cpp ../../core/mc_crypto_psa.cpp \
    ../../core/mc_identity.cpp \
    "$EDASAN"/*.o \
    -lmbedcrypto \
    -o "$MCASAN"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$MCASAN"

# The DM parser under the same sanitizers. Its lengths come straight off the air too: the 186-byte junk TXT_MSG
# case here is the S513 advert-overflow lesson applied to the DM path.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I ../../core -I ../../core/ed25519 \
    test_dm.cpp \
    ../../core/mc_dm.cpp ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp \
    ../../core/mc_identity.cpp \
    "$EDASAN"/*.o \
    -lmbedcrypto \
    -o "$DMASAN"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$DMASAN"

# DM SEND (S513/EST-436): compose an attempt (against the SAME Python oracle vectors test_dm.cpp pins), open a
# PATH packet and its ACK extras, match the ACK against the pending table, and the retry rule at exact instants.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../core/ed25519 \
    test_dm_send.cpp \
    ../../core/dm_send.cpp ../../core/mc_dm.cpp ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp \
    ../../core/mc_identity.cpp \
    "$ED"/*.o \
    -lmbedcrypto \
    -o "$DMSEND"
"$DMSEND"

# The PATH/ACK parser under the sanitizers: every length in a PATH packet came off the air.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I ../../core -I ../../core/ed25519 \
    test_dm_send.cpp \
    ../../core/dm_send.cpp ../../core/mc_dm.cpp ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp \
    ../../core/mc_identity.cpp \
    "$EDASAN"/*.o \
    -lmbedcrypto \
    -o "$DMSENDASAN"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$DMSENDASAN"

# The map pack rules (S503, step 6): record parse, binary search over the index, zoom -> pack, slippy maths.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_map.cpp \
    ../../core/map_pack.cpp ../../core/map_view.cpp \
    -lm \
    -o "$MAP"
"$MAP"

# The send queue's rules (S513): four accepted, the fifth refused, FIFO, a copy not a pointer, and an advert that
# never displaces a text. The device wraps this same ring in a mutex, so this is the code that runs there.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_txqueue.cpp \
    ../../core/tx_queue.cpp \
    -o "$TXQ"
"$TXQ"

# The Chat list's two-column card grid (S513/EST-430): the rects, the focus moves (including the empty DM column
# refusing focus) and the scroll that keeps the focused card visible. Pure arithmetic, so the panel's own numbers
# (374 px columns, 16 px gutter, 68 px cards, four per column) are pinned here instead of on the glass.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_chat_cards.cpp \
    ../../core/chat_cards.cpp \
    -o "$CARDS"
"$CARDS"

# The start menu's rules (S513/EST-432): three app cards, focus clamped at both ends, Enter on an available
# card vs a LATER one, and s only on a running card. The panel has no second area any more (the S499 two-area
# rule is retired), so these three decisions are the whole key behaviour worth pinning.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_menu_cards.cpp \
    ../../core/menu_cards.cpp \
    -o "$MENU"
"$MENU"

# Settings (EST-445, per-app tabs S514/EST-448): the node-name rule (1..31 bytes, MeshCore's allowed characters,
# trimming), the automatic-advert mapping (Off/1/3/6/12 h; Off means NO scheduled advert while the manual 'a' path
# still works), the tab order DERIVED from the registry (General first, then the menu's order; a LATER module's tab
# present), the module hook (a stopped module still answers; nullptr = the one honest line), the `,` target (right
# tab + section), the frame's key arithmetic (tabs clamped, focus skipping section headers, the scroll keeping focus
# visible, Esc's two levels), NeedsRunning, and the shared confirm sheet's Cancel/Apply rule. registry.cpp is linked
# for the tab list; alloc.cpp is what registry.cpp counts through.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_settings.cpp \
    ../../core/advert_rule.cpp ../../core/menu_cards.cpp ../../core/registry.cpp ../../core/alloc.cpp \
    -o "$SET"
"$SET"

# Channel administration (S514/EST-446): the L2's hashtag normalisation, the key sha256(tag)[0..15] checked
# against Python's hashlib (the oracle is GENERATED here, so the expected keys are an independent
# implementation and no real channel key is ever in the tree), the base64 decoder's good/short/bad-character
# cases, duplicate refusal, the NVS rewrite order cut after EVERY step, and remove + compaction + scope cleanup
# by the LIVE hash. The oracle file lands in a temp dir and is removed with the binaries.
python3 - "$CHANORACLE/channel_vectors.h" <<'PYORACLE'
import base64, hashlib, sys
vecs = [("Slovenija", "#slovenija"), ("si", "#si"), ("#EUROPE", "#europe"),
        ("SLO venija", "#slovenija"), ("test-a", "#test-a")]
with open(sys.argv[1], "w") as f:
    f.write("// generated by test/host/run.sh from Python's hashlib/base64 - do not commit\n")
    f.write("#pragma once\n#include <stddef.h>\n")
    f.write("struct ChannelVector { const char* typed; const char* tag; const char* key16_hex; };\n")
    f.write("static constexpr int kVectorsCount = %d;\n" % len(vecs))
    f.write("static const ChannelVector kVectors[] = {\n")
    for typed, tag in vecs:
        f.write('  {"%s", "%s", "%s"},\n' % (typed, tag, hashlib.sha256(tag.encode()).digest()[:16].hex()))
    f.write("};\n")
    f.write('static const char kKey16B64[] = "%s";\n' % base64.b64encode(bytes(range(1, 17))).decode())
PYORACLE
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I "$CHANORACLE" \
    test_channel_admin.cpp \
    ../../core/channel_admin.cpp ../../core/mc_channels.cpp ../../core/mc_wire.cpp \
    ../../core/mc_crypto_psa.cpp ../../core/channel_scope.cpp \
    -lmbedcrypto \
    -o "$CHANADMIN"
"$CHANADMIN"

# The advert rate rule (S513/EST-435): ONE advert per 5 s. What is pinned is the case that flooded the mesh — five
# presses in two seconds must produce one transmission and four refusals with the whole seconds left — plus the
# queued-advert refusal, the failed-send case and the 32-bit clock wrap.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_advert.cpp \
    ../../core/advert_rule.cpp \
    -o "$ADV"
"$ADV"

# The contact table's NVS blob (S513/EST-437): round trip of every field, the wrong version, the wrong record
# width, EVERY truncation of a two-row blob, extra bytes, an over-large count, and the full 32-row table. The
# device writes and reads this exact codec, so a lost field here is a contact that comes back subtly wrong.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_contact_blob.cpp \
    ../../core/contact_blob.cpp \
    -o "$CONTACT"
"$CONTACT"

# The per-channel flood scope (S513/EST-440): the modes and the region name rules, the resolution
# channel -> node default -> unscoped, the sha256 region key and MeshCore's HMAC transport code against
# vectors from an independent Python oracle, and the TRANSPORT_FLOOD frame byte for byte (the decoder
# already skips those 4 bytes on RX, so the same test opens the frame it built).
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_channel_scope.cpp \
    ../../core/channel_scope.cpp ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp \
    -lmbedcrypto \
    -o "$SCOPE"
"$SCOPE"

# The two DM defects found ON THE AIR (S513/EST-441). A restored thread is a THREAD, not a contact (a contact is
# born from a verified advert); a prefix-only record in the NVS blob is dropped on load; queueDm's peer must have a
# full key; and a frame WE transmitted is dropped when it comes back — also when a repeater added a path hash to it,
# and also when our pub_key[0] equals the peer's. The last three cases are the v41 log line for line, on real
# Ed25519 keys and the real DM codec.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../core/ed25519 \
    test_dm_guard.cpp \
    ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp ../../core/mc_store.cpp \
    ../../core/mc_history.cpp ../../core/contact_blob.cpp ../../core/mc_identity.cpp \
    ../../core/mc_dm.cpp ../../core/dm_send.cpp \
    "$ED"/*.o \
    -lmbedcrypto \
    -o "$GUARD"
"$GUARD"

# NODES + MAP settings (S514/EST-447): remove one contact (compaction, the vacated row, an out-of-range row),
# the mcct blob rewritten WITHOUT the removed contact and decoded back, the clear-all's empty loadable blob, the
# new-node notice's default (only a stored 0 is Off), the map start-position choice, the 12..17 start zoom, the
# last-position write rule at exact instants (including the 32-bit wrap), and a saved view's centre at a new
# start zoom. The rules are headers; contact_blob/mc_store are linked so the round trip is the real codec.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_nodes_map.cpp \
    ../../core/contact_blob.cpp ../../core/mc_store.cpp \
    -o "$NODESMAP"
"$NODESMAP"

# NODES SEARCH (S514/EST-449): the matcher (case, substring, UTF-8 name, empty query = all), the "N of M"
# text, the filtered order over the page's own order, the UTF-8-safe typing, the focus moves (Up from the first
# row -> bar, Down from the bar -> first match, Esc twice) and 'a' being the advert key only on the LIST. The
# rule is a header; mc_store is linked so the filter walks the real contact table the page walks.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_nodes_search.cpp \
    ../../core/mc_store.cpp \
    -o "$NODESSEARCH"
"$NODESSEARCH"

# NODE MENU + FAVOURITES (S514/EST-455): the rows each node type gets (client DM only; Ping/Trace/Map/Reset never
# offered), the favourite value/accent, the focus clamp and the action Enter runs, the notice's exact words, the
# favourites-first order with STABLE ties, the EST-449 filter keeping that order, the focus-follows-the-node rule,
# and the flag through the REAL `mcct` blob codec. The rules are headers; mc_store/contact_blob are the device's.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_node_menu.cpp \
    ../../core/mc_store.cpp ../../core/contact_blob.cpp \
    -o "$NODEMENU"
"$NODEMENU"

# RPTR part 1 (S514/EST-453): the four packets ported from upstream (scan, ANON_REQ login, REQ status and its
# RESPONSE), the found list, the login state machine + deadline, the 56-byte RepeaterStats decode and every unit
# on the page. Expected frames come from an independent Python oracle quoted in test_rptr.cpp; the parsers then
# run under AddressSanitizer with a 4000-round junk pass (every length there came off the air).
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../core/ed25519 \
    test_rptr.cpp \
    ../../core/rptr_model.cpp ../../core/rptr_wire.cpp \
    ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp ../../core/mc_identity.cpp \
    "$ED"/*.o \
    -lmbedcrypto \
    -o "$RPTR"
"$RPTR"

# The vendored Ed25519 C is built with ASan ONLY here: its own UBSan reports (left shifts of negative
# values, present since S501) are not our code and would bury this parser stage in noise. Our files still get
# both sanitizers, which is what the junk pass is for.
for f in ../../core/ed25519/*.c; do
    "$CC" -O1 -g -fsanitize=address -fno-omit-frame-pointer -c "$f" -o "$RPTRED/$(basename "$f" .c).o"
done
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I ../../core -I ../../core/ed25519 \
    test_rptr.cpp \
    ../../core/rptr_model.cpp ../../core/rptr_wire.cpp \
    ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp ../../core/mc_identity.cpp \
    "$RPTRED"/*.o \
    -lmbedcrypto \
    -o "$RPTRASAN"
ASAN_OPTIONS=detect_leaks=1 "$RPTRASAN"

# PING + TRACE (S514/EST-456): the L2's two node actions. The TRACE frame's expected bytes are upstream's
# createTrace()/sendDirect() (Mesh.cpp:605-620, 692-714), the ping's frame is checked to BE the EST-453 status
# request; the trace answer parser then runs under ASan/UBSan with a junk pass, because every length it reads
# came off the air.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../core/ed25519 \
    test_node_ops.cpp \
    ../../core/node_ops.cpp ../../core/rptr_model.cpp ../../core/rptr_wire.cpp \
    ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp ../../core/mc_identity.cpp \
    "$ED"/*.o \
    -lmbedcrypto \
    -o "$NODEOPS"
"$NODEOPS"

"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I ../../core -I ../../core/ed25519 \
    test_node_ops.cpp \
    ../../core/node_ops.cpp ../../core/rptr_model.cpp ../../core/rptr_wire.cpp \
    ../../core/mc_wire.cpp ../../core/mc_crypto_psa.cpp ../../core/mc_identity.cpp \
    "$RPTRED"/*.o \
    -lmbedcrypto \
    -o "$NODEOPSASAN"
ASAN_OPTIONS=detect_leaks=1 "$NODEOPSASAN"

# SOUNDS (S514/EST-450): the event -> L2 default mapping (including Boot Off here vs the L2's Bell
# TK-TK), the 400 ms rate gate at exact instants and across the 32-bit wrap, the echo/duplicate/restored
# refusal, the settings byte round trip and the L2's kRedirect for a retired/unknown id. It links the
# L2's OWN ../../../firmware/src/sound_synth.cpp — the same file the device component compiles — so the
# default ids and the redirect table are the real ones. The waveforms are already covered by the L2's
# firmware/test/host/test_est369.cpp and are not duplicated here.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_sound_events.cpp \
    ../../../firmware/src/sound_synth.cpp \
    -o "$SOUND"
"$SOUND"

# EMOJI (S514/EST-457): the SUBSET blob read by the L2's OWN parser (firmware/src/ui/emoji_table.h) and the L2's
# OWN UTF-8/emoji walker (firmware/src/ui/text_wrap.cpp, compiled here and into the core component). Pins: the blob
# under the 120 KiB cap, sorted/4-aligned/in-range/with-ink entries, lookups in and out of the subset, the walker on
# ZWJ / variation selector / skin tone / keycap, and the chat wrap with emoji at their DRAWN 26 px — no line wider
# than its width, and a code point the subset lacks keeping today's half-em advance.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core -I ../../../firmware/src \
    test_emoji.cpp \
    ../../core/emoji.cpp ../../core/chat_layout.cpp \
    ../../../firmware/src/ui/text_wrap.cpp \
    -o "$EMOJI"
"$EMOJI"

# NEW CONTACTS + DOTS + SCREEN OFF (S515/EST-461): the hint bar's dot mask from the SAME counters the chat
# list's badges use, the favourites -> fresh -> rest order, the ack rules (a key landing, a 1.5 s rest, page
# entry alone), the FROZEN order while Nodes is open (appended on arrival, re-sorted on entry, still filtered
# by the search) and the power button's screen state machine (press toggles, release ignored, a key while off
# is swallowed and the screen stays off, only the power button wakes it (Filip, S515), a message does not wake). The rules are headers; mc_store is the device's own.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_nodes_fresh.cpp \
    ../../core/mc_store.cpp \
    -o "$NODESFRESH"
"$NODESFRESH"

# …and the same suite under ASan/UBSan: the frozen order copies indices out of a 32-row table, and the blob's
# version-3 tail is read at an offset derived from the header.
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I ../../core \
    test_nodes_fresh.cpp \
    ../../core/mc_store.cpp \
    -o "$NODESFRESHASAN"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$NODESFRESHASAN"

# The subset blob must be exactly what tools/mkemoji_subset.py builds from the L2's table: a stale .bin (edited by
# hand, or left behind after the L2's blob changed) fails here, not on the glass.
python3 ../../tools/mkemoji_subset.py --check
