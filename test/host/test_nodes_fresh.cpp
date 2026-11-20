// SPDX-License-Identifier: MIT
// Host tests for S515/EST-461: the hint bar's dots, the NEW contact (order + ack + the frozen list) and the power
// button's screen state machine. Every rule here is one the device runs: core/nodes_fresh.h, core/screen_power.h
// and core/mc_store.h's counters are the same headers main.cpp includes.
#include <stdio.h>
#include <string.h>

#include "contact_admin.h"   // S516/EST-473: contactRemoveAt(), the shift a delete really makes
#include "mc_store.h"
#include "nodes_fresh.h"
#include "nodes_search.h"
#include "screen_power.h"

namespace {

int g_checks   = 0;
int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_EQ(got, want)                                                                     \
    do {                                                                                        \
        ++g_checks;                                                                             \
        const long g_ = static_cast<long>(got);                                                 \
        const long w_ = static_cast<long>(want);                                                \
        if (g_ != w_) {                                                                         \
            printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__, #got,      \
                   #want, g_, w_);                                                              \
            ++g_failures;                                                                       \
        }                                                                                       \
    } while (0)

void pubOf(uint8_t seed, uint8_t out[32]) {
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(seed + i);
}

// One contact, created the way the device creates one: contactsUpsert's INSERT branch (the first verified advert).
void addNew(om::mc::ContactStore& s, uint8_t seed, uint32_t heard_at) {
    uint8_t pub[32];
    pubOf(seed, pub);
    char name[16];
    snprintf(name, sizeof(name), "N%u", (unsigned)seed);
    om::mc::contactsUpsert(s, pub, name, 1, 1, 100 + seed, heard_at, -70, 52, false, 0, 0);
}

// ---------------------------------------------------------------- item 1: the dots, from the store's own counters
void dotsFromCounts() {
    // No unread, no fresh: no dot at all.
    CHECK_EQ(om::ui::hintDotMask(0, 0, 0), 0);

    // A channel message alone lights the CHAT sigil, and only it.
    CHECK_EQ(om::ui::hintDotMask(1, 0, 0), om::ui::kHintDotBitChat);
    // A DM alone does too (Filip: "any channel or DM has unread").
    CHECK_EQ(om::ui::hintDotMask(0, 3, 0), om::ui::kHintDotBitChat);
    // A new contact alone lights the NODES sigil.
    CHECK_EQ(om::ui::hintDotMask(0, 0, 2), om::ui::kHintDotBitNodes);
    // Both: both dots.
    CHECK_EQ(om::ui::hintDotMask(4, 1, 1), om::ui::kHintDotBitChat | om::ui::kHintDotBitNodes);

    // The bits are the SIGIL indices of chrome.h: Nodes is kind 1, Chat is kind 2.
    CHECK_EQ(om::ui::kHintDotBitNodes, 1u << 1);
    CHECK_EQ(om::ui::kHintDotBitChat, 1u << 2);

    // …and the real counting code agrees with the list's badges: one unread channel message + one unread DM
    // from a contact in the table = the chat dot; a fresh row that is then acked = no nodes dot.
    om::mc::ContactStore s;
    om::mc::MessageRing  msgs;
    om::mc::ReadState    rs;
    om::mc::DmRing       dms;
    addNew(s, 1, 1000);
    CHECK_EQ(om::mc::contactsFreshCount(s), 1);

    om::mc::Message m;
    m.channel = 0;
    snprintf(m.text, sizeof(m.text), "hello");
    om::mc::messagesPush(msgs, m);

    uint8_t peer[32];
    pubOf(1, peer);
    om::mc::DmMessage dm;
    memcpy(dm.peer_pub, peer, 32);
    snprintf(dm.text, sizeof(dm.text), "hi");
    om::mc::dmPush(dms, dm);

    uint32_t ch = 0, dm_unread = 0;
    om::mc::unreadTotals(msgs, rs, 1, dms, s, &ch, &dm_unread);
    CHECK_EQ(ch, 1);
    CHECK_EQ(dm_unread, 1);
    CHECK_EQ(om::ui::hintDotMask(ch, dm_unread, om::mc::contactsFreshCount(s)),
             om::ui::kHintDotBitChat | om::ui::kHintDotBitNodes);

    // Reading both leaves no chat dot, but the new contact still shows.
    om::mc::markChannelRead(msgs, rs, 0);
    om::mc::dmMarkPeerRead(dms, s, peer);
    om::mc::unreadTotals(msgs, rs, 1, dms, s, &ch, &dm_unread);
    CHECK_EQ(ch + dm_unread, 0);
    CHECK_EQ(om::ui::hintDotMask(ch, dm_unread, om::mc::contactsFreshCount(s)), om::ui::kHintDotBitNodes);

    // A DM from a node that is NOT in the table cannot have a thread row, so it adds nothing (the store rule).
    om::mc::ContactStore other;
    om::mc::unreadTotals(msgs, rs, 1, dms, other, &ch, &dm_unread);
    CHECK_EQ(dm_unread, 0);
}

// ------------------------------------------------------- item 2a: favourites, then fresh newest first, then the rest
void orderFavouritesThenFreshThenRest() {
    om::mc::ContactStore s;
    addNew(s, 1, 100);   // row 0, fresh, old
    addNew(s, 2, 300);   // row 1, fresh, newest
    addNew(s, 3, 200);   // row 2, fresh
    addNew(s, 4, 400);   // row 3, but long KNOWN: acked, so not fresh
    addNew(s, 5, 500);   // row 4
    s.c[3].fresh = false;
    s.c[4].fresh = false;
    // A favourite that is also fresh stays in the FAVOURITES group (as today).
    s.c[4].favourite = true;

    int order[om::mc::kMaxContacts];
    const int n = (int)om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
    CHECK_EQ(n, 5);
    CHECK_EQ(order[0], 4);   // the favourite, first
    CHECK_EQ(order[1], 1);   // fresh, newest heard first
    CHECK_EQ(order[2], 2);
    CHECK_EQ(order[3], 0);
    CHECK_EQ(order[4], 3);   // not fresh: by last heard

    // Acking the newest fresh row moves it out of the fresh group ON THE NEXT ORDER — but not while the page is
    // open, which is what the frozen snapshot below is for.
    s.c[1].fresh = false;
    om::mc::contactsOrder(s, order, om::mc::kMaxContacts);
    CHECK_EQ(order[0], 4);
    CHECK_EQ(order[1], 2);
    CHECK_EQ(order[2], 0);
    CHECK_EQ(order[3], 3);
    CHECK_EQ(order[4], 1);   // last heard 300 beats 200/100, so it lands at the top of the "rest"

    // An upsert of an EXISTING key refreshes and never re-freshens it.
    uint8_t pub[32];
    pubOf(2, pub);
    om::mc::contactsUpsert(s, pub, "N2", 1, 1, 101, 900, -70, 52);
    CHECK_EQ(s.c[1].fresh, 0);   // the acked row stays acked through a refresh
    CHECK_EQ(s.count, 5);
    // …while a NEW key is fresh, and a second advert of it is not a second row.
    uint8_t fresh_pub[32];
    pubOf(9, fresh_pub);
    const int idx = om::mc::contactsUpsert(s, fresh_pub, "N9", 1, 1, 102, 950, -70, 52);
    CHECK_EQ(idx, 5);
    CHECK(s.c[5].fresh);
    CHECK_EQ(om::mc::contactsFreshCount(s), 3);   // rows 0 and 2 are still new, plus N9
    om::mc::contactsUpsert(s, fresh_pub, "N9", 1, 1, 103, 960, -70, 52);
    CHECK_EQ(s.count, 6);
    CHECK(s.c[5].fresh);   // a refresh does not clear it either
}

// ------------------------------------------------ item 2c: the ack rules (a key landing, a 1.5 s rest, entry)
void ackRules() {
    om::ui::NodesAck a;

    // OPENING the page does not ack the top row: nothing at t, nothing one tick later, and only the dwell does it.
    om::ui::nodesAckEnter(a, 0, 1000);
    CHECK_EQ(om::ui::nodesAckTick(a, 1000), -1);
    CHECK_EQ(om::ui::nodesAckTick(a, 2499), -1);   // 1499 ms: not yet
    CHECK_EQ(om::ui::nodesAckTick(a, 2500), 0);    // 1500 ms: the rest counts as a look
    CHECK_EQ(om::ui::nodesAckTick(a, 2600), -1);   // one ack per rest

    // A KEY landing on a row acks at once (even 1 ms after entering).
    om::ui::NodesAck b;
    om::ui::nodesAckEnter(b, 0, 1000);
    CHECK_EQ(om::ui::nodesAckKeyMoved(b, 3, 1001), 3);
    CHECK_EQ(om::ui::nodesAckTick(b, 1001), -1);   // the dwell restarted on row 3 and acked nothing yet
    CHECK_EQ(om::ui::nodesAckTick(b, 2501), 3);    // …and the rest tries again, which is harmless: not fresh

    // No row (the search bar has the focus, or the list is empty): nothing is acked.
    om::ui::NodesAck c;
    om::ui::nodesAckEnter(c, -1, 0);
    CHECK_EQ(om::ui::nodesAckKeyMoved(c, -1, 10), -1);
    CHECK_EQ(om::ui::nodesAckTick(c, 100000), -1);

    // The 32-bit clock wraps: a dwell that spans the wrap still counts 1500 ms.
    om::ui::NodesAck d;
    om::ui::nodesAckEnter(d, 0, 0xFFFFFF00u);
    CHECK_EQ(om::ui::nodesAckTick(d, 0xFFFFFF00u + 1499u), -1);
    CHECK_EQ(om::ui::nodesAckTick(d, 0xFFFFFF00u + 1500u), 0);
}

// ------------------------------------- item 2c: the order is frozen while open, re-sorted on entry, appended
void frozenOrder() {
    om::mc::ContactStore s;
    addNew(s, 1, 100);
    addNew(s, 2, 300);
    addNew(s, 3, 200);
    s.c[0].fresh = false;   // row 0 is old news; rows 1 and 2 are the new ones

    int live[om::mc::kMaxContacts];
    int n = (int)om::mc::contactsOrder(s, live, om::mc::kMaxContacts);

    om::ui::NodesFrozen f;
    om::ui::nodesFrozenBegin(f, live, n);   // the page opens: a snapshot, in the fresh-newest-first order
    CHECK_EQ(f.count, 3);
    CHECK_EQ(f.order[0], 1);
    CHECK_EQ(f.order[1], 2);
    CHECK_EQ(f.order[2], 0);

    // An ack does NOT re-sort the open page: the live order moves the acked row, the snapshot keeps it.
    s.c[1].fresh = false;
    CHECK(!om::ui::nodesFrozenSync(f, s));   // the table changed, the SNAPSHOT did not
    CHECK_EQ(f.order[0], 1);
    CHECK_EQ(f.order[1], 2);
    CHECK_EQ(f.order[2], 0);

    // A contact arriving while the page is open is APPENDED, and says so.
    addNew(s, 4, 900);
    CHECK(om::ui::nodesFrozenSync(f, s));
    CHECK_EQ(f.count, 4);
    CHECK_EQ(f.order[3], 3);
    CHECK_EQ(f.order[0], 1);   // still no re-sort

    // A removed row is dropped from the snapshot and nothing else moves (the last row goes, so no index
    // shifts under the others).
    s.count = 3;
    CHECK(om::ui::nodesFrozenSync(f, s));
    CHECK_EQ(f.count, 3);
    CHECK_EQ(f.order[0], 1);
    CHECK_EQ(f.order[1], 2);
    CHECK_EQ(f.order[2], 0);

    // LEAVING and coming back re-sorts: a fresh snapshot of the live order (row 2 is still fresh, row 1 was
    // acked, row 0 is old).
    n = (int)om::mc::contactsOrder(s, live, om::mc::kMaxContacts);
    om::ui::nodesFrozenBegin(f, live, n);
    CHECK_EQ(f.count, 3);
    CHECK_EQ(f.order[0], 2);   // the only still-fresh row
    CHECK_EQ(f.order[1], 1);   // then last heard: 300, 100
    CHECK_EQ(f.order[2], 0);

    // The SEARCH FILTER still runs over the frozen order (EST-449), and the node menu follows its STORE row
    // (EST-455): the snapshot holds store indices, so a filter cannot make the sheet name another node.
    int shown[om::mc::kMaxContacts];
    const int m = om::ui::nodeSearchFilter(&s, f.order, f.count, "N1", shown);
    CHECK_EQ(m, 1);
    CHECK_EQ(shown[0], 0);
    CHECK_EQ(om::ui::nodeSearchFilter(&s, f.order, f.count, "", shown), 3);
}

// ------------------------------------------------------------- item 3: the screen state machine
void screenMachine() {
    om::ui::ScreenPower p;
    CHECK(!p.off);

    // A key while the screen is ON is an ordinary key: no action, no swallow.
    CHECK(om::ui::ScreenAct::None == p.onEvent(om::ui::ScreenEv::Key));

    // PRESS toggles off; the RELEASE never does anything (not even turning it back on).
    CHECK(om::ui::ScreenAct::Off == p.onEvent(om::ui::ScreenEv::PowerPress));
    CHECK(p.off);
    CHECK(om::ui::ScreenAct::None == p.onEvent(om::ui::ScreenEv::PowerRelease));
    CHECK(p.off);

    // A MESSAGE does not wake it (the sound still plays).
    CHECK(om::ui::ScreenAct::None == p.onEvent(om::ui::ScreenEv::Message));
    CHECK(p.off);

    // KEY-LOCK (Filip, S515): a key while off is SWALLOWED and the screen STAYS OFF, however many keys.
    CHECK(om::ui::ScreenAct::Swallow == p.onEvent(om::ui::ScreenEv::Key));
    CHECK(p.off);
    CHECK(om::ui::ScreenAct::Swallow == p.onEvent(om::ui::ScreenEv::Key));
    CHECK(p.off);

    // ONLY the power button wakes it, and says so.
    CHECK(om::ui::ScreenAct::OnByPower == p.onEvent(om::ui::ScreenEv::PowerPress));
    CHECK(!p.off);
    // Once on, a key is an ordinary key again.
    CHECK(om::ui::ScreenAct::None == p.onEvent(om::ui::ScreenEv::Key));
    CHECK(!p.off);
    // …and the release after a press that turned it ON does nothing.
    CHECK(om::ui::ScreenAct::None == p.onEvent(om::ui::ScreenEv::PowerRelease));
    CHECK(!p.off);
}

// ------------------------------------- S516 (EST-473): a node DELETED while the page is open
// `Delete everywhere` removes one row and contactRemoveAt() shifts the rows after it DOWN one, while the frozen
// snapshot holds STORE INDICES. Without the re-map every entry above the deleted row names the NEXT node: the list
// shows one node twice, drops the last one, and the ack/menu lands on a node the user did not pick. The ORDER the
// user is looking at must survive the delete; only the indices move.
void frozenOrderAfterDelete() {
    printf("test  a node deleted while Nodes is open keeps every OTHER row on its node\n");
    om::mc::ContactStore s;
    addNew(s, 1, 100);   // store row 0 -> "N1"
    addNew(s, 2, 300);   // store row 1 -> "N2"  (the one that gets deleted)
    addNew(s, 3, 200);   // store row 2 -> "N3"
    addNew(s, 4, 400);   // store row 3 -> "N4"

    // The page opened on a SCRAMBLED order (freshness/heard order, which is what contactsOrder really returns —
    // an identity order would hide the defect, because a shift then maps to the right successor by luck).
    om::ui::NodesFrozen f;
    const int order[4] = {2, 0, 3, 1};   // N3, N1, N4, N2
    om::ui::nodesFrozenBegin(f, order, 4);
    CHECK_EQ(f.count, 4);
    CHECK_EQ(f.order[3], 1);

    // Delete "N2" (store row 1), exactly as nodeDeleteEverywhere does it: the rows after it move down one.
    const int removed = (int)om::mc::contactsFind(s, s.c[1].pub);
    CHECK_EQ(removed, 1);
    CHECK(om::mc::contactRemoveAt(s, (size_t)removed));
    CHECK_EQ(s.count, 3);

    // The device calls the re-map first (main.cpp, Delete everywhere), then the same sync every draw runs.
    CHECK(om::ui::nodesFrozenAfterRemove(f, removed));
    (void)om::ui::nodesFrozenSync(f, s);

    // Three rows left, in the SAME order, each still naming its own node — N4 last, N2 gone, nothing doubled.
    CHECK_EQ(f.count, 3);
    CHECK_EQ(f.order[0], 1);   // was store row 2, now 1: still "N3"
    CHECK_EQ(f.order[1], 0);   // unchanged: "N1"
    CHECK_EQ(f.order[2], 2);   // was store row 3, now 2: still "N4"
    CHECK(strcmp(s.c[f.order[0]].name, "N3") == 0);
    CHECK(strcmp(s.c[f.order[1]].name, "N1") == 0);
    CHECK(strcmp(s.c[f.order[2]].name, "N4") == 0);
    // The deleted node is nowhere in the snapshot (the defect showed it twice and lost "N4").
    for (int i = 0; i < f.count; ++i) CHECK(strcmp(s.c[f.order[i]].name, "N2") != 0);

    // Deleting the LAST store row shifts nothing; only that row's entry goes (the list was N4, N1, N3).
    om::ui::NodesFrozen g;
    const int order2[3] = {2, 0, 1};
    om::ui::nodesFrozenBegin(g, order2, 3);
    CHECK(om::mc::contactRemoveAt(s, s.count - 1));   // store row 2 = "N4"
    CHECK(om::ui::nodesFrozenAfterRemove(g, 2));
    CHECK_EQ(g.count, 2);
    CHECK_EQ(g.order[0], 0);   // "N1" stays where it was
    CHECK_EQ(g.order[1], 1);   // "N3" stays where it was
    CHECK(strcmp(s.c[g.order[0]].name, "N1") == 0);
    CHECK(strcmp(s.c[g.order[1]].name, "N3") == 0);

    // A stale row id (-1: the node left the table before the delete) changes nothing and does not renumber.
    CHECK(!om::ui::nodesFrozenAfterRemove(g, -1));
    CHECK_EQ(g.count, 2);
    CHECK_EQ(g.order[0], 0);
}

}  // namespace

int main() {
    dotsFromCounts();
    orderFavouritesThenFreshThenRest();
    ackRules();
    frozenOrder();
    frozenOrderAfterDelete();
    screenMachine();
    printf("test_nodes_fresh: %d checks, %d failure%s\n", g_checks, g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
