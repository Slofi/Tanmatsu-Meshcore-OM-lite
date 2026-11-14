// SPDX-License-Identifier: MIT
#include "mc_channels.h"

#include <string.h>

namespace meshcore {

bool channelsAdd(ChannelSet& set, const uint8_t key[kKeyBytes], const char* name) {
    if (set.count >= kMaxChannels) return false;
    if (channelsFindByKey(set, key) >= 0) return false;   // already there: not an error, but nothing to add
    Channel& slot = set.ch[set.count];
    if (!channelSetKey(slot, key, name)) return false;
    set.count++;
    return true;
}

int channelsFindByKey(const ChannelSet& set, const uint8_t key[kKeyBytes]) {
    if (key == nullptr) return -1;
    for (size_t i = 0; i < set.count; i++) {
        if (memcmp(set.ch[i].secret, key, kKeyBytes) == 0) return (int)i;
    }
    return -1;
}

int channelsFindByHash(const ChannelSet& set, uint8_t hash) {
    for (size_t i = 0; i < set.count; i++) {
        if (set.ch[i].hash == hash) return (int)i;
    }
    return -1;
}

int channelsOpen(const ChannelSet& set, const View& pkt, char* sender, size_t sender_size, char* text,
                 size_t text_size, uint32_t* timestamp) {
    if (pkt.type() != kTypeGrpTxt || pkt.payload_len < 1) return -1;
    const uint8_t wire_hash = pkt.payload[0];
    // Every channel whose hash matches gets a try; the 2-byte MAC decides. That is MeshCore's own rule
    // (Mesh.cpp:236-246) and it is what makes a 1-byte hash safe enough to use.
    for (size_t i = 0; i < set.count; i++) {
        if (set.ch[i].hash != wire_hash) continue;
        if (openGroupText(set.ch[i], pkt, sender, sender_size, text, text_size, timestamp)) return (int)i;
    }
    return -1;
}

}  // namespace meshcore
