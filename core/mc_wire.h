// SPDX-License-Identifier: MIT
// MeshCore wire format, portable: the packet frame and the group-text payload.
//
// Read from the L2's own library in S501 (firmware/lib/meshcore, v1.18.0), not from memory:
//   · Packet::writeTo()/readFrom()  -> the frame on the air is
//         header, [transport_codes 4 bytes], path_len, path(hash_count * hash_size), payload
//     with no length prefix anywhere (Packet.cpp:37-80);
//   · Packet::getPathHashSize()     -> path_len = ((hash_size - 1) << 6) | hash_count, so 2-byte
//     path hashes are 0x40 with a count of 0 (Packet.h:80-81, Mesh.cpp:637-660);
//   · createGroupDatagram()         -> payload = channel hash (1 byte) + [2-byte MAC | ciphertext]
//     (Mesh.cpp:539-556), and the plaintext is timestamp(4, little-endian) + TXT_TYPE_PLAIN(0) +
//     "name: text" (helpers/BaseChatMesh.cpp:517-531);
//   · Utils::encryptThenMAC()       -> AES-128 ECB over zero-padded 16-byte blocks, MAC =
//     HMAC-SHA256(key = the 32-byte secret buffer, data = ciphertext) truncated to 2 bytes
//     (Utils.cpp:100-172).
//
// ⚠️ The one number that looks wrong and is not: MeshCore keys the HMAC with PUB_KEY_SIZE (32) bytes
// even for a 128-bit channel key. It is the *buffer* that is 32 bytes, 16 key + 16 zeros
// (BaseChatMesh.cpp:933-945), and HMAC-SHA256 zero-pads both 16 and 32-byte keys to the same 64-byte
// block, so a 16-byte key gives the same MAC either way. Keeping the full 32-byte buffer here means we
// also match a 256-bit channel, which hashes over all 32 bytes.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace meshcore {

// --- sizes, from lib/meshcore/MeshCore.h ---------------------------------------
constexpr size_t   kMaxPayload   = 184;   // MAX_PACKET_PAYLOAD
constexpr size_t   kMaxPath      = 64;    // MAX_PATH_SIZE
constexpr size_t   kSecretBytes  = 32;    // GroupChannel::secret[PUB_KEY_SIZE]
constexpr size_t   kKeyBytes     = 16;    // CIPHER_KEY_SIZE (a 128-bit channel key)
constexpr size_t   kMacBytes     = 2;     // CIPHER_MAC_SIZE
constexpr size_t   kBlockBytes   = 16;    // CIPHER_BLOCK_SIZE
constexpr size_t   kMaxText      = 160;   // MAX_TEXT_LEN
constexpr size_t   kMaxPlain     = 168;   // MAX_PACKET_PAYLOAD - CIPHER_BLOCK_SIZE - 3 (Mesh.cpp:542)
constexpr size_t   kMaxNameChars = 31;

// --- header bits (Packet.h:5-11, 25-40) ----------------------------------------
// EST-445: the flood frames this app sends (an advert and a channel text) use 2-byte path hashes. The
// Settings RADIO section reads this ONE value, so the row cannot drift from what advertBuild and
// buildScopedChannelText are actually given.
constexpr uint8_t kFloodPathHashSize = 2;

constexpr uint8_t kRouteTransportFlood = 0x00;   // flood + two transport codes (Packet.h:14)
constexpr uint8_t kRouteFlood     = 0x01;
constexpr uint8_t kTypeGrpTxt     = 0x05;
constexpr uint8_t kTypeAdvert     = 0x04;
constexpr uint8_t kTxtTypePlain   = 0x00;

// A MeshCore group channel. `secret` is the 32-byte buffer MeshCore keeps; `hash` is the 1-byte
// channel hash that goes on the wire and comes from the secret, never from the visible name.
struct Channel {
    char    name[32];
    uint8_t secret[kSecretBytes];
    uint8_t hash;
};

// Take a 16-byte channel key: zero the rest of the buffer (MeshCore's 128-bit rule) and derive the
// channel hash. Returns false only for a null key.
bool channelSetKey(Channel& ch, const uint8_t key[kKeyBytes], const char* name = nullptr);

// A decoded frame, pointing into the caller's buffer (no copies, no allocation).
struct View {
    uint8_t        header      = 0;
    uint8_t        path_len    = 0;
    uint8_t        hash_size   = 1;   // 1..3
    uint8_t        hash_count  = 0;
    const uint8_t* path        = nullptr;
    uint8_t        path_bytes  = 0;
    const uint8_t* payload     = nullptr;
    uint16_t       payload_len = 0;
    const uint8_t* transport   = nullptr;   // 4 bytes or nullptr

    uint8_t route() const { return header & 0x03; }
    uint8_t type() const { return (header >> 2) & 0x0F; }
};

// Parse a frame from the air. False on any malformed field (bad path_len, truncated path/payload).
bool decode(const uint8_t* buf, size_t len, View* out);

// Build a complete flood packet carrying a channel text. `hash_size` is 1..3 and must match what the
// mesh runs (the SLO mesh is 2). Returns the frame length, or 0 if it cannot fit or an argument is bad.
size_t buildGroupText(const Channel& ch, uint32_t timestamp, const char* sender, const char* text,
                      uint8_t hash_size, uint8_t* out, size_t out_size);

// Open a GRP_TXT frame for this channel: check the channel hash, verify the MAC, decrypt, and split the
// MeshCore plaintext ("name: text"). False when the packet is not ours, the MAC fails, or it is not a
// plain group text. Outputs are always NUL-terminated on success.
bool openGroupText(const Channel& ch, const View& pkt, char* sender, size_t sender_size, char* text,
                   size_t text_size, uint32_t* timestamp);

// MeshCore's Utils::encryptThenMAC / MACThenDecrypt, exposed so the direct-message codec (mc_dm) uses the SAME
// implementation instead of keeping a second copy of the crypto. `sealPayload` returns MAC+ciphertext length;
// `openPayload` returns the decrypted (block-padded) length, or 0 when the MAC does not match.
size_t sealPayload(const uint8_t secret[kSecretBytes], uint8_t* dest, size_t dest_size, const uint8_t* src,
                   size_t src_len);
size_t openPayload(const uint8_t secret[kSecretBytes], uint8_t* dest, size_t dest_size, const uint8_t* src,
                   size_t src_len);

// Exposed for the tests: the exact payload MeshCore puts inside a group-text packet.
size_t buildPlain(uint8_t* out, size_t out_size, uint32_t timestamp, const char* sender,
                  const char* text);
bool   parsePlain(const uint8_t* plain, size_t len, char* sender, size_t sender_size, char* text,
                  size_t text_size, uint32_t* timestamp);

// ---- S513/EST-441: the seen-ring, so our own frame coming back is not "incoming" -------------------------
// Upstream's rule, read in S513 (`firmware/lib/meshcore`): `Packet::calculatePacketHash` (Packet.cpp:41-50)
// hashes `payload_type || payload` and NOT the path, and every send marks its packet as seen —
// `Mesh.cpp:651, 680, 713, 723, 736` say it in as many words: *"mark this packet as already sent in case it is
// rebroadcast back to us"*. Leaving the path out is the point: a repeater forwards our frame with its own hash
// appended and changes nothing else, so the copy we hear back has the same payload and must match.
// The ring itself is `helpers/SimpleMeshTables.h:52-57` (160 slots there, because a repeater forwards for the
// whole mesh; a leaf node only needs its own traffic).
constexpr size_t kSeenSlots = 32;

// The fingerprint of a decoded frame: sha256(payload)[0..3], little-endian. The payload only — see above.
uint32_t frameFingerprint(const View& pkt);

struct SeenRing {
    uint32_t fp[kSeenSlots]   = {0};
    bool     ours[kSeenSlots] = {false};   // this entry was put there by US transmitting the frame
    uint8_t  next             = 0;         // the slot the next entry reuses

    // RX: true when this frame was already handled — drop it — and `ours_out` says whether it was our own frame
    // heard back. False when it is new; the entry is then stored as "not ours", so a duplicate flood copy is
    // caught the second time. A fingerprint of 0 is never matched (sha256 returning zero is not a frame).
    bool seen(uint32_t f, bool* ours_out);

    // TX: register a frame we are about to transmit, BEFORE it goes out. `ours` is what makes the echo
    // distinguishable from a peer's duplicate.
    void noteOurs(uint32_t f);

    void reset();
};

}  // namespace meshcore
