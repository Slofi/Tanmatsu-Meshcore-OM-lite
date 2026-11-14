// SPDX-License-Identifier: MIT
// The channel table: which channels this device knows, and which one a received packet belongs to.
//
// Portable on purpose (no IDF, no NVS, no radio) so the host test can prove the matching rules:
//   · MeshCore's channel hash is 1 byte and comes from the key, never from the visible name, so two
//     channels can share a hash (the real mesh has 5 channels; a 1-byte hash collides by birthday around
//     16 channels). Matching therefore tries every candidate with that hash and lets the MAC decide,
//     exactly as Mesh::onRecvPacket does (Mesh.cpp:226-248: up to 4 matching hashes, decrypt, first valid wins).
//   · a channel is a 16-byte key; the name is a local label and may be renamed at will.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_wire.h"

namespace meshcore {

constexpr size_t kMaxChannels = 8;

struct ChannelSet {
    Channel ch[kMaxChannels];
    uint8_t count = 0;
};

bool channelsAdd(ChannelSet& set, const uint8_t key[kKeyBytes], const char* name);

// -1 when absent.
int channelsFindByKey(const ChannelSet& set, const uint8_t key[kKeyBytes]);
int channelsFindByHash(const ChannelSet& set, uint8_t hash);

// Which channel can actually open this packet. Returns the channel index, or -1 when none can.
// On success the decoded sender/text/timestamp are returned (NUL-terminated).
int channelsOpen(const ChannelSet& set, const View& pkt, char* sender, size_t sender_size, char* text,
                 size_t text_size, uint32_t* timestamp);

}  // namespace meshcore
