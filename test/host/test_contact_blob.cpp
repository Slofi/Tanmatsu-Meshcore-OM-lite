// SPDX-License-Identifier: MIT
// Host tests for the contact table's NVS blob (S513/EST-437). The device writes this exact codec before it
// goes to sleep and reads it back before the radio task exists, so a round trip that loses a field would
// silently open a DM against a contact that is not quite the one that was stored.
#include <stdio.h>
#include <string.h>

#include "contact_blob.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                       \
      return;                                                             \
    }                                                                     \
  } while (0)

#define CHECK_EQ(got, want)                                                          \
  do {                                                                               \
    ++g_checks;                                                                      \
    const long g_ = static_cast<long>(got);                                          \
    const long w_ = static_cast<long>(want);                                         \
    if (g_ != w_) {                                                                  \
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,        \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

om::mc::Contact makeContact(uint8_t seed) {
  om::mc::Contact c;
  for (size_t i = 0; i < sizeof(c.pub); i++) c.pub[i] = (uint8_t)(seed + i);
  snprintf(c.name, sizeof(c.name), "Node %u", (unsigned)seed);
  c.type      = (uint8_t)(1 + (seed % 4));
  c.path_len  = (uint8_t)(seed % 9);
  c.remote_ts = 0x11223344u + seed;
  c.heard_at  = 0xA1B2C3D4u - seed;
  c.rssi      = (int16_t)(-40 - (int)seed);
  c.snr_x10   = (int16_t)(-125 + (int)seed);
  c.favourite = (seed % 2) != 0;
  c.dm_seen   = 100u + seed;
  c.dm_seen_log = 200u + seed;
  c.dm_log_next = 300u + seed;
  // S515/EST-461: the NEW flag and the advert's position, both part of a version-3 record.
  c.fresh        = (seed % 3) != 0;
  c.has_position = (seed % 5) != 0;
  c.lat_e6       = (int32_t)(461000000 + (int)seed * 1000);
  c.lon_e6       = (int32_t)(-144000000 - (int)seed * 500);
  // S513/EST-439: a route as MeshCore keeps it. Every fourth row has none (0xFF); of the rest, every third is a
  // ZERO-HOP direct route (0x40, a neighbour heard directly), the others a two-hop one.
  if (seed % 4 == 0) {
    c.out_path_len = om::mc::kOutPathUnknown;
  } else {
    c.out_path_len = (seed % 3 == 0) ? 0x40 : 0x41;
    // A zero-hop route carries NO path bytes; only a two-hop one has them.
    if (c.out_path_len != 0x40) {
      c.out_path[0] = seed;
      c.out_path[1] = (uint8_t)(seed + 1);
    }
  }
  return c;
}

void sameContact(const om::mc::Contact& a, const om::mc::Contact& b) {
  CHECK(memcmp(a.pub, b.pub, sizeof(a.pub)) == 0);
  CHECK(strcmp(a.name, b.name) == 0);
  CHECK_EQ(a.type, b.type);
  CHECK_EQ(a.path_len, b.path_len);
  CHECK_EQ(a.remote_ts, b.remote_ts);
  CHECK_EQ(a.heard_at, b.heard_at);
  CHECK_EQ(a.rssi, b.rssi);
  CHECK_EQ(a.snr_x10, b.snr_x10);
  CHECK_EQ(a.favourite, b.favourite);
  CHECK_EQ(a.dm_seen, b.dm_seen);
  CHECK_EQ(a.dm_seen_log, b.dm_seen_log);
  CHECK_EQ(a.dm_log_next, b.dm_log_next);
  CHECK_EQ(a.out_path_len, b.out_path_len);
  CHECK(memcmp(a.out_path, b.out_path, sizeof(a.out_path)) == 0);
  CHECK_EQ(a.fresh, b.fresh);
  CHECK_EQ(a.has_position, b.has_position);
  // A coordinates pair without the flag is NOT trusted back off the blob (the decoder zeroes it), which is the
  // same rule as an advert without ADV_LATLON_MASK.
  if (a.has_position) {
    CHECK_EQ(a.lat_e6, b.lat_e6);
    CHECK_EQ(a.lon_e6, b.lon_e6);
  } else {
    CHECK_EQ(b.lat_e6, 0);
    CHECK_EQ(b.lon_e6, 0);
  }
}

// 1. An empty table is a legal blob: just the header, and it decodes to zero rows.
void emptyTable() {
  om::mc::ContactStore s;
  uint8_t out[8] = {0};
  CHECK_EQ(om::mc::contactsEncode(s, out, sizeof(out)), om::mc::kContactBlobHeaderBytes);
  CHECK_EQ(out[0], om::mc::kContactBlobVersion);
  CHECK_EQ(out[1], 0);   // S515/EST-463: reserved from v4 (the u8 count could not say 2,000)
  CHECK_EQ((unsigned)out[2] | ((unsigned)out[3] << 8), 0);   // count u16
  CHECK_EQ((unsigned)out[4] | ((unsigned)out[5] << 8), om::mc::kContactBlobRecordBytes);

  om::mc::ContactStore d;
  d.count = 7;   // must be replaced, not appended
  CHECK(om::mc::contactsDecode(out, om::mc::kContactBlobHeaderBytes, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(d.count, 0);
}

// 2. Every field survives a round trip. The list is mixed (favourite and not, signed SNR) so no field can be
//    accidentally carried by another.
void roundTrip() {
  om::mc::ContactStore s;
  s.count = 3;
  s.c[0]  = makeContact(1);
  s.c[1]  = makeContact(17);
  s.c[2]  = makeContact(99);

  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  CHECK_EQ(n, om::mc::kContactBlobHeaderBytes + 3 * om::mc::kContactBlobRecordBytes);

  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, n, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(d.count, 3);
  for (size_t i = 0; i < 3; i++) sameContact(s.c[i], d.c[i]);
}

// 3. A blob from a newer/older format is refused, and the live table is NOT touched.
void wrongVersionIsIgnored() {
  om::mc::ContactStore s;
  s.count = 1;
  s.c[0]  = makeContact(5);

  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  CHECK(n > 0);
  buf[0] = (uint8_t)(om::mc::kContactBlobVersion + 1);

  om::mc::ContactStore d;
  d.count = 1;
  d.c[0]  = makeContact(200);   // the marker that must survive
  CHECK(om::mc::contactsDecode(buf, n, d) == om::mc::ContactDecode::BadVersion);
  CHECK_EQ(d.count, 1);
  CHECK(strcmp(d.c[0].name, "Node 200") == 0);
  CHECK(strcmp(om::mc::contactDecodeText(om::mc::ContactDecode::BadVersion), "wrong version") == 0);
}

// 4. The header's record width is part of the format: a row written by a build with a different Contact
//    shape must not be read as if it were this one.
void wrongRecordSizeIsIgnored() {
  om::mc::ContactStore s;
  s.count = 1;
  s.c[0]  = makeContact(8);
  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  const uint16_t want = (uint16_t)om::mc::kContactBlobRecordBytes;
  buf[4] = (uint8_t)((want - 1) & 0xFF);
  buf[5] = (uint8_t)((want - 1) >> 8);

  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, n, d) == om::mc::ContactDecode::BadRecordSize);
  CHECK_EQ(d.count, 0);
}

// 5. A torn NVS write (the tail missing) is refused, at every cut, and never half-fills the table.
void truncatedAtEveryLength() {
  om::mc::ContactStore s;
  s.count = 2;
  s.c[0]  = makeContact(3);
  s.c[1]  = makeContact(4);
  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  CHECK(n > om::mc::kContactBlobHeaderBytes);

  for (size_t cut = 1; cut < n; cut++) {
    om::mc::ContactStore d;
    d.count = 1;
    d.c[0]  = makeContact(201);
    CHECK(om::mc::contactsDecode(buf, cut, d) == om::mc::ContactDecode::Truncated);
    CHECK_EQ(d.count, 1);
    CHECK(strcmp(d.c[0].name, "Node 201") == 0);
  }
  // a header alone, with a count that promises rows, is truncated too
  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, om::mc::kContactBlobHeaderBytes, d) == om::mc::ContactDecode::Truncated);
}

// 6. Extra bytes past the last record are not silently accepted: the size must be exactly what the header
//    accounts for, so a record appended by a newer format cannot be mistaken for padding.
void extraBytesAreRejected() {
  om::mc::ContactStore s;
  s.count = 1;
  s.c[0]  = makeContact(6);
  uint8_t buf[om::mc::kContactBlobMaxBytes + 1];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  CHECK(om::mc::contactsDecode(buf, n + 1, s) == om::mc::ContactDecode::BadSize);
}

// 7. A count larger than the table is refused before any record is read (a corrupt count cannot write past 32).
void tooManyContacts() {
  // S515/EST-463: a v4 header, count u16 = one more than the table can hold.
  uint8_t buf[om::mc::kContactBlobHeaderBytes] = {
      om::mc::kContactBlobVersion, 0,
      (uint8_t)((om::mc::kMaxContacts + 1) & 0xFF), (uint8_t)((om::mc::kMaxContacts + 1) >> 8),
      (uint8_t)(om::mc::kContactBlobRecordBytes & 0xFF),
      (uint8_t)(om::mc::kContactBlobRecordBytes >> 8)};
  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, sizeof(buf), d) == om::mc::ContactDecode::BadCount);
  CHECK_EQ(d.count, 0);
}

// 8. The full table (all 32 rows) round-trips: the blob is deliberately large, and the header's one-byte
//    count must reach exactly kMaxContacts without wrapping or cutting the last row.
void fullTable() {
  om::mc::ContactStore s;
  s.count = om::mc::kMaxContacts;
  for (size_t i = 0; i < s.count; i++) s.c[i] = makeContact((uint8_t)(i * 7 + 1));

  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  CHECK_EQ(n, om::mc::kContactBlobMaxBytes);
  CHECK_EQ((unsigned)buf[2] | ((unsigned)buf[3] << 8), om::mc::kMaxContacts);   // the count is u16 from v4

  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, n, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(d.count, om::mc::kMaxContacts);
  for (size_t i = 0; i < s.count; i++) {
    CHECK(memcmp(s.c[i].pub, d.c[i].pub, 32) == 0);
    CHECK(strcmp(s.c[i].name, d.c[i].name) == 0);
    CHECK_EQ(s.c[i].heard_at, d.c[i].heard_at);
  }
}

// 9. A name without a terminator in the blob cannot run off the field after decode.
void nameIsTerminated() {
  om::mc::ContactStore s;
  s.count = 1;
  s.c[0]  = makeContact(9);
  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  const size_t name_off = om::mc::kContactBlobHeaderBytes + 32;
  memset(buf + name_off, 'A', om::mc::kNameBytes);   // no NUL anywhere

  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, n, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(strlen(d.c[0].name), om::mc::kNameBytes - 1);
  CHECK_EQ(d.c[0].name[om::mc::kNameBytes - 1], 0);
}

// 10. S513/EST-439: the route really round-trips, and a ZERO-HOP direct route (0x40) stays DIRECT — it must not
//     be flattened into "unknown". An unknown route is stored as OUT_PATH_UNKNOWN, never as a zero-length path.
void routeRoundTrip() {
  om::mc::ContactStore s;
  s.count = 3;
  s.c[0]  = makeContact(10);   // 10 % 4 != 0, 10 % 3 != 0 -> 0x41 with two path bytes (10, 11)
  s.c[0].out_path_len = om::mc::kOutPathUnknown;
  memset(s.c[0].out_path, 0, sizeof(s.c[0].out_path));
  s.c[1]  = makeContact(15);   // 15 % 4 != 0 and 15 % 3 == 0 -> 0x40, zero hops
  s.c[2]  = makeContact(13);   // 13 % 3 != 0 -> 0x41
  uint8_t buf[om::mc::kContactBlobMaxBytes];
  memset(buf, 0xCC, sizeof(buf));
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  CHECK_EQ(n, om::mc::kContactBlobHeaderBytes + 3 * om::mc::kContactBlobRecordBytes);
  CHECK_EQ(buf[0], om::mc::kContactBlobVersion);
  CHECK_EQ(buf[0], 4);   // S515/EST-463: v4 = v3 plus in_radio and a u16 count

  const size_t route_off = om::mc::kContactBlobHeaderBytes + 83;   // after dm_log_next
  CHECK_EQ(buf[route_off], om::mc::kOutPathUnknown);               // row 0: no route, not a zero-length one
  for (size_t i = 1; i <= om::mc::kContactBlobRouteBytes; i++) CHECK_EQ(buf[route_off + i], 0);
  const size_t route1 = route_off + om::mc::kContactBlobRecordBytes;
  CHECK_EQ(buf[route1], 0x40);                                     // row 1: zero hops, direct
  const size_t route2 = route1 + om::mc::kContactBlobRecordBytes;
  CHECK_EQ(buf[route2], 0x41);
  CHECK_EQ(buf[route2 + 1], 13);
  CHECK_EQ(buf[route2 + 2], 14);

  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(buf, n, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(d.count, 3);
  for (size_t i = 0; i < 3; i++) sameContact(s.c[i], d.c[i]);
  CHECK(!om::mc::contactHasOutPath(d.c[0]));
  CHECK_EQ(d.c[1].out_path_len, 0x40);
  CHECK(om::mc::contactHasOutPath(d.c[1]));
}

// 10b. A version-1 blob (EST-437: the slot was reserved and zeroed) still loads — every route as OUT_PATH_UNKNOWN.
//      And a version-2 blob whose route byte is not a valid encoding is treated as unknown rather than trusted.
void oldBlobAndBadRoute() {
  om::mc::ContactStore s;
  s.count = 1;
  s.c[0]  = makeContact(13);   // 0x41 + bytes 13, 14
  uint8_t buf[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncode(s, buf, sizeof(buf));
  const size_t route_off = om::mc::kContactBlobHeaderBytes + 83;

  // A REAL version-1 blob: same record, 148 bytes wide, the route slot zeroed exactly as EST-437 wrote it.
  // S515/EST-463: the record MOVES to offset 4 for a v1 blob (its header is 4 bytes, v4's is 6), so it is copied
  // across rather than reinterpreted in place.
  uint8_t v1[om::mc::kContactBlobMaxBytes];
  memset(v1, 0, sizeof(v1));
  memcpy(v1 + om::mc::kContactBlobHeaderBytesLegacy, buf + om::mc::kContactBlobHeaderBytes,
         om::mc::kContactBlobRecordBytesV2);
  v1[0] = 1;
  v1[1] = 1;
  v1[2] = (uint8_t)(om::mc::kContactBlobRecordBytesV2 & 0xFF);
  v1[3] = (uint8_t)(om::mc::kContactBlobRecordBytesV2 >> 8);
  const size_t v1_route = om::mc::kContactBlobHeaderBytesLegacy + 83;
  memset(v1 + v1_route, 0, 1 + om::mc::kContactBlobRouteBytes);   // exactly what EST-437 wrote
  const size_t n1 = om::mc::kContactBlobHeaderBytesLegacy + om::mc::kContactBlobRecordBytesV2;
  om::mc::ContactStore d;
  CHECK(om::mc::contactsDecode(v1, n1, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(d.c[0].fresh, 0);            // S515/EST-461: an old blob knows nothing about new contacts
  CHECK_EQ(d.c[0].has_position, 0);
  CHECK_EQ(d.c[0].lat_e6, 0);
  CHECK_EQ(d.c[0].out_path_len, om::mc::kOutPathUnknown);
  CHECK_EQ(d.c[0].out_path[0], 0);
  CHECK_EQ(d.c[0].name[0], s.c[0].name[0]);   // the rest of the row is untouched

  uint8_t bad[om::mc::kContactBlobMaxBytes];
  memcpy(bad, buf, n);
  bad[route_off] = 0xC1;                                        // hash_size 4: reserved, never a route
  memset(bad + route_off + 1, 0xAA, om::mc::kContactBlobRouteBytes);
  CHECK(om::mc::contactsDecode(bad, n, d) == om::mc::ContactDecode::Ok);
  CHECK_EQ(d.c[0].out_path_len, om::mc::kOutPathUnknown);
}

// 11. A short output buffer gets a refusal, and a capacity query is possible without writing.
void shortBufferRefused() {
  om::mc::ContactStore s;
  s.count = 2;
  s.c[0]  = makeContact(11);
  s.c[1]  = makeContact(12);
  const size_t need = om::mc::kContactBlobHeaderBytes + 2 * om::mc::kContactBlobRecordBytes;
  CHECK_EQ(om::mc::contactsEncode(s, nullptr, need), 0);
  uint8_t small[8];
  CHECK_EQ(om::mc::contactsEncode(s, small, sizeof(small)), 0);
}

}  // namespace

int main() {
  emptyTable();
  roundTrip();
  wrongVersionIsIgnored();
  wrongRecordSizeIsIgnored();
  truncatedAtEveryLength();
  extraBytesAreRejected();
  tooManyContacts();
  fullTable();
  nameIsTerminated();
  routeRoundTrip();
  oldBlobAndBadRoute();
  shortBufferRefused();
  if (g_failures == 0) {
    printf("test_contact_blob: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  printf("test_contact_blob: %d checks, %d FAILURES\n", g_checks, g_failures);
  return 1;
}
