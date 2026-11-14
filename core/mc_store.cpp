// SPDX-License-Identifier: MIT
#include "mc_store.h"

#include <stdio.h>
#include <string.h>

namespace om {
namespace mc {
namespace {

void copyTrunc(char* dst, size_t n, const char* src) {
    if (n == 0) return;
    size_t i = 0;
    if (src != nullptr) {
        for (; i + 1 < n && src[i] != '\0'; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

}  // namespace

int contactsFind(const ContactStore& s, const uint8_t pub[32]) {
    if (pub == nullptr) return -1;
    for (size_t i = 0; i < s.count; i++) {
        if (memcmp(s.c[i].pub, pub, sizeof(s.c[i].pub)) == 0) return (int)i;
    }
    return -1;
}

int contactsUpsert(ContactStore& s, const uint8_t pub[32], const char* name, uint8_t type, uint8_t path_len,
                   uint32_t remote_ts, uint32_t heard_at, int rssi, int snr_x10) {
    if (pub == nullptr) return -1;
    int idx = contactsFind(s, pub);
    if (idx < 0) {
        if (s.count >= kMaxContacts) return -1;   // full: a new node is dropped, an OLD one is not overwritten
        idx = (int)s.count++;
        memcpy(s.c[idx].pub, pub, sizeof(s.c[idx].pub));
        s.c[idx].favourite = false;
    }
    Contact& c = s.c[idx];
    // A name that arrives empty must not erase the one we already have (an app_data without the name mask).
    if (name != nullptr && name[0] != '\0') copyTrunc(c.name, sizeof(c.name), name);
    c.type     = type;
    c.path_len = path_len;
    c.remote_ts = remote_ts;
    c.heard_at  = heard_at;
    c.rssi      = (int16_t)rssi;
    c.snr_x10   = (int16_t)snr_x10;
    return idx;
}

size_t contactsOrder(const ContactStore& s, int* out, size_t max_out) {
    if (out == nullptr) return 0;
    size_t n = 0;
    // Two passes, no sort call: favourites first, each pass in descending heard_at. O(n^2) on 32 rows is
    // nothing, and it cannot reorder equal rows by an unstable tie-break.
    for (int pass = 0; pass < 2 && n < max_out; pass++) {
        const bool want_fav = (pass == 0);
        bool       taken[kMaxContacts] = {false};
        for (;;) {
            int best = -1;
            for (size_t i = 0; i < s.count; i++) {
                if (taken[i]) continue;
                if (s.c[i].favourite != want_fav) continue;
                if (best < 0 || s.c[i].heard_at > s.c[best].heard_at) best = (int)i;
            }
            if (best < 0) break;
            taken[best] = true;
            if (n < max_out) out[n++] = best;
            else return n;
        }
    }
    return n;
}

size_t contactsHeardWithin(const ContactStore& s, uint32_t now, uint32_t window_s) {
    size_t n = 0;
    for (size_t i = 0; i < s.count; i++) {
        if (s.c[i].heard_at <= now && now - s.c[i].heard_at <= window_s) n++;
    }
    return n;
}

uint32_t messagesPush(MessageRing& r, const Message& msg) {
    size_t slot;
    if (r.count < kMaxMessages) {
        slot = (r.head + r.count) % kMaxMessages;
        r.count++;
    } else {
        slot = r.head;                       // ring full: overwrite the OLDEST, which is what a log does
        r.head = (r.head + 1) % kMaxMessages;
    }
    r.m[slot] = msg;
    r.m[slot].seq = ++r.next_seq;   // ids start at 1, so a zeroed ReadState means "nothing read"
    return r.m[slot].seq;
}

const Message* messagesAt(const MessageRing& r, size_t i) {
    if (i >= r.count) return nullptr;
    return &r.m[(r.head + i) % kMaxMessages];
}

size_t messagesForChannel(const MessageRing& r, uint8_t ch, int* out, size_t max_out) {
    if (out == nullptr) return 0;
    size_t n = 0;
    for (size_t i = 0; i < r.count && n < max_out; i++) {
        if (messagesAt(r, i)->channel == ch) out[n++] = (int)i;
    }
    return n;
}

uint32_t messagesCountForChannel(const MessageRing& r, uint8_t ch) {
    uint32_t n = 0;
    for (size_t i = 0; i < r.count; i++)
        if (messagesAt(r, i)->channel == ch) n++;
    return n;
}

uint32_t unreadForChannel(const MessageRing& r, const ReadState& rs, uint8_t ch) {
    if (ch >= kMaxChannels) return 0;
    uint32_t n = 0;
    for (size_t i = 0; i < r.count; i++) {
        const Message* m = messagesAt(r, i);
        if (m->channel == ch && m->seq > rs.seen[ch]) n++;
    }
    return n;
}

void markChannelRead(const MessageRing& r, ReadState& rs, uint8_t ch) {
    if (ch >= kMaxChannels) return;
    rs.seen[ch] = r.next_seq;   // everything pushed so far, on every channel: ids are global
}

ChannelSummary channelSummary(const MessageRing& r, uint8_t ch) {
    ChannelSummary s;
    for (size_t i = r.count; i > 0; i--) {
        const Message* m = messagesAt(r, i - 1);
        if (m->channel != ch) continue;
        s.has    = true;
        s.sender = m->sender;
        s.text   = m->text;
        s.ts     = m->local_ts ? m->local_ts : m->ts;
        break;
    }
    return s;
}

void formatAge(uint32_t now, uint32_t ts, char* out, size_t n) {
    if (out == nullptr || n == 0) return;
    if (ts == 0) {
        snprintf(out, n, "-");
        return;
    }
    if (ts >= now) {
        snprintf(out, n, "now");
        return;
    }
    const uint32_t d = now - ts;
    if (d < 60) snprintf(out, n, "now");
    else if (d < 3600) snprintf(out, n, "%um", (unsigned)(d / 60));
    else if (d < 86400) snprintf(out, n, "%uh", (unsigned)(d / 3600));
    else snprintf(out, n, "%ud", (unsigned)(d / 86400));
}

}  // namespace mc
}  // namespace om
