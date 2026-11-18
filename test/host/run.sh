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
MCASAN="$(mktemp /tmp/tm-mc-asan.XXXXXX)"
ED="$(mktemp -d /tmp/tm-ed25519.XXXXXX)"
EDASAN="$(mktemp -d /tmp/tm-ed25519-asan.XXXXXX)"
trap 'rm -f "$CORE" "$MC" "$CLK" "$STORE" "$HIST" "$LAYOUT" "$MAP" "$TXQ" "$CARDS" "$MENU" "$MCASAN"; rm -rf "$ED" "$EDASAN"' EXIT

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
