// SPDX-License-Identifier: MIT
// Host tests for S515/EST-464 (EST-463 items 3 and 5): the two node files, the boot merge, the one-time NVS
// migration, and the step-0 rule that the NVS fallback encodes ONLY radio rows, never more than 32.
#include <stdio.h>
#include <string.h>

#include "contact_blob.h"
#include "mc_store.h"
#include "node_store.h"
#include "node_table.h"

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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,       \
             #got, #want, g_, w_);                                                   \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

void keyFor(uint8_t out[32], uint32_t seed) {
  memset(out, 0, 32);
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)((seed >> (8 * i)) & 0xFF);
  out[4] = (uint8_t)(seed * 7 + 3);
  out[31] = 0xA5;
}

om::mc::Contact makeNode(uint32_t seed, bool radio, uint32_t heard) {
  om::mc::Contact c;
  keyFor(c.pub, seed);
  snprintf(c.name, sizeof(c.name), "node-%u", (unsigned)seed);
  c.type      = 1;
  c.path_len  = (uint8_t)(seed % 4);
  c.remote_ts = 1000u + seed;
  c.heard_at  = heard;
  c.rssi      = -70;
  c.snr_x10   = 25;
  c.fresh     = seed % 2 == 0;
  c.favourite = seed % 3 == 0;
  c.in_radio  = radio;
  c.out_path_len = radio ? 0x41 : om::mc::kOutPathUnknown;
  if (radio) {
    c.out_path[0] = 0xAA;
    c.out_path[1] = 0xBB;
  }
  return c;
}

void addContact(om::mc::ContactStore& s, const om::mc::Contact& c) {
  CHECK(s.count < s.cap);
  s.c[s.count++] = c;
}

// ---- 1. codec ------------------------------------------------------------------------------------
void testRadioFileRoundTrip() {
  om::mc::ContactStore s;
  addContact(s, makeNode(1, true, 100));
  addContact(s, makeNode(2, false, 200));
  addContact(s, makeNode(3, true, 300));

  static uint8_t buf[om::mc::kNodeFileMaxBytes];
  const size_t n = om::mc::nodeFileEncode(s, true, om::mc::kMaxRadio, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK_EQ(n, om::mc::kNodeFileHeaderBytes + 2 * om::mc::kContactBlobRecordBytes + om::mc::kNodeFileCrcBytes);
  // Count in the header is the RADIO count, not the table count.
  CHECK_EQ(buf[8] | (buf[9] << 8), 2);

  om::mc::ContactStore back;
  CHECK_EQ(om::mc::nodeFileDecode(buf, n, back), om::mc::NodeFileResult::Ok);
  CHECK_EQ(back.count, 2);
  CHECK_EQ(memcmp(back.c[0].pub, s.c[0].pub, 32), 0);
  CHECK_EQ(memcmp(back.c[1].pub, s.c[2].pub, 32), 0);
  CHECK(back.c[0].in_radio && back.c[1].in_radio);
}

void testArchiveFileRoundTrip() {
  om::mc::ContactStore s;
  addContact(s, makeNode(10, false, 100));
  addContact(s, makeNode(11, true, 200));
  static uint8_t buf[om::mc::kNodeFileMaxBytes];
  const size_t n = om::mc::nodeFileEncode(s, false, 0, buf, sizeof(buf));
  CHECK(n > 0);
  om::mc::ContactStore back;
  CHECK_EQ(om::mc::nodeFileDecode(buf, n, back), om::mc::NodeFileResult::Ok);
  CHECK_EQ(back.count, 2);
  CHECK(!back.c[0].in_radio);
  CHECK(back.c[1].in_radio);
  CHECK_EQ(back.c[1].heard_at, s.c[1].heard_at);
  CHECK_EQ(strcmp(back.c[0].name, s.c[0].name), 0);
}

void testCodecRefusals() {
  om::mc::ContactStore s;
  addContact(s, makeNode(20, true, 100));
  static uint8_t buf[om::mc::kNodeFileMaxBytes];
  size_t n = om::mc::nodeFileEncode(s, true, 0, buf, sizeof(buf));
  CHECK(n > 0);
  om::mc::ContactStore back;

  CHECK_EQ(om::mc::nodeFileDecode(buf, 0, back), om::mc::NodeFileResult::Empty);
  CHECK_EQ(back.count, 0);

  uint8_t copy[om::mc::kNodeFileMaxBytes];
  memcpy(copy, buf, n);
  copy[0] ^= 0xFF;   // magic
  CHECK_EQ(om::mc::nodeFileDecode(copy, n, back), om::mc::NodeFileResult::BadMagic);

  memcpy(copy, buf, n);
  copy[4] = 9;       // version
  CHECK_EQ(om::mc::nodeFileDecode(copy, n, back), om::mc::NodeFileResult::BadVersion);

  memcpy(copy, buf, n);
  copy[6] = 158;     // record size
  CHECK_EQ(om::mc::nodeFileDecode(copy, n, back), om::mc::NodeFileResult::BadRecordSize);

  memcpy(copy, buf, n);
  copy[8] = 0xFF;    // count
  copy[9] = 0x7F;
  CHECK_EQ(om::mc::nodeFileDecode(copy, n, back), om::mc::NodeFileResult::BadCount);

  CHECK_EQ(om::mc::nodeFileDecode(buf, n - 1, back), om::mc::NodeFileResult::Truncated);

  memcpy(copy, buf, n);
  copy[n] = 0;
  CHECK_EQ(om::mc::nodeFileDecode(copy, n + 1, back), om::mc::NodeFileResult::BadSize);

  memcpy(copy, buf, n);
  copy[om::mc::kNodeFileHeaderBytes + 3] ^= 0x01;   // a record byte
  CHECK_EQ(om::mc::nodeFileDecode(copy, n, back), om::mc::NodeFileResult::BadCrc);
}

// S516/EST-472 item 2: the temp file a power cut leaves behind must be CRC-checked BEFORE readNodeFileOrTemp()
// renames it over the real name. That check lives in core as nodeFileCheck(), and this pins that it answers for
// the same bytes nodeFileDecode does — including the torn temp (a short file with a good header).
void testFileCheckBeforeTrust() {
  om::mc::ContactStore s;
  addContact(s, makeNode(30, true, 100));
  addContact(s, makeNode(31, true, 200));
  static uint8_t buf[om::mc::kNodeFileMaxBytes];
  const size_t n = om::mc::nodeFileEncode(s, true, 0, buf, sizeof(buf));
  CHECK(n > (size_t)om::mc::kNodeFileCrcBytes);

  CHECK_EQ(om::mc::nodeFileCheck(buf, n), om::mc::NodeFileResult::Ok);
  CHECK_EQ(om::mc::nodeFileCheck(buf, 0), om::mc::NodeFileResult::Empty);
  CHECK_EQ(om::mc::nodeFileCheck(nullptr, 0), om::mc::NodeFileResult::Empty);

  // A TEMP WRITE CUT SHORT: the header is intact and claims two records, the file holds one. This is exactly
  // what the old readNodeFileOrTemp() renamed into place; the check must refuse it.
  const size_t one_record = om::mc::kNodeFileHeaderBytes + om::mc::kContactBlobRecordBytes;
  CHECK(one_record < n - om::mc::kNodeFileCrcBytes);
  CHECK_EQ(om::mc::nodeFileCheck(buf, one_record), om::mc::NodeFileResult::Truncated);

  static uint8_t copy[om::mc::kNodeFileMaxBytes];
  memcpy(copy, buf, n);
  copy[n - 1] ^= 0xFF;   // the trailing CRC, torn
  CHECK_EQ(om::mc::nodeFileCheck(copy, n), om::mc::NodeFileResult::BadCrc);
  memcpy(copy, buf, n);
  copy[8] = 0xFF;        // count 0x7FFF: more rows than the table holds
  copy[9] = 0x7F;
  CHECK_EQ(om::mc::nodeFileCheck(copy, n), om::mc::NodeFileResult::BadCount);
  memcpy(copy, buf, n);
  copy[0] ^= 0xFF;
  CHECK_EQ(om::mc::nodeFileCheck(copy, n), om::mc::NodeFileResult::BadMagic);
  // The same answer nodeFileDecode gives, so the two can never disagree about the temp.
  CHECK_EQ((int)om::mc::nodeFileCheck(buf, n), (int)om::mc::nodeFileDecode(buf, n, s));
}

// ---- 2. the step-0 NVS fallback encoder ----------------------------------------------------------
void testNvsEncoderOnlyRadioAndCapped() {
  om::mc::ContactStore s;
  for (uint32_t i = 0; i < 40; i++) addContact(s, makeNode(100 + i, i < 35, 1000 + i));   // 35 radio
  static uint8_t blob[om::mc::kContactBlobMaxBytes];
  const size_t n = om::mc::contactsEncodeRadio(s, blob, sizeof(blob), 32);
  CHECK(n > 0);
  CHECK_EQ(blob[0], om::mc::kContactBlobVersion);
  CHECK_EQ(blob[2] | (blob[3] << 8), 32);   // u16 count, capped

  om::mc::ContactStore back;
  CHECK_EQ(om::mc::contactsDecode(blob, n, back), om::mc::ContactDecode::Ok);
  CHECK_EQ(back.count, 32);
  for (size_t i = 0; i < back.count; i++) {
    CHECK(back.c[i].in_radio);   // NEVER a non-radio row…
    CHECK_EQ(back.c[i].pub[0], (uint8_t)(100 + i));   // …and never more than 32
  }

  // A table with no radio rows writes an empty blob, not one row per archive node.
  om::mc::ContactStore a;
  addContact(a, makeNode(200, false, 10));
  addContact(a, makeNode(201, false, 20));
  const size_t en = om::mc::contactsEncodeRadio(a, blob, sizeof(blob), 32);
  CHECK(en > 0);
  CHECK_EQ(blob[2] | (blob[3] << 8), 0);
  CHECK_EQ(en, om::mc::kContactBlobHeaderBytes);
}

// ---- 3. merge ------------------------------------------------------------------------------------
void testMergeRules() {
  om::mc::ContactStore live;     // the radio file: node 1 has the radio mark and a route
  om::mc::Contact radio = makeNode(1, true, 100);
  radio.fresh = false;
  addContact(live, radio);

  om::mc::ContactStore archive;
  om::mc::Contact newer = makeNode(1, true, 500);   // newer sighting, no route of its own
  newer.out_path_len = om::mc::kOutPathUnknown;
  newer.name[0] = '\0';
  snprintf(newer.name, sizeof(newer.name), "renamed");
  newer.fresh = true;
  addContact(archive, newer);
  addContact(archive, makeNode(2, true, 200));      // archive-only from the radio file's point of view

  const om::mc::MergeResult m = om::mc::mergeArchive(live, archive);
  CHECK_EQ(m.archive_rows, 2);
  CHECK_EQ(m.added, 1);
  CHECK_EQ(m.updated, 1);
  CHECK_EQ(live.count, 2);

  const int i1 = om::mc::contactsFind(live, newer.pub);
  CHECK(i1 >= 0);
  CHECK(live.c[i1].in_radio);                                  // radio wins the mark
  CHECK_EQ(live.c[i1].out_path_len, 0x41);                     // radio wins the route
  CHECK_EQ(live.c[i1].out_path[0], 0xAA);
  CHECK_EQ(strcmp(live.c[i1].name, "renamed"), 0);             // newer heard_at wins the name
  CHECK_EQ(live.c[i1].heard_at, 500);
  CHECK(live.c[i1].fresh);                                     // fresh comes from the archive

  const int i2 = om::mc::contactsFind(live, archive.c[1].pub);
  CHECK(i2 >= 0);
  CHECK(!live.c[i2].in_radio);                                 // radio.bin is the only radio authority
}

// ---- 4. migration from v2 / v3 / v4 ---------------------------------------------------------------
void buildLegacyBlob(const om::mc::Contact& c, uint8_t version, uint8_t* out, size_t* out_len) {
  uint8_t rec[om::mc::kContactBlobRecordBytes];
  om::mc::contactEncodeRecord(c, rec);
  const size_t stride = version == 2 ? om::mc::kContactBlobRecordBytesV2 : om::mc::kContactBlobRecordBytesV3;
  out[0] = version;
  out[1] = 1;
  out[2] = (uint8_t)(stride & 0xFF);
  out[3] = (uint8_t)(stride >> 8);
  memcpy(out + 4, rec, stride);
  *out_len = 4 + stride;
}

void testMigrationFromLegacyBlobs() {
  om::mc::Contact src = makeNode(300, false, 100);   // an old blob had no in_radio field at all
  src.out_path_len = 0x41;
  for (uint8_t version = 2; version <= 3; version++) {
    uint8_t blob[512];
    size_t   len = 0;
    buildLegacyBlob(src, version, blob, &len);
    om::mc::ContactStore s;
    CHECK_EQ(om::mc::contactsDecode(blob, len, s), om::mc::ContactDecode::Ok);
    CHECK_EQ(s.count, 1);
    CHECK(!s.c[0].in_radio);   // the old format has no mark: false right after decode
    CHECK_EQ(om::mc::migrateNvsToRadio(s, version), 1);
    CHECK(s.c[0].in_radio);    // …and every row becomes a radio node
  }

  // v4 carries the flags: migration must not invent one.
  om::mc::ContactStore s;
  addContact(s, makeNode(400, true, 100));
  addContact(s, makeNode(401, false, 200));
  uint8_t blob[om::mc::kContactBlobMaxBytes];
  const size_t len = om::mc::contactsEncode(s, blob, sizeof(blob));
  CHECK(len > 0);
  om::mc::ContactStore back;
  CHECK_EQ(om::mc::contactsDecode(blob, len, back), om::mc::ContactDecode::Ok);
  CHECK_EQ(om::mc::migrateNvsToRadio(back, om::mc::kContactBlobVersion), 1);
  CHECK(back.c[0].in_radio);
  CHECK(!back.c[1].in_radio);
}

// ---- 5. the radio mark helper --------------------------------------------------------------------
void testMarkAllInRadioCaps() {
  om::mc::ContactStore s;
  for (uint32_t i = 0; i < 5; i++) addContact(s, makeNode(500 + i, false, i));
  CHECK_EQ(om::mc::markAllInRadio(s, 3), 3);
  CHECK_EQ(om::mc::radioCount(s), 3);
}

}  // namespace

// S515 (Claude, found on the device): the "did the radio file change?" signature. The device skipped every
// radio.bin write after the migration because it compared CRC-32s of WHOLE files, and a file that ends with its
// own CRC-32 always hashes to the residue 0x2144DF1C. Moving a node out of the radio must change the signature.
void testSignatureSeesAChange() {
  om::mc::ContactStore s;
  addContact(s, makeNode(1, true, 100));
  addContact(s, makeNode(2, true, 200));
  static uint8_t a[om::mc::kNodeFileMaxBytes];
  static uint8_t b[om::mc::kNodeFileMaxBytes];
  const size_t na = om::mc::nodeFileEncode(s, true, om::mc::kMaxRadio, a, sizeof(a));
  s.c[1].in_radio = false;   // "Move to SD"
  const size_t nb = om::mc::nodeFileEncode(s, true, om::mc::kMaxRadio, b, sizeof(b));
  CHECK(na > 0 && nb > 0);
  // The trap, pinned: whole-file CRCs of two DIFFERENT files are the same constant.
  CHECK_EQ(om::mc::nodeFileCrc32(a, na), 0x2144DF1Cu);
  CHECK_EQ(om::mc::nodeFileCrc32(b, nb), 0x2144DF1Cu);
  CHECK(om::mc::nodeFileSignature(a, na) != om::mc::nodeFileSignature(b, nb));
  // The same content gives the same signature (so an unchanged list still skips the write).
  static uint8_t c[om::mc::kNodeFileMaxBytes];
  s.c[1].in_radio = true;
  const size_t nc = om::mc::nodeFileEncode(s, true, om::mc::kMaxRadio, c, sizeof(c));
  CHECK_EQ(om::mc::nodeFileSignature(a, na), om::mc::nodeFileSignature(c, nc));
  CHECK_EQ(om::mc::nodeFileSignature(a, 3), 0u);
}

int main() {
  testSignatureSeesAChange();
  testRadioFileRoundTrip();
  testArchiveFileRoundTrip();
  testCodecRefusals();
  testFileCheckBeforeTrust();
  testNvsEncoderOnlyRadioAndCapped();
  testMergeRules();
  testMigrationFromLegacyBlobs();
  testMarkAllInRadioCaps();
  printf("%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
