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
ED="$(mktemp -d /tmp/tm-ed25519.XXXXXX)"
EDASAN="$(mktemp -d /tmp/tm-ed25519-asan.XXXXXX)"
trap 'rm -f "$CORE" "$MC" "$CLK" "$STORE" "$HIST" "$LAYOUT" "$MAP" "$TXQ" "$CARDS" "$MENU" "$DM" "$ADV" "$MCASAN" "$DMASAN" "$DMSEND" "$DMSENDASAN" "$CONTACT" "$SCOPE" "$GUARD"; rm -rf "$ED" "$EDASAN"' EXIT

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
