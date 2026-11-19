// SPDX-License-Identifier: MIT
// Host tests for channel administration (S514/EST-446). Real exit code: 0 = every check passed.
//
// The expected hashtag KEYS are computed by Python's hashlib in test/host/run.sh and passed in as
// `channel_vectors.h`: the test compares our sha256 path against an independent implementation, not
// against itself. No channel key from a real device appears here — every vector is a derivable hashtag
// or an obviously synthetic 0x01..0x10 key.
#include <stdio.h>
#include <string.h>

#include "channel_admin.h"
#include "channel_vectors.h"

namespace {

int g_checks = 0;
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
             #got, #want, g_, w_);                                                    \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

#define CHECK_STR(got, want)                                                         \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (strcmp((got), (want)) != 0) {                                                \
      printf("FAIL %s:%d  \"%s\" == \"%s\"\n", __FILE__, __LINE__, (got), (want));    \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0;
}

void hexToBytes(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)((hexNibble(hex[2 * i]) << 4) | hexNibble(hex[2 * i + 1]));
}

meshcore::Channel makeChannel(const char* name, const uint8_t key[16]) {
  meshcore::Channel ch{};
  meshcore::channelSetKey(ch, key, name);
  return ch;
}

// --------------------------------------------------------------------------- 1. hashtag normalisation
void test_tag_normalisation() {
  printf("test  hashtag normalisation (the L2's channels.cpp:424-441)\n");
  char out[40];

  CHECK_EQ(meshcore::channelTagCanonical("#Slovenija", out, sizeof(out)), 10);
  CHECK_STR(out, "#slovenija");
  CHECK_EQ(meshcore::channelTagCanonical("si", out, sizeof(out)), 3);
  CHECK_STR(out, "#si");
  CHECK_EQ(meshcore::channelTagCanonical("  #  SLO  venija  ", out, sizeof(out)), 10);
  CHECK_STR(out, "#slovenija");
  CHECK_EQ(meshcore::channelTagCanonical("##SLOVENIA", out, sizeof(out)), 9);
  CHECK_STR(out, "#slovenia");
  CHECK_EQ(meshcore::channelTagCanonical("#europe", out, sizeof(out)), 7);
  CHECK_STR(out, "#europe");

  // Refusals, and nothing written.
  CHECK_EQ(meshcore::channelTagCanonical("", out, sizeof(out)), -1);
  CHECK_STR(out, "");
  CHECK_EQ(meshcore::channelTagCanonical("#", out, sizeof(out)), -1);
  CHECK_STR(out, "");
  CHECK_EQ(meshcore::channelTagCanonical("   ##   ", out, sizeof(out)), -1);
  CHECK_STR(out, "");

  // 31 bytes including '#' is the cap; 32 is refused, never truncated into a different channel.
  const char* thirty = "#012345678901234567890123456789";   // # + 30 = 31
  CHECK_EQ(meshcore::channelTagCanonical(thirty, out, sizeof(out)), 31);
  const char* thirtyone = "#0123456789012345678901234567890";   // # + 31 = 32
  CHECK_EQ(meshcore::channelTagCanonical(thirtyone, out, sizeof(out)), -1);
  CHECK_STR(out, "");
}

// ------------------------------------------------------------------------------- 2. key from hashlib
void test_tag_key_against_python() {
  printf("test  hashtag key against Python hashlib (oracle in run.sh)\n");
  uint8_t key[16];
  for (int i = 0; i < kVectorsCount; ++i) {
    char tag[40];
    CHECK_EQ(meshcore::channelTagCanonical(kVectors[i].typed, tag, sizeof(tag)), (int)strlen(kVectors[i].tag));
    CHECK_STR(tag, kVectors[i].tag);
    CHECK(meshcore::channelTagKey(tag, key));
    uint8_t want[16];
    hexToBytes(kVectors[i].key16_hex, want, sizeof(want));
    CHECK(memcmp(key, want, sizeof(want)) == 0);
  }
  // The key is derived from the LOWERCASED tag: typing the same channel in caps must not create a
  // second network.
  char a[40], b[40];
  uint8_t ka[16], kb[16];
  meshcore::channelTagCanonical("#SLOVENIJA", a, sizeof(a));
  meshcore::channelTagCanonical("#slovenija", b, sizeof(b));
  CHECK(meshcore::channelTagKey(a, ka));
  CHECK(meshcore::channelTagKey(b, kb));
  CHECK(memcmp(ka, kb, 16) == 0);
}

// ------------------------------------------------------------------------------------- 3. base64 rules
void test_base64() {
  printf("test  base64 encode/decode (the L2's channels.cpp:76-85, 388-410)\n");
  uint8_t key[16];
  for (int i = 0; i < 16; ++i) key[i] = (uint8_t)(i + 1);   // synthetic, public test vector
  char b64[32];
  CHECK(meshcore::channelKeyToBase64(key, b64, sizeof(b64)));
  CHECK_STR(b64, kKey16B64);   // also from Python's base64

  uint8_t back[16];
  CHECK(meshcore::channelKeyFromBase64(b64, back));
  CHECK(memcmp(back, key, 16) == 0);

  // Pasted air is skipped (the L2 does this), '=' ends the text.
  char spaced[40];
  snprintf(spaced, sizeof(spaced), " %s \t", b64);
  CHECK(meshcore::channelKeyFromBase64(spaced, back));
  CHECK(memcmp(back, key, 16) == 0);

  // SHORT: 16 base64 chars decode to 12 bytes.
  CHECK(!meshcore::channelKeyFromBase64("AAAAAAAAAAAAAAAA", back));
  CHECK(!meshcore::channelKeyFromBase64("", back));
  // BAD CHARACTERS, one per class.
  CHECK(!meshcore::channelKeyFromBase64("AAAAAAAAAAAAAAAAAAAAAA!A", back));
  CHECK(!meshcore::channelKeyFromBase64("AAAAAAAAAAAAA-AAAAAAAAAA", back));
  CHECK(!meshcore::channelKeyFromBase64("AAAAAAAAAAAAAAAAAAAAAAAA\x1f", back));
  // MORE THAN 16 BYTES: refused (the one deliberate tightening over the L2).
  CHECK(!meshcore::channelKeyFromBase64("AAAAAAAAAAAAAAAAAAAAAAAAAAAA", back));
  // NULL / tiny output buffer.
  CHECK(!meshcore::channelKeyFromBase64(nullptr, back));
  char tiny[8];
  CHECK(!meshcore::channelKeyToBase64(key, tiny, sizeof(tiny)));

  // A local label: 1..31 printable, no air at either end.
  CHECK(meshcore::channelLabelValid("Public"));
  CHECK(!meshcore::channelLabelValid(""));
  CHECK(!meshcore::channelLabelValid(" lead"));
  CHECK(!meshcore::channelLabelValid("trail "));
  CHECK(!meshcore::channelLabelValid("tab\there"));
  CHECK(meshcore::channelLabelValid("Don't Panic"));
  char longname[40];
  memset(longname, 'a', sizeof(longname));
  longname[32] = 0;
  CHECK(!meshcore::channelLabelValid(longname));   // 32 bytes
  longname[31] = 0;
  CHECK(meshcore::channelLabelValid(longname));    // 31 bytes
}

// ---------------------------------------------------------------------------------- 4. duplicate refusal
void test_duplicate() {
  printf("test  duplicate refusal\n");
  uint8_t si[16], eu[16], other[16];
  CHECK(meshcore::channelTagKey("#si", si));
  CHECK(meshcore::channelTagKey("#europe", eu));
  for (int i = 0; i < 16; ++i) other[i] = 0x80;

  meshcore::ChannelSet set{};
  CHECK(meshcore::channelsAdd(set, si, "#si"));
  CHECK_EQ(set.count, 1);
  CHECK(meshcore::channelKeyKnown(set, si));
  CHECK(!meshcore::channelKeyKnown(set, eu));
  CHECK(!meshcore::channelKeyKnown(set, other));

  // Adding the same tag again is refused by the caller BEFORE any NVS write.
  CHECK(!meshcore::channelsAdd(set, si, "#si"));
  CHECK_EQ(set.count, 1);
}

// ------------------------------------------------------------------------------- 5. the safe rewrite
// The NVS "mcch" table, as far as these tests care: cnt, n<i>, k<i>, s<hh>.
struct FakeNvs {
  static constexpr int kSlots = 8;
  bool has_cnt = false;
  uint8_t cnt = 0;
  bool has_key[kSlots] = {};
  bool has_name[kSlots] = {};
  uint8_t key[kSlots][16] = {};
  char name[kSlots][32] = {};
  bool has_scope[256] = {};

  void seed(const meshcore::ChannelSet& set) {
    has_cnt = true;
    cnt = set.count;
    for (size_t i = 0; i < set.count; ++i) {
      has_key[i] = true;
      memcpy(key[i], set.ch[i].secret, 16);
      has_name[i] = true;
      snprintf(name[i], sizeof(name[i]), "%s", set.ch[i].name);
      has_scope[set.ch[i].hash] = true;
    }
  }

  void apply(const meshcore::ChannelNvsStep& st, const meshcore::ChannelSet& after) {
    switch (st.op) {
      case meshcore::ChannelNvsOp::SetKey:
        has_key[st.index] = true;
        memcpy(key[st.index], after.ch[st.index].secret, 16);
        break;
      case meshcore::ChannelNvsOp::SetName:
        has_name[st.index] = true;
        snprintf(name[st.index], sizeof(name[st.index]), "%s", after.ch[st.index].name);
        break;
      case meshcore::ChannelNvsOp::SetCount:
        has_cnt = true;
        cnt = after.count;
        break;
      case meshcore::ChannelNvsOp::EraseKey: has_key[st.index] = false; break;
      case meshcore::ChannelNvsOp::EraseName: has_name[st.index] = false; break;
      case meshcore::ChannelNvsOp::EraseScope: has_scope[st.hash] = false; break;
      case meshcore::ChannelNvsOp::None: break;
    }
  }

  // The SAME load rule as main/mc_link.cpp: cnt, then each slot's key, a missing key is skipped and a
  // duplicate key is refused by channelsAdd. Returns the loaded count.
  int load(meshcore::ChannelSet& out) const {
    out.count = 0;
    if (!has_cnt) return 0;
    const int n = cnt > kSlots ? kSlots : cnt;
    for (int i = 0; i < n; ++i) {
      if (!has_key[i]) continue;
      meshcore::channelsAdd(out, key[i], has_name[i] ? name[i] : "channel");
    }
    return out.count;
  }
};

meshcore::ChannelSet setOf(const meshcore::Channel(&chs)[3], int n) {
  meshcore::ChannelSet s{};
  for (int i = 0; i < n; ++i) meshcore::channelsAdd(s, chs[i].secret, chs[i].name);
  return s;
}

bool containsKey(const meshcore::ChannelSet& s, const uint8_t key[16]) {
  return meshcore::channelsFindByKey(s, key) >= 0;
}

// Cuts the plan after EVERY step and checks that the remaining table still loads, never counts a slot
// without a key, never invents a key, and that the FINAL state is exactly `after`.
void checkEveryCut(const meshcore::ChannelSet& before, const meshcore::ChannelSet& after,
                   const meshcore::ChannelNvsStep* plan, int steps, const char* what) {
  printf("test  cut after every step: %s\n", what);
  for (int cut = 0; cut <= steps; ++cut) {
    FakeNvs nvs;
    nvs.seed(before);
    for (int i = 0; i < cut; ++i) nvs.apply(plan[i], after);

    // (a) every counted slot has a key: the loader can never read garbage for a slot it counts.
    if (nvs.has_cnt) {
      ++g_checks;
      for (int i = 0; i < nvs.cnt && i < FakeNvs::kSlots; ++i) {
        if (!nvs.has_key[i]) {
          printf("FAIL cut %d/%d (%s): counted slot %d has no key\n", cut, steps, what, i);
          ++g_failures;
          return;
        }
      }
    }

    // (b) it loads, and every loaded key came from before or after.
    meshcore::ChannelSet loaded{};
    const int got = nvs.load(loaded);
    ++g_checks;
    for (int i = 0; i < (int)loaded.count; ++i) {
      if (!containsKey(before, loaded.ch[i].secret) && !containsKey(after, loaded.ch[i].secret)) {
        printf("FAIL cut %d/%d (%s): invented key at %d\n", cut, steps, what, i);
        ++g_failures;
        return;
      }
    }

    // (c) before the count is committed an ADD still loads the old table exactly; after every step the
    //     final table loads exactly `after`.
    if (cut == 0) {
      ++g_checks;
      if (got != (int)before.count) {
        printf("FAIL cut 0 (%s): loaded %d, want old %d\n", what, got, (int)before.count);
        ++g_failures;
        return;
      }
    }
    if (cut == steps) {
      ++g_checks;
      if (got != (int)after.count) {
        printf("FAIL final (%s): loaded %d, want new %d\n", what, got, (int)after.count);
        ++g_failures;
        return;
      }
      ++g_checks;
      for (int i = 0; i < (int)after.count; ++i) {
        if (memcmp(loaded.ch[i].secret, after.ch[i].secret, 16) != 0 ||
            strcmp(loaded.ch[i].name, after.ch[i].name) != 0) {
          printf("FAIL final (%s): slot %d differs\n", what, i);
          ++g_failures;
          return;
        }
      }
    }
  }
}

void test_rewrite_plan() {
  printf("test  safe rewrite order (new entries first, cnt last)\n");
  uint8_t a[16], b[16], c[16], d[16];
  for (int i = 0; i < 16; ++i) {
    a[i] = (uint8_t)(0x10 + i);
    b[i] = (uint8_t)(0x20 + i);
    c[i] = (uint8_t)(0x30 + i);
    d[i] = (uint8_t)(0x40 + i);
  }
  const meshcore::Channel three[3] = {makeChannel("Alpha", a), makeChannel("Bravo", b), makeChannel("Charlie", c)};

  meshcore::ChannelSet before = setOf(three, 3);

  // ---- ADD: the new slot is written, then cnt.
  meshcore::ChannelSet add = before;
  CHECK(meshcore::channelsAdd(add, d, "Delta"));
  meshcore::ChannelNvsStep plan[meshcore::kChannelNvsMaxSteps];
  int n = meshcore::channelRewritePlan(before, add, plan, meshcore::kChannelNvsMaxSteps);
  CHECK_EQ(n, 3);
  CHECK(plan[0].op == meshcore::ChannelNvsOp::SetKey);
  CHECK_EQ(plan[0].index, 3);
  CHECK(plan[1].op == meshcore::ChannelNvsOp::SetName);
  CHECK_EQ(plan[1].index, 3);
  CHECK(plan[2].op == meshcore::ChannelNvsOp::SetCount);
  CHECK_EQ(n, 3);   // SetCount is the LAST table write
  checkEveryCut(before, add, plan, n, "add Delta");

  // ---- REMOVE index 0: compaction from the removed slot upward, cnt, cleanup.
  meshcore::ChannelSet removed0 = before;
  removed0.count = 0;
  CHECK(meshcore::channelsAdd(removed0, b, "Bravo"));
  CHECK(meshcore::channelsAdd(removed0, c, "Charlie"));
  n = meshcore::channelRewritePlan(before, removed0, plan, meshcore::kChannelNvsMaxSteps);
  CHECK(n > 0);
  CHECK(plan[0].op == meshcore::ChannelNvsOp::SetKey);
  CHECK_EQ(plan[0].index, 0);
  int count_at = -1;
  for (int i = 0; i < n; ++i) {
    if (plan[i].op == meshcore::ChannelNvsOp::SetCount) count_at = i;
    if (plan[i].op == meshcore::ChannelNvsOp::EraseScope) CHECK(count_at >= 0);   // cleanup AFTER cnt
  }
  CHECK(count_at >= 0);
  CHECK_EQ(count_at, 4);   // SetKey/SetName for slots 0 and 1, then cnt
  checkEveryCut(before, removed0, plan, n, "remove index 0");

  // ---- REMOVE index 1 (the middle): everything after it compacts.
  meshcore::ChannelSet removed1 = before;
  removed1.count = 0;
  CHECK(meshcore::channelsAdd(removed1, a, "Alpha"));
  CHECK(meshcore::channelsAdd(removed1, c, "Charlie"));
  n = meshcore::channelRewritePlan(before, removed1, plan, meshcore::kChannelNvsMaxSteps);
  checkEveryCut(before, removed1, plan, n, "remove index 1");

  // ---- RENAME: only the name key, no cnt change.
  meshcore::ChannelSet renamed = before;
  snprintf(renamed.ch[1].name, sizeof(renamed.ch[1].name), "Bravo Two");
  n = meshcore::channelRewritePlan(before, renamed, plan, meshcore::kChannelNvsMaxSteps);
  CHECK_EQ(n, 1);
  CHECK(plan[0].op == meshcore::ChannelNvsOp::SetName);
  CHECK_EQ(plan[0].index, 1);
  checkEveryCut(before, renamed, plan, n, "rename slot 1");
}

// ------------------------------------------------------------------- 6. remove + compaction + scope
void test_scope_cleanup() {
  printf("test  remove: compaction and scope cleanup by LIVE hash\n");
  uint8_t a[16], b[16], c[16];
  for (int i = 0; i < 16; ++i) {
    a[i] = (uint8_t)(0x11 + i);
    b[i] = (uint8_t)(0x22 + i);
    c[i] = (uint8_t)(0x33 + i);
  }
  const meshcore::Channel chans[3] = {makeChannel("Alpha", a), makeChannel("Bravo", b), makeChannel("Alpha2", c)};
  meshcore::ChannelSet before = setOf(chans, 3);
  // Force the collision case: two slots share the 1-byte hash of key A (the real table allows a hash
  // collision; the keys themselves differ, so channelsAdd keeps both).
  before.ch[2].hash = before.ch[0].hash;

  meshcore::ChannelNvsStep plan[meshcore::kChannelNvsMaxSteps];
  int n = meshcore::channelRewritePlan(before, before, plan, meshcore::kChannelNvsMaxSteps);
  CHECK_EQ(n, 0);   // nothing changed, nothing written

  // Remove slot 0; slot 2 still carries its hash -> the scope record MUST survive.
  meshcore::ChannelSet after = before;
  after.count = 0;
  CHECK(meshcore::channelsAdd(after, b, "Bravo"));
  CHECK(meshcore::channelsAdd(after, c, "Alpha2"));
  after.ch[1].hash = before.ch[0].hash;   // the surviving collision partner
  n = meshcore::channelRewritePlan(before, after, plan, meshcore::kChannelNvsMaxSteps);
  int erased = 0;
  for (int i = 0; i < n; ++i)
    if (plan[i].op == meshcore::ChannelNvsOp::EraseScope) ++erased;
  CHECK_EQ(erased, 0);

  // Remove both channels that carry key A; NOW its scope record goes, exactly once.
  meshcore::ChannelSet onlyB = before;
  onlyB.count = 0;
  CHECK(meshcore::channelsAdd(onlyB, b, "Bravo"));
  n = meshcore::channelRewritePlan(before, onlyB, plan, meshcore::kChannelNvsMaxSteps);
  erased = 0;
  for (int i = 0; i < n; ++i) {
    if (plan[i].op == meshcore::ChannelNvsOp::EraseScope) {
      ++erased;
      CHECK_EQ(plan[i].hash, before.ch[0].hash);
    }
  }
  CHECK_EQ(erased, 1);

  // The row text the ticket asks for.
  char text[32];
  meshcore::channelScopeRowText("", text, sizeof(text));
  CHECK_STR(text, "default");
  meshcore::channelScopeRowText("!none", text, sizeof(text));
  CHECK_STR(text, "no scope");
  meshcore::channelScopeRowText("si", text, sizeof(text));
  CHECK_STR(text, "scope #si");
  meshcore::channelScopeRowText("#europe", text, sizeof(text));
  CHECK_STR(text, "scope #europe");
  meshcore::channelScopeRowText("bad name!", text, sizeof(text));   // no longer legal -> Default
  CHECK_STR(text, "default");
}

}  // namespace

int main() {
  test_tag_normalisation();
  test_tag_key_against_python();
  test_base64();
  test_duplicate();
  test_rewrite_plan();
  test_scope_cleanup();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
