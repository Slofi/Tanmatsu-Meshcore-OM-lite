// SPDX-License-Identifier: MIT
// MeshCore on the radio: the channel table out of NVS, and one receive path that decodes real mesh traffic.
//
// Slice 2 of step 3b. The rules it follows are MeshCore's, read from the L2's library:
//   · channels live beside the node's own settings, keyed by index: `cnt`, `n<i>` (name), `k<i>` (16-byte key);
//     the name is a local label and the hash is derived from the key at load time (channels.cpp in the L2
//     does exactly this, and BaseChatMesh::setChannel derives the same hash);
//   · a received group text is tried against every channel whose 1-byte hash matches, and the 2-byte MAC
//     decides (Mesh.cpp:236-246);
//   · a flood can reach us twice over different paths, so payloads we have already shown are dropped by a
//     small ring of payload hashes (MeshCore keeps its own seen-table; this is the leaf-node version).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_store.h"

namespace om {
namespace mc {

// Load the channel table and the node identity from NVS (namespaces "mcch" and "mcid") and say what it found.
// An empty channel table is not an error: it is the state before provisioning, and the log names the command.
// A missing identity is generated once (32 random bytes) and stored, because an advert cannot exist without it.
void begin();

size_t      channelCount();
const char* channelName(size_t i);

// Decode one received frame and print what it is: a channel text for us, a group text for a channel we do not
// have, an advert (name + key prefix), or something else. Never throws, never blocks.
void onPacket(const uint8_t* buf, size_t len, int rssi_dbm, int snr_db);

// ONE owner of the radio: the key handlers only QUEUE a request, the radio task performs it. That is not
// tidiness — S501 measured what happens otherwise: the advert's Ed25519 signing ran on the main task while the
// radio task drove the same link, and the returning flood was verified on a 4 KB stack, which panicked with a
// stack protection fault. Queueing also removes the two-writers race on the radio.
//
// Both COPY the request into a queue of FOUR (core/tx_queue.*) and return false when that queue is FULL: nothing
// is ever replaced, so a caller that gets false still owns its text and must keep it (S513 — before this, the
// second request replaced the first and a message could vanish after the composer had cleared).
// The SENDER name is not a parameter: it is the node's name (see nodeName()), so a message and an advert can
// never announce two different identities.
bool queueText(size_t i, const char* text);
bool queueAdvert();

// This node's name on the mesh: what an advert announces and what a message is signed "from" ("TNMT (SI)",
// Filip, S501 — the same <device> (<country>) shape the L2 uses for "ERA-3 (SI)"). One definition, in mc_link.cpp.
const char* nodeName();

// Called by the radio task every loop: performs any queued request (build, sign, transmit).
void serviceRequests();

// Our own public key as hex, cut to 8 bytes — the thing other nodes identify us by, for the log.
const char* publicKeyHex();

// "mc 1ch" for the status bar.
const char* statusText();

// --------------------------------------------------------------------------------- the store the pages read
// Written by the radio task as packets arrive (S502, step 4), read by the Nodes and Chat pages. One writer and
// one reader, so every access goes through StoreLock rather than hoping a name is not being refreshed mid-draw.
ContactStore& contacts();
MessageRing&  messages();
ReadState&    readState();
DmRing&       dmThreads();   // S513/EST-434: the RAM DM threads, one view per contact (item 3)

// Item 4: restore every dm-*.log from the card into the RAM threads and the contact table. Called once, after the
// card is mounted and BEFORE the radio task starts, so nothing races the restore. Occupies the radio's own DM path:
// the caller must not have started it yet.
size_t restoreDmHistory();

// "The user has now seen this DM thread": marks it read and moves the seen point in the contact's log (the format's
// Meta record), so unread survives a restart. Takes the store lock itself.
void noteDmRead(const uint8_t pub[32]);

// "The user has now seen this channel": marks it read in the store AND moves the seen point on the card (the
// format's Meta record), so unread survives a restart. One call for the three places that mean it — opening a
// channel, the refresh while it is open, and Esc on the way up — because three copies of that rule had already
// started to drift. Takes the store lock itself.
void noteChannelRead(uint8_t ch);

// A recursive mutex: a page fills its view and draws it on the same task, and the fill and the draw must see
// the same store. Held for a few milliseconds of drawing, never across a panel blit.
class StoreLock {
  public:
    StoreLock();
    ~StoreLock();
    StoreLock(const StoreLock&)            = delete;
    StoreLock& operator=(const StoreLock&) = delete;
};

}  // namespace mc
}  // namespace om
