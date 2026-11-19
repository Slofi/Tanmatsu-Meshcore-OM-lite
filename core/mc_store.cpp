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
                   uint32_t remote_ts, uint32_t heard_at, int rssi, int snr_x10, bool has_position,
                   int32_t lat_e6, int32_t lon_e6) {
    if (pub == nullptr) return -1;
    int idx = contactsFind(s, pub);
    if (idx < 0) {
        if (s.count >= kMaxContacts) return -1;   // full: a new node is dropped, an OLD one is not overwritten
        idx = (int)s.count++;
        memcpy(s.c[idx].pub, pub, sizeof(s.c[idx].pub));
        s.c[idx].favourite = false;
        // S515/EST-461: NEW. This is the only place a row is born, and every caller of this function is the
        // verified-advert path (mc_link's kTypeAdvert branch), so "inserted here" IS "first verified advert of a
        // key that was not in the table". A restore from NVS/SD decodes the stored flag instead.
        s.c[idx].fresh = true;
    }
    Contact& c = s.c[idx];
    // A name that arrives empty must not erase the one we already have (an app_data without the name mask).
    if (name != nullptr && name[0] != '\0') copyTrunc(c.name, sizeof(c.name), name);
    // The same rule for the position: an advert that carries none must not wipe a known one.
    if (has_position) {
        c.has_position = true;
        c.lat_e6       = lat_e6;
        c.lon_e6       = lon_e6;
    }
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
    // Three passes, no sort call (S515/EST-461): favourites first, then the still-fresh rows newest first,
    // then everything else by last heard. Each pass is descending heard_at, so equal rows cannot swap places
    // by an unstable tie-break (O(n^2) on 32 rows is nothing).
    for (int pass = 0; pass < 3 && n < max_out; pass++) {
        bool taken[kMaxContacts] = {false};
        for (;;) {
            int best = -1;
            for (size_t i = 0; i < s.count; i++) {
                if (taken[i]) continue;
                const bool in_group = (pass == 0) ? s.c[i].favourite
                                     : (pass == 1) ? (!s.c[i].favourite && s.c[i].fresh)
                                                   : (!s.c[i].favourite && !s.c[i].fresh);
                if (!in_group) continue;
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

size_t contactsFreshCount(const ContactStore& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.count; i++)
        if (s.c[i].fresh) n++;
    return n;
}

size_t contactsHeardWithin(const ContactStore& s, uint32_t now, uint32_t window_s) {
    size_t n = 0;
    for (size_t i = 0; i < s.count; i++) {
        if (s.c[i].heard_at <= now && now - s.c[i].heard_at <= window_s) n++;
    }
    return n;
}

// ---- S513/EST-441: no contact without a full key ------------------------------------------------------------
bool contactKeyIncomplete(const uint8_t pub[32]) {
    if (pub == nullptr) return true;
    for (size_t i = 4; i < 32; i++) {
        if (pub[i] != 0) return false;
    }
    return true;
}

size_t contactsDropIncomplete(ContactStore& s) {
    size_t kept    = 0;
    size_t dropped = 0;
    for (size_t i = 0; i < s.count && i < kMaxContacts; i++) {
        if (contactKeyIncomplete(s.c[i].pub)) {
            dropped++;
            continue;
        }
        if (kept != i) s.c[kept] = s.c[i];
        kept++;
    }
    s.count = kept;
    return dropped;
}

int contactsFindUniquePrefix(const ContactStore& s, const uint8_t prefix[4]) {
    if (prefix == nullptr) return -1;
    int found = -1;
    for (size_t i = 0; i < s.count && i < kMaxContacts; i++) {
        // An incomplete row is not a key at all: it can never be the answer, so it is not a second candidate.
        if (contactKeyIncomplete(s.c[i].pub)) continue;
        if (memcmp(s.c[i].pub, prefix, 4) != 0) continue;
        if (found >= 0) return -1;   // two full keys share the prefix: refuse to guess which one it is
        found = (int)i;
    }
    return found;
}

bool dmPeerKey(const ContactStore& s, const uint8_t pub[32], char* name, size_t name_cap) {
    if (name != nullptr && name_cap > 0) name[0] = '\0';
    if (pub == nullptr || contactKeyIncomplete(pub)) return false;   // never a shared secret from 4 bytes
    const int i = contactsFind(s, pub);
    if (i < 0 || i >= (int)s.count || contactKeyIncomplete(s.c[i].pub)) return false;
    if (name != nullptr && name_cap > 0) snprintf(name, name_cap, "%s", s.c[i].name);
    return true;
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

void unreadTotals(const MessageRing& r, const ReadState& rs, size_t channel_count, const DmRing& d,
                  const ContactStore& s, uint32_t* channel_unread, uint32_t* dm_unread) {
    uint32_t ch = 0;
    for (size_t i = 0; i < channel_count && i < kMaxChannels; i++) ch += unreadForChannel(r, rs, (uint8_t)i);
    // A DM thread's unread is counted against its contact row (dm_seen), so only peers IN the table can have
    // any — the same answer dmThreadSummaries() puts on the cards, without a 64-entry summary array.
    uint32_t dm = 0;
    for (size_t i = 0; i < s.count && i < kMaxContacts; i++) {
        if (contactKeyIncomplete(s.c[i].pub)) continue;
        dm += dmUnreadForPeer(d, s, s.c[i].pub);
    }
    if (channel_unread != nullptr) *channel_unread = ch;
    if (dm_unread != nullptr) *dm_unread = dm;
}

// ------------------------------------------------------------------------------------- direct messages (S513/EST-434)
// The same ring rule as channels: overwrite the OLDEST when full, ids start at 1 so a zeroed seen point means
// "nothing read". A thread is not a second ring — it is this ring filtered by the peer's public key.
uint32_t dmPush(DmRing& r, const DmMessage& msg) {
    size_t slot;
    if (r.count < kMaxDms) {
        slot = (r.head + r.count) % kMaxDms;
        r.count++;
    } else {
        slot = r.head;
        r.head = (r.head + 1) % kMaxDms;
    }
    r.m[slot] = msg;
    r.m[slot].seq = ++r.next_seq;
    return r.m[slot].seq;
}

const DmMessage* dmAt(const DmRing& r, size_t i) {
    if (i >= r.count) return nullptr;
    return &r.m[(r.head + i) % kMaxDms];
}

namespace {

bool samePub(const uint8_t* a, const uint8_t* b) { return a != nullptr && b != nullptr && memcmp(a, b, 32) == 0; }

}  // namespace

size_t dmForPeer(const DmRing& r, const uint8_t pub[32], int* out, size_t max_out) {
    if (pub == nullptr || out == nullptr) return 0;
    size_t n = 0;
    for (size_t i = 0; i < r.count && n < max_out; i++) {
        const DmMessage* m = dmAt(r, i);
        if (samePub(m->peer_pub, pub)) out[n++] = (int)i;
    }
    return n;
}

uint32_t dmUnreadForPeer(const DmRing& r, const ContactStore& s, const uint8_t pub[32]) {
    const int idx = contactsFind(s, pub);
    if (idx < 0) return 0;
    uint32_t n = 0;
    for (size_t i = 0; i < r.count; i++) {
        const DmMessage* m = dmAt(r, i);
        if (samePub(m->peer_pub, pub) && m->seq > s.c[idx].dm_seen) n++;
    }
    return n;
}

void dmMarkPeerRead(const DmRing& r, ContactStore& s, const uint8_t pub[32]) {
    const int idx = contactsFind(s, pub);
    if (idx < 0) return;
    s.c[idx].dm_seen = r.next_seq;
}

uint32_t dmNewestLogId(const DmRing& r, const uint8_t pub[32]) {
    if (pub == nullptr) return 0;
    uint32_t newest = 0;
    for (size_t i = 0; i < r.count; i++) {
        const DmMessage* m = dmAt(r, i);
        if (samePub(m->peer_pub, pub) && m->log_id > newest) newest = m->log_id;
    }
    return newest;
}

DmSummary dmSummaryForPeer(const DmRing& r, const ContactStore& s, const uint8_t pub[32]) {
    DmSummary out;
    if (pub == nullptr) return out;
    memcpy(out.pub, pub, sizeof(out.pub));
    const int idx = contactsFind(s, pub);
    for (size_t i = r.count; i > 0; i--) {
        const DmMessage* m = dmAt(r, i - 1);
        if (!samePub(m->peer_pub, pub)) continue;
        out.has     = true;
        out.contact = idx;
        out.name    = (idx >= 0 && s.c[idx].name[0] != '\0') ? s.c[idx].name : m->peer_name;
        out.text    = m->text;
        out.ts      = m->local_ts ? m->local_ts : m->ts;
        break;
    }
    out.unread = dmUnreadForPeer(r, s, pub);
    return out;
}

size_t dmThreadSummaries(const DmRing& r, const ContactStore& s, DmSummary* out, size_t max_out) {
    if (out == nullptr) return 0;
    size_t n = 0;
    // Walk the ring newest-first and take the FIRST message of each peer. A peer already taken has a LATER
    // message than the current one, so it must be skipped: no extra table, just a second scan.
    for (size_t i = r.count; i > 0 && n < max_out; i--) {
        const DmMessage* m = dmAt(r, i - 1);
        bool later = false;
        for (size_t j = i; j < r.count; j++) {
            if (samePub(dmAt(r, j)->peer_pub, m->peer_pub)) {
                later = true;
                break;
            }
        }
        if (later) continue;
        out[n++] = dmSummaryForPeer(r, s, m->peer_pub);
    }
    return n;
}

}  // namespace mc
}  // namespace om
