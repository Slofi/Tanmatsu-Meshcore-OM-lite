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
         # S513/EST-433: direct messages — open a TXT_MSG against the contact list and build the ACK / path
         # return. Pure, bounds-checked, host-tested against an independent Python oracle.
         "mc_dm.cpp"
         # S513/EST-436: DM SEND — compose an attempt, open a PATH packet, match the ACK, and the retry rule.
         # Pure: time is an argument and nothing here transmits.
         "dm_send.cpp"
         # S513/EST-435: one advert per 5 s — the gate the key handler and the 3-hour schedule both ask, so a
         # held key cannot flood the mesh. Time is an argument, so the host tests drive exact instants.
         "advert_rule.cpp"
         # S513/EST-437: the contact table's one versioned NVS blob — encode/decode only, so the host tests
         # round-trip the exact bytes the device stores and refuse a wrong version, width or length.
         "contact_blob.cpp"
         # S513/EST-440: per-channel flood scope (ported from the L2's S509) — the modes, the region name,
         # the sha256 region key, MeshCore's HMAC transport code and the transport-flood frame.
         "channel_scope.cpp"
         # S514/EST-446: adding/removing a channel — the L2's hashtag normalisation and base64 rules, the
         # duplicate refusal, and the NVS rewrite ORDER that makes a power cut at any step loadable.
         "channel_admin.cpp"
         # S514/EST-453: RPTR part 1 — the found list / login / status rules and the four upstream packets.
         "rptr_model.cpp" "rptr_wire.cpp"
         # S514/EST-457: the emoji SUBSET table — the L2's own blob layout and parser
         # (firmware/src/ui/emoji_table.h) and the L2's own UTF-8/emoji unit walker
         # (firmware/src/ui/text_wrap.cpp, compiled from that tree like sound_synth.cpp). The one
         # width rule the draw and the chat wrap share lives in emoji.cpp.
         "emoji.cpp" "../../firmware/src/ui/text_wrap.cpp"
         # S514/EST-456: the node menu's Ping and Trace — the L2's status request reused, and upstream's
         # createTrace()/sendDirect() frame + its answer parser. Pure and bounds-checked, host-tested.
         "node_ops.cpp"
         # S514/EST-450: the L2's shared phrase library, compiled from ITS OWN source (never copied or
         # forked), exactly as mc_history uses the L2's history format. core/sound_events.h is the policy;
         # this one file is the synth both devices play.
         "../../firmware/src/sound_synth.cpp"
         # Ed25519, vendored from firmware/lib/meshcore_ed25519 (orlp/ed25519, zlib licence): the same code
         # the L2 signs with, so the signatures match by construction rather than by hope.
         "ed25519/add_scalar.c" "ed25519/fe.c" "ed25519/ge.c" "ed25519/key_exchange.c"
         "ed25519/keypair.c" "ed25519/sc.c" "ed25519/seed.c" "ed25519/sha512.c"
         "ed25519/sign.c" "ed25519/verify.c"
    INCLUDE_DIRS "." "ed25519" "${CMAKE_CURRENT_LIST_DIR}/../../firmware/src"
    REQUIRES mbedtls
)
