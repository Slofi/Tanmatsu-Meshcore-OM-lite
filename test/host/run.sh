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
ED="$(mktemp -d /tmp/tm-ed25519.XXXXXX)"
trap 'rm -f "$CORE" "$MC" "$CLK" "$STORE" "$HIST" "$LAYOUT" "$MAP"; rm -rf "$ED"' EXIT

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

# The map pack rules (S503, step 6): record parse, binary search over the index, zoom -> pack, slippy maths.
g++ -std=c++17 -Wall -Wextra -Werror -O2 -I ../../core \
    test_map.cpp \
    ../../core/map_pack.cpp ../../core/map_view.cpp \
    -lm \
    -o "$MAP"
"$MAP"
