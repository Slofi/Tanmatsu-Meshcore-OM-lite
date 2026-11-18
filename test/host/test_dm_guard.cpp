// SPDX-License-Identifier: MIT
// Host tests for the two DM defects found ON THE AIR (S513/EST-441), both on the real code the device runs:
//
//   A. A CONTACT IS BORN FROM A VERIFIED ADVERT, never from a DM log's file name. `dmRestoreThread()` is the
//      per-thread body of `sdhistory::restoreDmThreads()`; `contactsDropIncomplete()` is what `restoreContacts()`
//      runs straight after decoding the NVS blob. v41 turned `dm-<4 bytes>.log` into a contact, and that invented
//      row was used for the shared secret (a secret the peer cannot derive) and as the only RX candidate (a MAC
//      that cannot match the peer's real frames).
//   B. A FRAME WE TRANSMIT IS REGISTERED IN THE SEEN-RING BEFORE IT GOES OUT. `SeenRing` is the ring the device
//      holds; `frameFingerprint` is the payload hash (never the path), so a repeater's copy — same payload, its
//      hash added to the path — matches. This is what stops our own DM opening as an incoming one when our
//      pub_key[0] equals the peer's (ECDH is symmetric, so it DOES open).
//
// Every case here is RED against v41: the ring had 8 slots and no idea whose frame an entry was, and
// restoreDmThreads() upserted the four-byte prefix.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contact_blob.h"
#include "dm_send.h"
#include "mc_crypto.h"
#include "mc_dm.h"
#include "mc_history.h"
#include "mc_identity.h"
#include "mc_store.h"
#include "mc_wire.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define CHECK_STR(got, want)                                                          \
    do {                                                                              \
        g_checks++;                                                                   \
        if (strcmp((got), (want)) != 0) {                                             \
            g_failures++;                                                             \
            printf("FAIL %s:%d  got \"%s\" want \"%s\"\n", __FILE__, __LINE__, (got), \
                   (want));                                                           \
        }                                                                             \
    } while (0)

// -------------------------------------------------------------------------------------------- A: no contact in a log
// The four bytes a DM log's file name carries, and 28 zeros — the exact row v41 invented (log: `0x42` twice).
void prefixKey(uint8_t* pub, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    memset(pub, 0, 32);
    pub[0] = b0;
    pub[1] = b1;
    pub[2] = b2;
    pub[3] = b3;
}

void testKeyCompleteness() {
    uint8_t pub[32];
    prefixKey(pub, 0x42, 0x8b, 0x49, 0x79);
    CHECK(om::mc::contactKeyIncomplete(pub));
    pub[31] = 1;   // one non-zero byte past the prefix is enough to be a key
    CHECK(!om::mc::contactKeyIncomplete(pub));
    CHECK(om::mc::contactKeyIncomplete(nullptr));
}

// "restore with a prefix-only DM log creates NO contact" — the thread appears, the contact table stays empty.
void testRestoreCreatesNoContact() {
    om::mc::DmRing       ring;
    om::mc::ContactStore contacts;

    uint8_t pub[32];
    prefixKey(pub, 0x42, 0x8b, 0x49, 0x79);

    om::mc::DmMessage recs[2];
    recs[0].log_id = 1;
    recs[0].outgoing = false;
    recs[0].ts = 1795011200;
    snprintf(recs[0].peer_name, sizeof(recs[0].peer_name), "ERA-3 (SI)");
    snprintf(recs[0].text, sizeof(recs[0].text), "hello");
    recs[0].delivery = om::ui::DmState::Sending;
    recs[1].log_id = 2;
    recs[1].outgoing = true;
    recs[1].ts = 1795011320;
    snprintf(recs[1].peer_name, sizeof(recs[1].peer_name), "TNMT (SI)");
    snprintf(recs[1].text, sizeof(recs[1].text), "t4");
    recs[1].delivery = om::ui::DmState::Sending;

    om::mc::SeenPoint sp;
    sp.found       = true;
    sp.seen_log_id = 1;   // the first record was read before the restart

    om::mc::RestoredDm r;
    const size_t       n = om::mc::dmRestoreThread(ring, contacts, pub, recs, 2, sp, &r);

    CHECK(n == 2);
    CHECK(r.messages == 2);
    CHECK(!r.rebound);
    CHECK_STR(r.label, "ERA-3 (SI)");
    // THE POINT: a thread, and not one contact.
    CHECK(contacts.count == 0);
    CHECK(om::mc::contactsFind(contacts, pub) < 0);
    // The thread is named from its own records, and the seen point landed on the first record's new seq.
    CHECK(ring.count == 2);
    const om::mc::DmMessage* first = om::mc::dmAt(ring, 0);
    CHECK(first != nullptr);
    CHECK(first->seq == r.seen_seq);
    CHECK(om::mc::contactKeyIncomplete(first->peer_pub));   // still the prefix: no key was invented
    CHECK_STR(first->peer_name, "ERA-3 (SI)");
    // The truncation is a PREFIX match on 4 bytes only, as the file name is.
    CHECK(memcmp(first->peer_pub, pub, 4) == 0);
}

// A thread with only our own messages: no name to borrow, so the 8-hex prefix names it — and still no contact.
void testRestoreOutgoingOnlyKeepsLabel() {
    om::mc::DmRing       ring;
    om::mc::ContactStore contacts;
    uint8_t              pub[32];
    prefixKey(pub, 0x42, 0x8b, 0x49, 0x79);

    om::mc::DmMessage rec;
    rec.log_id  = 7;
    rec.outgoing = true;
    snprintf(rec.peer_name, sizeof(rec.peer_name), "TNMT (SI)");
    snprintf(rec.text, sizeof(rec.text), "t4");

    om::mc::RestoredDm r;
    CHECK(om::mc::dmRestoreThread(ring, contacts, pub, &rec, 1, om::mc::SeenPoint(), &r) == 1);
    CHECK(contacts.count == 0);
    CHECK_STR(r.label, "428b4979");
    CHECK(!r.seen_found);
    CHECK(r.seen_seq == 0);
}

// A full-key contact already in the table (it came from a verified advert) is the ONLY thing that may name the
// rest of the key — and only when its four-byte prefix is unique.
void testRestoreRebindsToUniqueFullKey() {
    om::mc::DmRing       ring;
    om::mc::ContactStore contacts;
    uint8_t              full[32];
    prefixKey(full, 0x42, 0x8b, 0x49, 0x79);
    full[4] = 0x11;
    full[31] = 0x99;
    CHECK(om::mc::contactsUpsert(contacts, full, "ERA-3 (SI)", 1, 0, 0, 0, 0, 0) == 0);

    uint8_t prefix[32];
    prefixKey(prefix, 0x42, 0x8b, 0x49, 0x79);
    om::mc::DmMessage rec;
    rec.log_id = 1;
    snprintf(rec.peer_name, sizeof(rec.peer_name), "ERA-3 (SI)");

    om::mc::RestoredDm r;
    CHECK(om::mc::dmRestoreThread(ring, contacts, prefix, &rec, 1, om::mc::SeenPoint(), &r) == 1);
    CHECK(r.rebound);
    CHECK(contacts.count == 1);   // still one row: the thread joined it, it did not create a second
    const om::mc::DmMessage* m = om::mc::dmAt(ring, 0);
    CHECK(m != nullptr && memcmp(m->peer_pub, full, 32) == 0);
    CHECK(contacts.c[0].dm_log_next == 2);
}

// The 0x42/0x42 collision: TWO full keys share the prefix, so the prefix names nobody and the thread stays
// orphaned. Guessing here is how v41 attached a thread to the wrong peer.
void testRestoreRefusesAmbiguousPrefix() {
    om::mc::DmRing       ring;
    om::mc::ContactStore contacts;
    uint8_t              a[32], b[32];
    prefixKey(a, 0x42, 0x8b, 0x49, 0x79);
    a[5] = 1;
    prefixKey(b, 0x42, 0x8b, 0x49, 0x79);
    b[5] = 2;
    CHECK(om::mc::contactsUpsert(contacts, a, "ERA-3 (SI)", 1, 0, 0, 0, 0, 0) == 0);
    CHECK(om::mc::contactsUpsert(contacts, b, "ERA-OTHER", 1, 0, 0, 0, 0, 0) == 1);
    CHECK(om::mc::contactsFindUniquePrefix(contacts, a) == -1);

    uint8_t prefix[32];
    prefixKey(prefix, 0x42, 0x8b, 0x49, 0x79);
    om::mc::DmMessage rec;
    rec.log_id = 1;
    om::mc::RestoredDm r;
    CHECK(om::mc::dmRestoreThread(ring, contacts, prefix, &rec, 1, om::mc::SeenPoint(), &r) == 1);
    CHECK(!r.rebound);
    const om::mc::DmMessage* m = om::mc::dmAt(ring, 0);
    CHECK(m != nullptr && om::mc::contactKeyIncomplete(m->peer_pub));
}

// "an NVS blob holding a prefix-only record loads without it": the device decodes the blob, then drops every
// incomplete row (restoreContacts() does exactly this order). The cleaned table is what gets written back.
void testNvsBlobDropsPrefixOnlyRecord() {
    uint8_t blob[om::mc::kContactBlobMaxBytes];

    om::mc::ContactStore stored;
    uint8_t              good[32];
    prefixKey(good, 0x42, 0xa1, 0x60, 0x70);   // our own node, as a full key
    good[4]  = 0x42;
    good[20] = 0xEE;
    CHECK(om::mc::contactsUpsert(stored, good, "TNMT (SI)", 1, 0, 100, 200, -70, 25) == 0);
    uint8_t bogus[32];
    prefixKey(bogus, 0x42, 0x8b, 0x49, 0x79);   // what v41 wrote from dm-428b4979.log
    CHECK(om::mc::contactsUpsert(stored, bogus, "ERA-3 (SI)", 0, 0, 0, 0, 0, 0) == 1);

    const size_t bytes = om::mc::contactsEncode(stored, blob, sizeof(blob));
    CHECK(bytes > 0);

    om::mc::ContactStore loaded;
    CHECK(om::mc::contactsDecode(blob, bytes, loaded) == om::mc::ContactDecode::Ok);
    CHECK(loaded.count == 2);   // the blob really does hold the bogus row (v41 stored it: "1 stored to NVS")

    const size_t dropped = om::mc::contactsDropIncomplete(loaded);
    CHECK(dropped == 1);
    CHECK(loaded.count == 1);
    CHECK(memcmp(loaded.c[0].pub, good, 32) == 0);
    CHECK_STR(loaded.c[0].name, "TNMT (SI)");

    // Writing the cleaned table back and reading it again yields one row: the drop survives a restart.
    const size_t bytes2 = om::mc::contactsEncode(loaded, blob, sizeof(blob));
    CHECK(bytes2 > 0);
    om::mc::ContactStore again;
    CHECK(om::mc::contactsDecode(blob, bytes2, again) == om::mc::ContactDecode::Ok);
    CHECK(again.count == 1);
    CHECK(om::mc::contactsDropIncomplete(again) == 0);

    // A table of nothing but bogus rows comes back empty, not with a row whose key is four bytes.
    om::mc::ContactStore only_bogus;
    prefixKey(bogus, 0x42, 0x8b, 0x49, 0x79);
    CHECK(om::mc::contactsUpsert(only_bogus, bogus, "ERA-3 (SI)", 0, 0, 0, 0, 0, 0) == 0);
    CHECK(om::mc::contactsDropIncomplete(only_bogus) == 1);
    CHECK(only_bogus.count == 0);
}

// "queueDm on a non-contact refuses" — the decision queueDm asks first. Never a shared secret from 4 bytes.
void testPeerMustBeFullKeyContact() {
    om::mc::ContactStore s;
    char                 name[om::mc::kNameBytes];

    uint8_t prefix[32];
    prefixKey(prefix, 0x42, 0x8b, 0x49, 0x79);
    CHECK(!om::mc::dmPeerKey(s, prefix, name, sizeof(name)));   // no contacts at all

    // A restored thread with no contact row: refused, and the name is left empty (the caller falls back).
    snprintf(name, sizeof(name), "x");
    CHECK(!om::mc::dmPeerKey(s, prefix, name, sizeof(name)));
    CHECK_STR(name, "");

    uint8_t full[32];
    prefixKey(full, 0x42, 0x8b, 0x49, 0x79);
    full[4] = 0x5A;
    CHECK(om::mc::contactsUpsert(s, full, "ERA-3 (SI)", 1, 0, 0, 0, 0, 0) == 0);
    // The SAME four bytes, but only four: still not a contact (contactsFind matches all 32).
    CHECK(!om::mc::dmPeerKey(s, prefix, name, sizeof(name)));
    // The full key is a contact: sendable, and the name comes back for the log line.
    CHECK(om::mc::dmPeerKey(s, full, name, sizeof(name)));
    CHECK_STR(name, "ERA-3 (SI)");

    // A row that somehow IS the prefix (a blob from v41 that was not dropped) is still refused.
    om::mc::ContactStore bad;
    prefixKey(prefix, 0x42, 0x8b, 0x49, 0x79);
    CHECK(om::mc::contactsUpsert(bad, prefix, "ERA-3 (SI)", 0, 0, 0, 0, 0, 0) == 0);
    CHECK(!om::mc::dmPeerKey(bad, prefix, name, sizeof(name)));
}

// ------------------------------------------------------------------------------------ B: our own frame comes back
// A peer whose pub_key[0] equals OURS: the collision that made our own DM open "from ERA-3". Searched, not
// invented — a real Ed25519 key from a real seed, found by deriving seeds until the first byte matches.
meshcore::Identity g_self;
meshcore::Identity g_peer;

void findCollidingPeer() {
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = (uint8_t)(0xA0 + i);
    CHECK(meshcore::identityFromSeed(g_self, seed));

    // A fast xorshift fills each seed: an arithmetic seed family correlates badly (measured here: 3000 such seeds
    // reached only 147 of the 256 first-byte values), while random seeds hit the matching first byte in ~256 tries.
    uint32_t st = 0x9E3779B9u;
    for (uint32_t n = 0; n < 4000; n++) {
        uint8_t s2[32];
        for (size_t i = 0; i < sizeof(s2); i++) {
            st ^= st << 13;
            st ^= st >> 17;
            st ^= st << 5;
            s2[i] = (uint8_t)st;
        }
        meshcore::Identity cand;
        if (!meshcore::identityFromSeed(cand, s2)) continue;
        if (cand.pub[0] != g_self.pub[0]) continue;
        if (memcmp(cand.pub, g_self.pub, 32) == 0) continue;
        g_peer = cand;
        return;
    }
    CHECK(false);   // 256 tries expected, 5000 offered: this must never happen
}

// The frame a repeater forwards: the SAME payload, one hop added. This mesh runs 2-byte path hashes, so
// path_len 0x41 = (hash_size 2, count 1) and the path is the TWO bytes of the repeater's hash — path_len 0x41 with
// "1 hop(s)" is the v41 log line ("DM: route to ... stored (path_len 0x41, 1 hop(s))").
size_t repeaterCopy(const uint8_t* frame, size_t len, meshcore::View* in, uint8_t* out, size_t out_size) {
    if (!meshcore::decode(frame, len, in)) return 0;
    const size_t need = 2 + 2 + (size_t)in->payload_len;   // header, path_len, 2 path bytes, payload
    if (need > out_size) return 0;
    out[0] = in->header;      // same route/type: the repeater changes nothing but the path
    out[1] = 0x41;            // 2-byte hashes, count 1
    out[2] = 0x7E;            // the repeater's hash ...
    out[3] = 0x3C;            // ... both bytes of it
    memcpy(&out[4], in->payload, in->payload_len);
    return need;
}

void testOurOwnEchoIsDropped() {
    findCollidingPeer();
    CHECK(g_self.pub[0] == g_peer.pub[0]);

    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_self, g_peer.pub, secret));

    meshcore::DmComposed c;
    CHECK(meshcore::dmCompose(g_self, g_peer.pub, secret, 1795011320u, 0, "t4", nullptr, 0, &c));

    meshcore::SeenRing ring;
    meshcore::View     sent;
    CHECK(meshcore::decode(c.frame, c.frame_len, &sent));
    const uint32_t fp = meshcore::frameFingerprint(sent);

    // What queueFrame() does before the frame reaches the radio.
    ring.noteOurs(fp);

    // 1. The echo, byte for byte.
    meshcore::View echo;
    CHECK(meshcore::decode(c.frame, c.frame_len, &echo));
    bool ours = false;
    CHECK(ring.seen(meshcore::frameFingerprint(echo), &ours));
    CHECK(ours);

    // 2. The echo through a repeater: same payload, a path hash added. The fingerprint ignores the path, so it is
    //    the same frame — this is the exact case in the log ("path_len 0x41, 1 hop(s)").
    uint8_t       echoed[256];
    const size_t  en = repeaterCopy(c.frame, c.frame_len, &echo, echoed, sizeof(echoed));
    CHECK(en > 0);
    meshcore::View repeater_echo;
    CHECK(meshcore::decode(echoed, en, &repeater_echo));
    CHECK(repeater_echo.path_bytes == 2);
    CHECK((repeater_echo.path_len & 0x3F) == 1);   // one hop
    CHECK(meshcore::frameFingerprint(repeater_echo) == fp);
    bool ours2 = false;
    CHECK(ring.seen(meshcore::frameFingerprint(repeater_echo), &ours2));
    CHECK(ours2);

    // 3. The DM contains our own timestamp and text: without the ring it opens against the colliding peer,
    //    because ECDH is symmetric. This shows WHY the drop has to happen.
    uint8_t peers[32];
    memcpy(peers, g_peer.pub, 32);
    meshcore::DmMessage opened;
    CHECK(meshcore::dmOpen(g_self, repeater_echo, peers, 1, 0, &opened));
    CHECK_STR(opened.text, "t4");
    CHECK(opened.timestamp == 1795011320u);
}

// A GENUINE DM from the peer whose pub_key[0] equals ours is NOT in the ring and must still open.
void testGenuineDmFromCollidingPeerOpens() {
    findCollidingPeer();

    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_peer, g_self.pub, secret));

    meshcore::DmComposed c;
    CHECK(meshcore::dmCompose(g_peer, g_self.pub, secret, 1795011999u, 0, "yes I hear you", nullptr, 0, &c));

    // A ring that has seen OUR earlier transmission (the same exchange, our side).
    uint8_t            our_secret[32];
    CHECK(meshcore::identitySharedSecret(g_self, g_peer.pub, our_secret));
    meshcore::DmComposed ours;
    CHECK(meshcore::dmCompose(g_self, g_peer.pub, our_secret, 1795011320u, 0, "t4", nullptr, 0, &ours));
    meshcore::SeenRing ring;
    meshcore::View     mine;
    CHECK(meshcore::decode(ours.frame, ours.frame_len, &mine));
    ring.noteOurs(meshcore::frameFingerprint(mine));

    meshcore::View theirs;
    CHECK(meshcore::decode(c.frame, c.frame_len, &theirs));
    bool already = true;
    CHECK(!ring.seen(meshcore::frameFingerprint(theirs), &already));   // new: not dropped
    CHECK(!already);

    uint8_t peers[32];
    memcpy(peers, g_peer.pub, 32);
    meshcore::DmMessage dm;
    CHECK(meshcore::dmOpen(g_self, theirs, peers, 1, 0, &dm));
    CHECK_STR(dm.text, "yes I hear you");
    CHECK(dm.timestamp == 1795011999u);
}

// A duplicate from a PEER is a duplicate, not our echo — the two reasons are kept apart in the log line.
void testPeerDuplicateIsNotOurEcho() {
    findCollidingPeer();
    uint8_t secret[32];
    CHECK(meshcore::identitySharedSecret(g_peer, g_self.pub, secret));
    meshcore::DmComposed c;
    CHECK(meshcore::dmCompose(g_peer, g_self.pub, secret, 42, 0, "hi", nullptr, 0, &c));

    meshcore::SeenRing ring;
    meshcore::View     first;
    CHECK(meshcore::decode(c.frame, c.frame_len, &first));
    bool ours = true;
    CHECK(!ring.seen(meshcore::frameFingerprint(first), &ours));   // first copy: processed
    CHECK(!ours);

    meshcore::View again;
    CHECK(meshcore::decode(c.frame, c.frame_len, &again));
    CHECK(ring.seen(meshcore::frameFingerprint(again), &ours));
    CHECK(!ours);   // dropped, but as a duplicate — NOT as our own echo
}

// The ring's size is part of the fix: 8 slots was the defect (a retry burst pushed our own entry out before the
// repeater returned it).
void testRingIsThirtyTwo() {
    CHECK(meshcore::kSeenSlots == 32);
    meshcore::SeenRing ring;
    // 32 distinct frames fit.
    for (uint32_t i = 1; i <= 32; i++) {
        bool ours = true;
        CHECK(!ring.seen(i, &ours));
    }
    bool ours = false;
    CHECK(ring.seen(1, &ours));
    // The 33rd evicts the oldest, which is the ring's rule (and why 8 was too small for two-way DM traffic).
    CHECK(!ring.seen(33, &ours));
    CHECK(!ring.seen(1, &ours));
    CHECK(ring.seen(33, &ours));

    // A fingerprint of 0 is never a match and never stored.
    CHECK(!ring.seen(0, &ours));
    CHECK(!ring.seen(0, &ours));
}

// Registering a frame twice must not consume two slots (a retry repeats a payload; an echo may arrive twice).
void testNoteOursIsIdempotent() {
    meshcore::SeenRing ring;
    ring.noteOurs(7);
    ring.noteOurs(7);
    for (uint32_t i = 100; i < 100 + 30; i++) {
        bool ours = true;
        CHECK(!ring.seen(i, &ours));   // 30 more = 31 entries in total, so 7 is still there
    }
    bool ours = false;
    CHECK(ring.seen(7, &ours));
    CHECK(ours);
    // And a frame first heard as a peer's duplicate becomes OURS once we transmit the same payload (a retry).
    ring.seen(500, &ours);
    ring.noteOurs(500);
    CHECK(ring.seen(500, &ours));
    CHECK(ours);
}

}  // namespace

int main() {
    testKeyCompleteness();
    testRestoreCreatesNoContact();
    testRestoreOutgoingOnlyKeepsLabel();
    testRestoreRebindsToUniqueFullKey();
    testRestoreRefusesAmbiguousPrefix();
    testNvsBlobDropsPrefixOnlyRecord();
    testPeerMustBeFullKeyContact();
    testOurOwnEchoIsDropped();
    testGenuineDmFromCollidingPeerOpens();
    testPeerDuplicateIsNotOurEcho();
    testRingIsThirtyTwo();
    testNoteOursIsIdempotent();

    if (g_failures == 0) {
        printf("dm guard: %d checks passed\n", g_checks);
        return 0;
    }
    printf("dm guard: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
