// SPDX-License-Identifier: MIT
// Host tests for S514/EST-456: Ping and Trace, the L2's two node actions.
//
// The TRACE frame's expectation below is built from upstream's createTrace()/sendDirect() (Mesh.cpp:605-620,
// 692-714), NOT from this code: tag 0x11223344, auth 0, 2-byte path hashes, path ab cd ef 01 23 45 ->
//   26 00 | 44 33 22 11 | 00 00 00 00 | 01 | ab cd ef 01 23 45
// (header (0x09<<2)|DIRECT, path_len 0, then the payload with the path appended).
// The PING frame reuses the EST-453 status request, whose oracle is quoted in test_rptr.cpp; here it only has to
// BE that frame (same bytes as buildStatusRequest), so a second format cannot creep in.
#include <stdio.h>
#include <string.h>

#include "mc_identity.h"
#include "mc_wire.h"
#include "node_menu.h"
#include "node_ops.h"
#include "rptr_wire.h"

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

#define CHECK_STR(got, want)                                                        \
  do {                                                                              \
    ++g_checks;                                                                     \
    if (strcmp((got), (want)) != 0) {                                               \
      printf("FAIL %s:%d  \"%s\" == \"%s\"\n", __FILE__, __LINE__, (got), (want));  \
      ++g_failures;                                                                 \
      return;                                                                       \
    }                                                                               \
  } while (0)

using om::ops::HopNamer;

// A tiny table the namer searches: the parser stays pure, this is the device's contact lookup in miniature.
struct Table {
  struct Row {
    uint8_t hash[2];
    const char* name;
  } rows[3];
  int count = 0;
};

bool nameHop(const uint8_t* hash, uint8_t len, void* ctx, char* out, size_t cap) {
  const Table* t = static_cast<const Table*>(ctx);
  if (t == nullptr || len < 2) return false;
  for (int i = 0; i < t->count; ++i) {
    if (memcmp(t->rows[i].hash, hash, 2) == 0) {
      snprintf(out, cap, "%s", t->rows[i].name);
      return true;
    }
  }
  return false;
}

void pingFrameIsTheStatusRequest() {
  meshcore::Identity self;
  memset(&self, 0, sizeof(self));
  for (int i = 0; i < 32; ++i) self.pub[i] = (uint8_t)i;          // 00..1f
  uint8_t dest[32];
  for (int i = 0; i < 32; ++i) dest[i] = (uint8_t)(0x20 + i);      // 20..3f
  uint8_t secret[meshcore::kSecretBytes];
  memset(secret, 0x5a, sizeof(secret));

  uint8_t a[256], b[256];
  const size_t na = om::ops::buildPingFrame(self, dest, secret, 0x55667788u, 0xaabbccddu, nullptr, 0xFF, a,
                                            sizeof(a));
  const size_t nb = om::rptr::buildStatusRequest(self, dest, secret, 0x55667788u, 0xaabbccddu, nullptr, 0xFF, b,
                                                 sizeof(b));
  CHECK(na > 0);
  CHECK_EQ(na, nb);
  CHECK_EQ(memcmp(a, b, na), 0);
  CHECK_EQ(a[0], (uint8_t)((om::rptr::kTypeReq << 2) | om::rptr::kRouteFlood));   // no route = a flood

  // A stored route makes it DIRECT, and the path bytes are the contact's out_path.
  const uint8_t path[2] = {0xab, 0xcd};
  const size_t  nd = om::ops::buildPingFrame(self, dest, secret, 1, 2, path, 0x41, a, sizeof(a));
  CHECK(nd > 0);
  CHECK_EQ(a[0], (uint8_t)((om::rptr::kTypeReq << 2) | om::rptr::kRouteDirect));
  CHECK_EQ(a[1], 0x41);
  CHECK_EQ(a[2], 0xab);
  CHECK_EQ(a[3], 0xcd);
}

void pingStatesAndWords() {
  om::ops::PingJob j;
  CHECK(j.state == om::ops::PingState::Idle);
  uint8_t key[32];
  memset(key, 0x42, sizeof(key));
  om::ops::pingStarted(j, key, "RPT-Kamera", 7, 1000);
  CHECK(j.state == om::ops::PingState::Sent);
  CHECK_EQ(j.deadline_ms, 1000 + om::ops::kPingTimeoutMs);
  char text[64];
  om::ops::pingText(text, sizeof(text), j);
  CHECK_STR(text, "Ping RPT-Kamera: waiting...");

  // Just before the L2's deadline it is still waiting; at the deadline it times out.
  om::ops::pingTick(j, 1000 + om::ops::kPingTimeoutMs - 1);
  CHECK(j.state == om::ops::PingState::Sent);
  om::ops::pingTick(j, 1000 + om::ops::kPingTimeoutMs);
  CHECK(j.state == om::ops::PingState::Timeout);
  om::ops::pingText(text, sizeof(text), j);
  CHECK_STR(text, "Ping RPT-Kamera: no reply");

  // The round trip is measured from the send, and printed in the ticket's words.
  om::ops::pingStarted(j, key, "RPT-Kamera", 8, 500);
  om::ops::pingReplied(j, 2300);
  CHECK(j.state == om::ops::PingState::Replied);
  CHECK_EQ(j.rtt_ms, 1800);
  om::ops::pingText(text, sizeof(text), j);
  CHECK_STR(text, "Ping RPT-Kamera: 1.8 s");

  // An answer after the timeout (or to nothing) changes nothing.
  om::ops::pingReplied(j, 9000);
  CHECK_EQ(j.rtt_ms, 1800);
  CHECK(j.state == om::ops::PingState::Replied);

  // Esc stops the wait; the packet is not recalled. A send that never queued reports Failed.
  om::ops::pingStarted(j, key, "RPT-Kamera", 9, 500);
  om::ops::pingCancel(j);
  CHECK(j.state == om::ops::PingState::Idle);
  om::ops::pingText(text, sizeof(text), j);
  CHECK_STR(text, "");
  om::ops::pingFailed(j);
  om::ops::pingText(text, sizeof(text), j);
  CHECK_STR(text, "Ping: send failed");
}

void traceFrameIsUpstreams() {
  const uint8_t path[6] = {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45};
  uint8_t       out[64];
  const size_t  n = om::ops::buildTraceFrame(0x11223344u, 0, 2, path, sizeof(path), out, sizeof(out));
  const uint8_t want[] = {0x26, 0x00, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01,
                          0xab, 0xcd, 0xef, 0x01, 0x23, 0x45};
  CHECK_EQ(n, sizeof(want));
  CHECK_EQ(memcmp(out, want, sizeof(want)), 0);
  // The flags byte carries the path hash size (2 bytes -> code 1), the same rule as mesh_link.cpp:2304.
  CHECK_EQ(out[10], om::ops::traceFlagsFor(2));
  CHECK_EQ(out[1], 0);   // sendDirect() appends the path to the payload, header path_len stays 0

  // A bad hash size or a too-small buffer is refused, not truncated.
  CHECK_EQ(om::ops::buildTraceFrame(1, 0, 0, path, sizeof(path), out, sizeof(out)), 0);
  CHECK_EQ(om::ops::buildTraceFrame(1, 0, 4, path, sizeof(path), out, sizeof(out)), 0);
  CHECK_EQ(om::ops::buildTraceFrame(1, 0, 2, path, sizeof(path), out, 16), 0);
}

void traceAnswerParses() {
  // A three-hop answer, 2-byte hashes: SNRs 7.0 / 11.5 / 2.0 dB (x4: 28, 46, 8), hashes 4b91 / 2e33 / c7d0.
  const uint8_t frame[] = {0x26, 0x03, 0x1c, 0x2e, 0x08,   // header, path_len 3 (the SNR bytes), path
                           0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01,   // tag, auth, flags
                           0x4b, 0x91, 0x2e, 0x33, 0xc7, 0xd0};                    // the hop hashes
  meshcore::View v;
  CHECK(meshcore::decode(frame, sizeof(frame), &v));
  CHECK_EQ(v.type(), 0x09);
  CHECK_EQ(v.route(), om::rptr::kRouteDirect);

  Table t;
  t.rows[0] = {{0x4b, 0x91}, "RPT-Kamera"};
  t.rows[1] = {{0xc7, 0xd0}, "RPT-Vzhod"};
  t.count   = 2;

  om::ops::TraceView tv;
  CHECK(om::ops::parseTraceResponse(v, (int8_t)((-12.25f) * 4.0f), nameHop, &t, &tv));
  CHECK_EQ(tv.count, 3);
  CHECK(tv.haveRtt);
  CHECK_EQ(tv.hops[0].hash_len, 2);
  CHECK(tv.hops[0].known);
  CHECK_STR(tv.hops[0].name, "RPT-Kamera");
  CHECK_EQ(tv.hops[0].snr4, 28);
  // The middle hop is not in the table, so its 2-byte hash is shown as hex, exactly 4 digits.
  CHECK(!tv.hops[1].known);
  CHECK_STR(tv.hops[1].name, "2e33");
  CHECK_EQ(tv.hops[1].snr4, 46);
  CHECK_STR(tv.hops[2].name, "RPT-Vzhod");
  CHECK_EQ(tv.hops[2].snr4, 8);

  char label[32];
  om::ops::traceHopLabel(label, sizeof(label), tv.hops[1]);
  CHECK_STR(label, "2e33");
  CHECK_EQ(memcmp(tv.hops[1].hash, (const uint8_t[]){0x2e, 0x33}, 2), 0);

  // The total line: hop count and the round trip.
  tv.rttMs = 2400;
  char total[48];
  om::ops::traceTotalText(total, sizeof(total), tv);
  CHECK_STR(total, "3 hops \xc2\xb7 RTT 2.4 s");
}

void traceBoundsAreChecked() {
  om::ops::TraceView tv;
  Table             t;
  t.count = 0;

  // Every length that came off the air is checked: a wrong type/route, a short payload, a 0-size hash mode,
  // a path with more SNR bytes than hashes, and more hashes than the 8-hop list. (Also under ASan.)
  uint8_t buf[128];
  meshcore::View v;

  const uint8_t wrongType[] = {0x25, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(meshcore::decode(wrongType, sizeof(wrongType), &v));
  CHECK(!om::ops::parseTraceResponse(v, 0, nameHop, &t, &tv));

  const uint8_t shortPayload[] = {0x26, 0x00, 1, 2, 3, 4, 5, 6, 7};
  CHECK(meshcore::decode(shortPayload, sizeof(shortPayload), &v));
  CHECK(!om::ops::parseTraceResponse(v, 0, nameHop, &t, &tv));

  // 1-byte hashes (flags 0) with 4 SNR bytes in the path but only 2 hashes: two hops, never a read past the end.
  const uint8_t fewerHashes[] = {0x26, 0x04, 0x1c, 0x2e, 0x08, 0x14, 1, 0, 0, 0, 1, 0, 0, 0, 0,
                                 0xaa, 0xbb};
  CHECK(meshcore::decode(fewerHashes, sizeof(fewerHashes), &v));
  CHECK(om::ops::parseTraceResponse(v, 0, nullptr, &t, &tv));
  CHECK_EQ(tv.count, 2);
  CHECK_STR(tv.hops[0].name, "aa");

  // Junk straight into the parser: never a crash, and never a claim it parsed a trace that is not one.
  for (size_t n = 0; n < sizeof(buf); ++n) buf[n] = (uint8_t)(n * 37u);
  for (size_t len = 0; len <= 40; ++len) {
    if (!meshcore::decode(buf, len, &v)) continue;
    om::ops::parseTraceResponse(v, 0, nameHop, &t, &tv);   // the answer is not checked on purpose: no crash is
  }                                                        // what this pass proves

  // A zero-hop answer is a real result: the node itself answered, count 0, RTT present.
  const uint8_t zeroHop[] = {0x26, 0x00, 0x44, 0x33, 0x22, 0x11, 0, 0, 0, 0, 0};
  CHECK(meshcore::decode(zeroHop, sizeof(zeroHop), &v));
  CHECK(om::ops::parseTraceResponse(v, -44, nullptr, nullptr, &tv));
  CHECK_EQ(tv.count, 0);
  CHECK(tv.haveRtt);
  CHECK_EQ(tv.finalSnr4, -44);
}

void traceDeadlineAndCancel() {
  om::ops::TraceJob j;
  CHECK(!j.pending);
  om::ops::traceStarted(j, "RPT-Kamera", 100);
  CHECK(j.pending);
  CHECK_EQ(j.deadline_ms, 100 + om::ops::kTraceTimeoutMs);
  om::ops::traceTick(j, 100 + om::ops::kTraceTimeoutMs - 1);
  CHECK(j.pending);
  om::ops::traceTick(j, 100 + om::ops::kTraceTimeoutMs);
  CHECK(!j.pending);   // the wait is over; the screen says no data came back

  om::ops::traceStarted(j, "RPT-Kamera", 0);
  om::ops::traceCancel(j);
  CHECK(!j.pending);
}

void menuRowsPerType() {
  om::ui::NodeMenuRow rows[om::ui::kMaxNodeMenuRows];
  om::ui::NodeMenuContext c;

  // A client with a route: DM, Info, Ping, Trace, Map, Add to radio, Favourite, Delete everywhere
  // (S515/EST-465 and EST-466's Map row, which sits after Trace).
  c.advType = om::ui::kNodeMenuAdvChat;
  c.hasKey = true;
  c.pathKnown = true;
  c.favourite = true;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 8);
  CHECK(rows[0].action == om::ui::NodeAction::DirectMessage);
  CHECK(rows[1].action == om::ui::NodeAction::Info);
  CHECK(rows[2].action == om::ui::NodeAction::Ping);
  CHECK(rows[3].action == om::ui::NodeAction::Trace);
  CHECK(rows[4].action == om::ui::NodeAction::Map);
  CHECK(rows[5].action == om::ui::NodeAction::AddToRadio);
  CHECK(rows[6].action == om::ui::NodeAction::Favourite);
  CHECK_STR(rows[6].value, "on");
  CHECK(rows[6].valueAccent);
  CHECK(rows[7].action == om::ui::NodeAction::DeleteEverywhere);

  // NO stored route: the Trace row is ABSENT (not dimmed), and the rows after it move up.
  c.pathKnown = false;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 7);
  CHECK(rows[2].action == om::ui::NodeAction::Ping);
  CHECK(rows[3].action == om::ui::NodeAction::Map);   // S515/EST-466: Map is not behind a route
  CHECK(rows[4].action == om::ui::NodeAction::AddToRadio);
  CHECK(rows[5].action == om::ui::NodeAction::Favourite);
  for (int i = 0; i < 7; ++i) CHECK(rows[i].action != om::ui::NodeAction::Trace);

  // A repeater with a route: no DM, but Ping and Trace are offered (L2: Ping on hasKey, Trace on hasKey+route).
  c.advType = om::ui::kNodeMenuAdvRepeater;
  c.pathKnown = true;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 7);
  CHECK(rows[0].action == om::ui::NodeAction::Info);
  CHECK(rows[1].action == om::ui::NodeAction::Ping);
  CHECK(rows[2].action == om::ui::NodeAction::Trace);
  CHECK(rows[3].action == om::ui::NodeAction::Map);

  // No full key: Info only (plus EST-466's Map, which needs no key). Neither action can work without the key.
  c.hasKey = false;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 2);
  CHECK(rows[0].action == om::ui::NodeAction::Info);
  CHECK(rows[1].action == om::ui::NodeAction::Map);

  // A job in flight puts "waiting..." in that row's value; Esc (the caller) stops the wait.
  c.hasKey = true;
  c.pathKnown = true;
  c.pingPending = true;
  c.tracePending = true;
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, om::ui::kMaxNodeMenuRows), 7);   // still the repeater above
  CHECK(rows[1].action == om::ui::NodeAction::Ping);
  CHECK_STR(rows[1].value, "waiting...");
  CHECK(rows[2].action == om::ui::NodeAction::Trace);
  CHECK_STR(rows[2].value, "waiting...");

  // The bound is real: a caller with one slot gets one row, never a write past it.
  CHECK_EQ(om::ui::nodeMenuRows(c, rows, 1), 1);
  CHECK_EQ(om::ui::nodeMenuRows(c, nullptr, 6), 0);
}

}  // namespace

void test_trace_hash_sizes() {
    // S514 (Claude): only 1- and 2-byte path hashes can be traced (upstream entry size = 1 << (flags & 3)).
    uint8_t frame[64];
    const uint8_t path[4] = {0xAB, 0xCD, 0x12, 0x34};
    CHECK(om::ops::buildTraceFrame(1, 2, 2, path, 4, frame, sizeof(frame)) > 0);
    CHECK(om::ops::buildTraceFrame(1, 2, 1, path, 4, frame, sizeof(frame)) > 0);
    CHECK(om::ops::buildTraceFrame(1, 2, 3, path, 3, frame, sizeof(frame)) == 0);
    CHECK(om::ops::buildTraceFrame(1, 2, 4, path, 4, frame, sizeof(frame)) == 0);
}

int main() {
    test_trace_hash_sizes();
  pingFrameIsTheStatusRequest();
  pingStatesAndWords();
  traceFrameIsUpstreams();
  traceAnswerParses();
  traceBoundsAreChecked();
  traceDeadlineAndCancel();
  menuRowsPerType();
  printf("%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
