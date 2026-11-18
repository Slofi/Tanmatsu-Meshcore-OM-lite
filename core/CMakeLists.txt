# The app core as an IDF component: the same .cpp files the host tests compile with g++.
# radio_plan.cpp (step 3a) holds the LoRa numbers + the beacon, so the device and the host test share
# ONE copy of them. mc_wire.cpp + mc_crypto_psa.cpp (step 3b) are the MeshCore wire codec: the host test
# compiles these exact files against the system mbedTLS 3.x and the device against the IDF's mbedTLS 4.x,
# through the PSA Crypto API that both provide — one implementation, not two that can agree with each other
# and disagree with the mesh.
idf_component_register(
    SRCS "alloc.cpp" "registry.cpp" "router.cpp" "core.cpp" "radio_plan.cpp"
         "mc_wire.cpp" "mc_channels.cpp" "mc_crypto_psa.cpp" "mc_identity.cpp"
         # The clock's RULES (S502): what time to believe at boot, when a peer's authenticated timestamp wins,
         # and how the advert timestamp is floored. Host-tested, because a wrong decision here fails silently.
         "clock.cpp"
         # The store the pages read (S502, step 4): contacts by public key + the message ring.
         "mc_store.cpp"
         # Chat history records, in the L2's format (S502, step 5).
         "mc_history.cpp"
         # Chat layout (S503): wrapping a message into lines, and which messages fit the screen. Pure arithmetic,
         # so the host tests pin the break points instead of the glass teaching us.
         "chat_layout.cpp"
         # S513/EST-430: the Chat list's two-column card grid — the rects, the focus moves and the scroll that keeps
         # the focused card visible. Pure arithmetic on the page's box, so the host tests pin the panel's numbers.
         "chat_cards.cpp"
         # S513/EST-432: the start menu's three app cards — focus clamped at both ends, Enter on available vs
         # LATER, s only on a running card. The panel's whole key behaviour, host-tested.
         "menu_cards.cpp"
         # The map pack rules (S503, step 6): record layout, binary search, zoom -> pack, tile coordinates.
         "map_pack.cpp"
         # The Map page arithmetic (S503): view tiles, panning, zooming, and the decoded-tile cache.
         "map_view.cpp"
         # S513: the send queue's rules (four request slots, FIFO, refuse when full) — the code the radio task
         # pushes to and pops from, and the code the host tests cover.
         "tx_queue.cpp"
         # Ed25519, vendored from firmware/lib/meshcore_ed25519 (orlp/ed25519, zlib licence): the same code
         # the L2 signs with, so the signatures match by construction rather than by hope.
         "ed25519/add_scalar.c" "ed25519/fe.c" "ed25519/ge.c" "ed25519/key_exchange.c"
         "ed25519/keypair.c" "ed25519/sc.c" "ed25519/seed.c" "ed25519/sha512.c"
         "ed25519/sign.c" "ed25519/verify.c"
    INCLUDE_DIRS "." "ed25519"
    REQUIRES mbedtls
)
