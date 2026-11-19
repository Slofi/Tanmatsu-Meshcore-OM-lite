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

#include "advert_rule.h"
#include "mc_identity.h"
#include "mc_store.h"

namespace om {
namespace mc {

// Load the channel table and the node identity from NVS (namespaces "mcch" and "mcid") and say what it found.
// An empty channel table is not an error: it is the state before provisioning, and the log names the command.
// A missing identity is generated once (32 random bytes) and stored, because an advert cannot exist without it.
void begin();

size_t      channelCount();
const char* channelName(size_t i);

// S513/EST-440: the channel's stored flood scope, read from NVS "mcch" key "s<hh>" of the channel's LIVE
// 1-byte hash (never a mirrored/legacy hash: S509's first defect). "" = Default, "!none" = No scope,
// anything else = the region name without '#', which is the L2's stored form. A missing key is Default
// and not an error; a corrupt/over-long value is also Default, never truncated into a different region.
// The value is cached per channel after the first read (channels are provisioned at boot and there is no
// editing UI yet); channelScopeSet() updates the cache.
bool channelScopeStored(size_t i, char* out, size_t cap);

// S513/EST-440: write one channel's scope. An empty value removes the key and goes back to Default; a key
// that was never there is already Default, so NVS's NOT_FOUND on erase is SUCCESS (S509's second defect).
// Values are stored raw, exactly as the L2's mcChannelSetScope() does; callers pass a canonical name.
bool channelScopeSet(size_t i, const char* setting);

// S513/EST-440: the open channel's head text in the ticket's words — "scope #si", "no scope", or "" when
// the plan is unscoped. Resolves through core/channel_scope.* (channel -> node default -> unscoped; the
// Tanmatsu has no node default, so Default is unscoped today).
void channelScopeHead(size_t i, char* out, size_t cap);

// ---------------------------------------------------------- S514/EST-446: add, remove and rename a channel
// The MAIN task calls these. Each one writes the new NVS "mcch" table in the safe order from
// core/channel_admin.h (new entries first, `cnt` last), so a power cut at any step leaves a loadable table,
// and then asks the radio task to reload — the radio task is the one owner of the live table, so a
// channel becomes usable (and disappears) without a restart. Names are logged; keys never are.
//
// channelsAddHashtag: the L2's normalisation (one '#', no spaces, lowercased); the key is sha256(tag)[0..15].
// `out_name` receives the normalised tag. False on a bare '#', a full table, or a duplicate.
bool channelsAddHashtag(const char* typed, char* out_name, size_t cap);
// channelsAddByKey: the L2's base64 rules (16 bytes, spaces skipped, '=' ends, bad characters refused).
// False on a bad name, a bad key, a full table, or a duplicate key.
bool channelsAddByKey(const char* name, const char* key_b64);
// Rename a PRIVATE channel (a hashtag's name IS its key, so the UI never offers this for one).
bool channelsRename(size_t i, const char* name);
// Remove slot i (index 0 included); the table compacts and the radio's view follows on its next loop.
bool channelsRemove(size_t i);
// Copy one channel's 16-byte key, for the on-screen "share" only. It is never logged or stored.
bool channelsKeyCopy(size_t i, uint8_t out[16]);
// A manual reload request for callers that changed NVS themselves.
void channelsRequestReload();
// Goes up by one each time the radio task reloads the table, so a page can notice it changed.
uint32_t channelsRevision();

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

// S513/EST-435: ask for an advert. Returns the RULE's answer, not just true/false, because a refusal now has a
// reason worth showing: `Wait` carries the whole seconds left of the 5 s gap, `Busy` means one is still queued.
// `Send` means it WAS queued (or, if the send queue was full, `Busy`). The caller (a key handler) renders the
// sentence with advertRefusalText(); nothing here draws, and nothing here blocks.
AdvertDecision queueAdvert();

// This node's name on the mesh: what an advert announces and what a message is signed "from" ("TNMT (SI)",
// Filip, S501 — the same <device> (<country>) shape the L2 uses for "ERA-3 (SI)"). One definition, in mc_link.cpp.
// EST-445: it is read from NVS mcid/name at begin() and used for adverts AND as the channel/DM sender, so the
// two can never announce different identities. The default when nothing is stored is tm::kDefaultNodeName.
const char* nodeName();

// EST-445: apply a new node name. The caller validates with tm::nodeNameApply (1..31 bytes, MeshCore's
// allowed characters, trimmed); this re-checks it and only moves the live name when NVS mcid/name accepted it.
bool setNodeName(const char* name);

// EST-445: the automatic advert interval, in SECONDS (0 = Off), stored in NVS mcid/advint. The schedule in
// serviceRequests() reads it every loop; the manual paths ('a', "Advert now") never do. set... accepts only
// the five choices (0/1/3/6/12 h) and returns false otherwise.
uint32_t advertIntervalSeconds();
bool setAdvertIntervalSeconds(uint32_t seconds);

// Called by the radio task every loop: performs any queued request (build, sign, transmit).
void serviceRequests();

// Our own public key as hex, cut to 8 bytes — the thing other nodes identify us by, for the log.
const char* publicKeyHex();

// S514/EST-453: RPTR part 1's two doors into the radio. selfIdentity() copies the node's keypair (false before
// the first begin()), and queueRptrFrame() puts a COMPLETE frame into the same FOUR-slot queue a DM uses — the
// RPTR page never calls the radio itself. The receive hooks live in main/rptr_link.* and are called from
// onPacket() on the radio task.
bool selfIdentity(meshcore::Identity* out);
bool queueRptrFrame(const uint8_t* frame, size_t len);

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

// S513/EST-437: restore the contact table from NVS ("mcct"/"tbl"). Called at boot BEFORE the radio task exists, so
// a DM from a node heard before the restart can be opened without waiting for its next advert. Logs
// "contacts: N restored from NVS"; a wrong version/size is ignored with one warning line.
size_t restoreContacts();

// S513/EST-437: called by the MAIN task in its loop. Writes the contact blob when the radio task has marked it
// dirty: an add/rename/route change on the next pass, last-heard/SNR at most once per 10 minutes. Never writes NVS
// from the radio task; does nothing when there is nothing to write.
void contactsPersistTick();

// ------------------------------------------------- S514/EST-447: the NODES section's two destructive actions
// The MAIN task calls both. They take the store lock, change the RAM table through core/contact_admin.h and
// raise the SAME structural dirty flag contactsPersistTick() already owns, so the `mcct` blob is rewritten on
// this loop's pass without a second NVS writer. A contact's DM threads and dm-*.log are never touched: its
// history stays on the card, as the confirm sheet says.
bool   contactsRemove(size_t index);   // false for an out-of-range index (a stale row must remove nobody)
size_t contactsClear();                // returns how many rows were removed; 0 changes nothing

// S514/EST-455: set one contact's favourite flag. It is a STRUCTURAL change — the flag is part of the contact
// record in the `mcct` blob — so it raises the same dirty flag and contactsPersistTick() rewrites NVS on this
// loop's pass, exactly like remove/clear. False for an out-of-range index (a stale row must mark nobody).
bool contactsSetFavourite(size_t index, bool on);

// The "New node: ..." notice (NVS "mcset"/"newnode", default On). The radio task reads the cached value when it
// raises the notice; the MAIN task flips it. A corrupt stored byte reads as On.
bool newNodeNoticeEnabled();
bool setNewNodeNoticeEnabled(bool on);

// S513/EST-441: is this peer sendable, and what is it called? False when it is not a contact with a FULL key — a
// DM thread restored from the card knows only four bytes of its peer's key, and a shared secret over those bytes
// is one the peer cannot derive. The DM view keeps the text and says "No key for <name> yet: waiting for its
// advert"; `name` is filled whatever the answer.
bool dmPeerReady(const uint8_t pub[32], char* name, size_t cap);

// S513/EST-439: send a direct message to `pub` (the open DM thread's contact). The MAIN task calls this: it
// composes attempt 0 (a flood when the contact's route is OUT_PATH_UNKNOWN, a DIRECT frame down the route it
// learned otherwise), puts the message in the thread and its log as SENDING, begins the pending entry that the
// ACK will clear, and QUEUES the frame — it never transmits. False when the radio's queue is full or the text does
// not fit: nothing is stored or sent, so the caller still owns its text ("radio busy, not sent").
bool queueDm(const uint8_t pub[32], const char* text);

// S513/EST-439: called by the MAIN task in its loop. Polls the pending table: a retry composes the next attempt
// (1, 2; the LAST is a flood, and a failed DIRECT attempt clears the stored route) and queues it; when every
// attempt is spent the message becomes "no confirmation". Returns true when a state changed, so the page repaints.
bool dmTick();

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
