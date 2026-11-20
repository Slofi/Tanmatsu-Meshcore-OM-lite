// SPDX-License-Identifier: MIT
// Host tests for the Tanmatsu app core (EST-426). Real exit code: 0 = every check passed.
//
// The four rules the ticket names are four tests, each with its own fixtures, so a broken rule has one
// obvious failing line:
//   1. start -> stop -> start leaves the same counted memory (and a second start is refused);
//   2. a module that leaks on stop is reported BY NAME (and the report is one line);
//   3. a second owner of the radio is refused, and the refusal names the resource;
//   4. each symbol key reaches its page, a module never sees a symbol key, a stopped target gets
//      OfferStart (not an auto-start), and Esc goes back;
//   4b. the symbol key of the page already on screen goes to that page's top (S514/EST-444): the
//      owner closes its inner level, at the top it is a no-op, and another page's key routes as before.
//   4c. at that top the own key now selects the FIRST row (S514/EST-454): Chat column 0 row 0 + both
//      column scrolls, the Nodes/RPTR list row 0; inside an inner level it only closes one step.
// The fifth test covers the Launcher-never-beside-a-module rule and the three registry states.
//
// Build/run: sh test/host/run.sh (g++ -std=c++17 -Wall -Wextra -Werror).
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "own_top.h"
#include "radio_plan.h"
#include "keys.h"
#include "module.h"
#include "registry.h"
#include "router.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,  \
                  #got, #want, g_, w_);                                              \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

struct DummyCanvas : tm::Canvas {};

constexpr uint16_t kAllOmPages = tm::page_bit(tm::Page::Nodes) | tm::page_bit(tm::Page::Chat) |
                                 tm::page_bit(tm::Page::Map) | tm::page_bit(tm::Page::Rptr) |
                                 tm::page_bit(tm::Page::Settings);

// --------------------------------------------------------------------------------------- fixtures
// "alpha": well behaved. 1024 + 512 + 64 = 1600 bytes in 3 blocks, all returned on stop.
void* g_alpha[3] = {};
int g_alpha_starts = 0, g_alpha_stops = 0, g_alpha_ticks = 0, g_alpha_draws = 0, g_alpha_keys = 0;

bool alpha_start(tm::Core& c) {
  ++g_alpha_starts;
  g_alpha[0] = c.alloc().alloc("alpha", 1024);
  g_alpha[1] = c.alloc().alloc("alpha", 512);
  g_alpha[2] = c.alloc().alloc("alpha", 64);
  return g_alpha[0] && g_alpha[1] && g_alpha[2];
}
void alpha_stop(tm::Core& c) {
  ++g_alpha_stops;
  for (void*& p : g_alpha) {
    c.alloc().release("alpha", p);
    p = nullptr;
  }
}
void alpha_tick(tm::Core&) { ++g_alpha_ticks; }
void alpha_draw(tm::Canvas&) { ++g_alpha_draws; }
bool alpha_key(tm::Core&, tm::Key) {
  ++g_alpha_keys;
  return true;   // claims every key it is given, on purpose: the core must not give it a symbol key
}

// "leaky": 2000 + 1000 + 24 = 3024 bytes in 3 blocks; stop() forgets the first two.
void* g_leaky[3] = {};
int g_leaky_starts = 0, g_leaky_stops = 0;

bool leaky_start(tm::Core& c) {
  ++g_leaky_starts;
  g_leaky[0] = c.alloc().alloc("leaky", 2000);
  g_leaky[1] = c.alloc().alloc("leaky", 1000);
  g_leaky[2] = c.alloc().alloc("leaky", 24);
  return g_leaky[0] && g_leaky[1] && g_leaky[2];
}
void leaky_stop(tm::Core& c) {
  ++g_leaky_stops;
  c.alloc().release("leaky", g_leaky[2]);   // the bug the test must catch
  g_leaky[2] = nullptr;
}

// "om-lite" and "probe": both ask for the radio; only the first may get it.
int g_om_starts = 0, g_om_draws = 0, g_om_keys = 0;
int g_probe_starts = 0;
void* g_om_block = nullptr;

bool om_start(tm::Core& c) {
  ++g_om_starts;
  g_om_block = c.alloc().alloc("om-lite", 256);
  return g_om_block != nullptr;
}

void om_stop_clean(tm::Core& c) {
  c.alloc().release("om-lite", g_om_block);
  g_om_block = nullptr;
}
void om_draw(tm::Canvas&) { ++g_om_draws; }
bool om_key(tm::Core&, tm::Key) {
  ++g_om_keys;
  return true;
}

// S514/EST-444 + EST-454: the module's own-top hook, modelling the three pages that have a list, and
// driving the SAME core/own_top.h arithmetic main.cpp uses. Only the page the hook is told about may
// close its own level: OM Lite owns five pages.
int      g_om_tops      = 0;
bool     g_om_inner     = false;   // Chat: an open channel/DM
int      g_om_col       = om::mc::kColChannels;
int      g_om_row       = 0;
int      g_om_scroll[om::mc::kCardCols] = {0, 0};
bool     g_om_nodes_bar = false;   // Nodes: the search bar has the focus
int      g_om_nodes_row = 0;
int      g_om_rptr_mode = 0;       // RPTR: 0 list, 1 login, 2 status
int      g_om_rptr_row  = 0;
bool     g_om_settings_sub = false;   // Settings: a sub-page is open (S516/EST-478)
tm::Page g_om_top_page  = tm::Page::Count;

void om_top_reset_focus() {
  g_om_tops      = 0;
  g_om_inner     = false;
  g_om_col       = om::mc::kColChannels;
  g_om_row       = 0;
  g_om_scroll[0] = g_om_scroll[1] = 0;
  g_om_nodes_bar = false;
  g_om_nodes_row = 0;
  g_om_rptr_mode = 0;
  g_om_rptr_row  = 0;
  g_om_settings_sub = false;
  g_om_top_page  = tm::Page::Count;
}

bool om_top(tm::Core&, tm::Page page) {
  ++g_om_tops;
  g_om_top_page = page;
  if (page == tm::Page::Chat) {
    tm::CardListTop top;
    top.inner = g_om_inner;
    top.col   = g_om_col;
    top.row   = g_om_row;
    for (int i = 0; i < om::mc::kCardCols; ++i) top.scroll[i] = g_om_scroll[i];
    const bool closed = tm::ownTopCardList(top);
    g_om_inner = top.inner;
    g_om_col   = top.col;
    g_om_row   = top.row;
    for (int i = 0; i < om::mc::kCardCols; ++i) g_om_scroll[i] = top.scroll[i];
    return closed;
  }
  if (page == tm::Page::Nodes) {
    tm::ListTop top;
    top.inner = g_om_nodes_bar;
    top.row   = g_om_nodes_row;
    const bool closed = tm::ownTopList(top);
    g_om_nodes_bar = top.inner;
    g_om_nodes_row = top.row;
    return closed;
  }
  if (page == tm::Page::Rptr) {
    tm::ListTop top;
    top.inner = g_om_rptr_mode != 0;
    top.row   = g_om_rptr_row;
    const bool closed = tm::ownTopList(top);
    g_om_rptr_row = top.row;
    if (closed) g_om_rptr_mode = 0;
    return closed;
  }
  if (page == tm::Page::Settings) {
    // S516/EST-478: the diamond closes a Settings sub-page; at the root there is nothing to close.
    if (!g_om_settings_sub) return false;
    g_om_settings_sub = false;
    return true;
  }
  return false;   // Map has no list; the Launcher has no module
}

bool probe_start(tm::Core&) {
  ++g_probe_starts;
  return true;
}

// --------------------------------------------------------------------------------------- 1. memory
void test_start_stop_start_keeps_memory() {
  printf("test  start-stop-start keeps the same counted memory\n");
  g_alpha_starts = g_alpha_stops = 0;

  static const tm::Module mod = {"alpha", "Alpha", "test module", tm::page_bit(tm::Page::Nodes), true,
                                 tm::Resource::Settings | tm::Resource::Clock,
                                 alpha_start, alpha_stop, alpha_tick, alpha_draw, alpha_key, nullptr, nullptr};
  tm::Core core;
  CHECK(core.registry().add(mod));

  // one start: 3 blocks, 1600 bytes, and nothing else
  CHECK(core.start("alpha"));
  CHECK_EQ(core.alloc().stats("alpha").blocks, 3);
  CHECK_EQ(core.alloc().stats("alpha").bytes, 1600);
  CHECK_EQ(core.alloc().total().blocks, 3);
  CHECK_EQ(core.alloc().total().bytes, 1600);
  CHECK_EQ(core.registry().state("alpha") == tm::ModuleState::Running, true);

  // a second start while running changes nothing (and calls no start hook)
  const tm::StartResult again = core.start("alpha");
  CHECK(again.code == tm::StartResult::AlreadyRunning);
  CHECK_EQ(g_alpha_starts, 1);
  CHECK_EQ(core.alloc().stats("alpha").bytes, 1600);

  // stop: the module returns every block, the core can see zero, no leak is reported
  const tm::StopResult off = core.stop("alpha");
  CHECK(off.code == tm::StopResult::Ok);
  CHECK(off.leaked == nullptr);
  CHECK_EQ(g_alpha_stops, 1);
  CHECK_EQ(core.alloc().stats("alpha").blocks, 0);
  CHECK_EQ(core.alloc().stats("alpha").bytes, 0);
  CHECK_EQ(core.alloc().total().blocks, 0);
  CHECK_EQ(core.registry().state("alpha") == tm::ModuleState::Stopped, true);

  // stop again: refused, and it does not free somebody else's memory
  CHECK(core.stop("alpha").code == tm::StopResult::NotRunning);

  // start AGAIN: byte for byte and block for block what the first start took
  CHECK(core.start("alpha"));
  CHECK_EQ(g_alpha_starts, 2);
  CHECK_EQ(core.alloc().stats("alpha").blocks, 3);
  CHECK_EQ(core.alloc().stats("alpha").bytes, 1600);
  CHECK_EQ(core.alloc().total().bytes, 1600);

  CHECK(core.stop("alpha"));
  CHECK_EQ(core.alloc().total().blocks, 0);
  char line[128];
  CHECK_EQ(strlen(core.leakLine(line, sizeof line)), 0u);
}

// --------------------------------------------------------------------------------------- 2. leak
void test_leak_is_reported_by_name() {
  printf("test  a module that leaks on stop is reported by name\n");
  g_leaky_starts = g_leaky_stops = 0;

  static const tm::Module mod = {"leaky", "Leaky", "leaks on stop", tm::page_bit(tm::Page::Chat), true,
                                 tm::Resource::None, leaky_start, leaky_stop, nullptr, nullptr,
                                 nullptr, nullptr, nullptr};
  tm::Core core;
  CHECK(core.registry().add(mod));

  CHECK(core.start("leaky"));
  CHECK_EQ(core.alloc().stats("leaky").blocks, 3);
  CHECK_EQ(core.alloc().stats("leaky").bytes, 3024);

  const tm::StopResult off = core.stop("leaky");
  CHECK(off.code == tm::StopResult::Ok);
  CHECK_EQ(g_leaky_stops, 1);

  // the stop result names the module and its numbers
  CHECK(off.leaked != nullptr);
  CHECK(strcmp(off.leaked, "leaky") == 0);
  CHECK_EQ(off.leak_stats.blocks, 2);
  CHECK_EQ(off.leak_stats.bytes, 3000);

  // the allocator itself agrees, and the core's one-line report is the Launcher's string
  CHECK_EQ(core.alloc().stats("leaky").blocks, 2);
  CHECK_EQ(core.alloc().leakCount(), 1u);
  CHECK(core.firstLeak() != nullptr);
  CHECK(strcmp(core.firstLeak(), "leaky") == 0);
  char line[128];
  core.leakLine(line, sizeof line);
  printf("      %s\n", line);
  CHECK(strncmp(line, "leak: leaky", 11) == 0);
  CHECK(strstr(line, "2 blocks") != nullptr);
  CHECK(strstr(line, "3000 bytes") != nullptr);

  // a leak is per module: the clean module "alpha" is not blamed, even while leaky still holds memory
  CHECK_EQ(core.alloc().stats("alpha").blocks, 0);
}

// --------------------------------------------------------------------------------------- 3. radio
void test_second_radio_owner_is_refused() {
  printf("test  a second owner of the radio is refused\n");
  g_om_starts = g_probe_starts = 0;

  static const tm::Module om = {"om-lite", "OM Lite", "mesh client", kAllOmPages, true,
                                tm::Resource::Radio | tm::Resource::SdCard, om_start, om_stop_clean,
                                nullptr, om_draw, om_key, nullptr, nullptr};
  static const tm::Module probe = {"probe", "Probe", "also wants the radio", 0, true, tm::Resource::Radio,
                                   probe_start, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  tm::Core core;
  CHECK(core.registry().add(om));
  CHECK(core.registry().add(probe));

  // the core owns the DRIVER for the whole app, whoever holds the lease (PLAN §2 rule 1)
  CHECK(core.ownsRadioDriver());

  CHECK(core.start("om-lite"));
  CHECK_EQ(g_om_starts, 1);
  CHECK_EQ(static_cast<unsigned>(core.registry().granted() & tm::Resource::Radio),
           static_cast<unsigned>(tm::Resource::Radio));

  // the second asker is refused, the refusal names the radio, and its start hook never runs
  const tm::StartResult second = core.start("probe");
  CHECK(second.code == tm::StartResult::ResourceBusy);
  CHECK(second.busy == tm::Resource::Radio);
  CHECK_EQ(g_probe_starts, 0);
  CHECK_EQ(core.registry().state("probe") == tm::ModuleState::Stopped, true);

  // the radio lease follows the running module: stop om-lite, the probe may start
  CHECK(core.stop("om-lite"));
  CHECK_EQ(g_om_starts, 1);
  CHECK((core.registry().granted() & tm::Resource::Radio) == tm::Resource::None);
  CHECK(core.start("probe"));
  CHECK_EQ(g_probe_starts, 1);
  CHECK_EQ(core.registry().runningCount(), 1u);

  CHECK(core.stop("probe"));
  CHECK_EQ(static_cast<unsigned>(core.registry().granted()), 0u);
}

// --------------------------------------------------------------------------------------- 4. router
void test_symbol_keys_route_and_are_not_swallowed() {
  printf("test  symbol keys route to their page and no module swallows them\n");
  g_om_starts = g_om_draws = g_om_keys = 0;
  g_om_block = nullptr;

  static const tm::Module om = {"om-lite", "OM Lite", "mesh client", kAllOmPages, true,
                                tm::Resource::Radio, om_start, om_stop_clean, nullptr, om_draw,
                                om_key, nullptr, nullptr};
  tm::Core core;
  CHECK(core.registry().add(om));

  // the key table: NAMES + codes. S516/EST-478 moved the function keys one up: F1/X = 20, then the five
  // symbol keys 21..25, and the volume keys 37/38 (the BSP enum, counted in order).
  struct Want { tm::Key key; const char* name; tm::Page page; uint16_t code; };
  static const Want wants[] = {
      {tm::Key::SymX, "x", tm::Page::Launcher, 20},
      {tm::Key::SymTriangle, "triangle", tm::Page::Nodes, 21},
      {tm::Key::SymSquare, "square", tm::Page::Chat, 22},
      {tm::Key::SymCircle, "circle", tm::Page::Map, 23},
      {tm::Key::SymCloud, "cloud", tm::Page::Rptr, 24},
      {tm::Key::SymDiamond, "diamond", tm::Page::Settings, 25},
  };
  for (const Want& w : wants) {
    CHECK(strcmp(tm::key_name(w.key), w.name) == 0);
    CHECK_EQ(tm::key_code(w.key), w.code);              // from the BSP enum / the glass log
    CHECK(tm::key_from_code(w.code) == w.key);          // and the reverse mapping agrees
    CHECK_EQ(tm::page_for_symbol(w.key) == w.page, true);
    CHECK_EQ(tm::is_symbol(w.key), true);
  }
  // the volume keys: table codes 37/38, NOT page keys, and never routed to a page.
  CHECK(strcmp(tm::key_name(tm::Key::VolUp), "volumeup") == 0);
  CHECK(strcmp(tm::key_name(tm::Key::VolDown), "volumedown") == 0);
  CHECK_EQ(tm::key_code(tm::Key::VolUp), 37);
  CHECK_EQ(tm::key_code(tm::Key::VolDown), 38);
  CHECK(tm::key_from_code(37) == tm::Key::VolUp);
  CHECK(tm::key_from_code(38) == tm::Key::VolDown);
  CHECK_EQ(tm::is_volume_key(tm::Key::VolUp), true);
  CHECK_EQ(tm::is_volume_key(tm::Key::VolDown), true);
  CHECK_EQ(tm::is_symbol(tm::Key::VolUp), false);
  CHECK_EQ(tm::page_for_symbol(tm::Key::VolUp) == tm::Page::Count, true);
  // the arrows/enter the same logger saw
  CHECK_EQ(tm::key_code(tm::Key::Up), 4);
  CHECK_EQ(tm::key_code(tm::Key::Down), 5);
  CHECK_EQ(tm::key_code(tm::Key::Enter), 13);
  // keys NOT yet pressed on the glass must still be placeholders, and a placeholder is never a key
  CHECK_EQ(tm::key_code(tm::Key::Left), tm::kUnlogged);
  CHECK_EQ(tm::key_code(tm::Key::Right), tm::kUnlogged);
  CHECK(tm::key_from_code(tm::kUnlogged) == tm::Key::None);
  CHECK_EQ(tm::page_for_symbol(tm::Key::Enter) == tm::Page::Count, true);

  CHECK(core.start("om-lite"));
  CHECK_EQ(core.alloc().stats("om-lite").blocks, 1);

  // a stopped target: OfferStart, the page does NOT change and nothing is started
  CHECK(core.registry().isRunning("om-lite"));
  const tm::StopResult parked = core.stop("om-lite");
  CHECK(parked.code == tm::StopResult::Ok);
  CHECK(parked.leaked == nullptr);
  CHECK_EQ(core.alloc().stats("om-lite").blocks, 0);
  CHECK_EQ(g_om_starts, 1);
  const tm::RouteResult offered = core.router().symbol(tm::Key::SymSquare, core.registry());
  CHECK(offered.action == tm::RouteAction::OfferStart);
  CHECK(offered.module != nullptr && strcmp(offered.module, "om-lite") == 0);
  CHECK_EQ(core.page() == tm::Page::Launcher, true);
  CHECK_EQ(g_om_starts, 1);   // still one: the router never auto-starts

  // now running: every symbol key reaches its page and is consumed by the core
  CHECK(core.start("om-lite"));
  for (const Want& w : wants) {
    CHECK_EQ(core.handleKey(w.key), true);                  // consumed by the core
    CHECK_EQ(core.page() == w.page, true);
    CHECK_EQ(g_om_keys, 0);                                 // the module NEVER saw a symbol key
  }

  // Esc is UP, not undo (Filip, S502: "folder up button, layer up, not just back"): from any module
  // page it goes to the parent, the Launcher, in ONE step — it does NOT retrace the pages visited on the
  // way (we are on Rptr; the old history rule would have gone Rptr -> Map -> Chat -> Nodes -> Launcher).
  // The module never sees Esc, and at the root Esc changes nothing.
  CHECK_EQ(core.handleKey(tm::Key::Esc), true);
  CHECK_EQ(core.page() == tm::Page::Launcher, true);
  CHECK_EQ(g_om_keys, 0);
  CHECK_EQ(core.handleKey(tm::Key::Esc), true);              // at the root: consumed, nothing above it
  CHECK_EQ(core.page() == tm::Page::Launcher, true);
  CHECK(core.router().back(core.registry()).action == tm::RouteAction::Noop);
  // Two pages visited in a row: Esc from the second still lands on the Launcher, never on the first.
  CHECK(core.goPage(tm::Page::Nodes).action == tm::RouteAction::Switched);
  CHECK(core.goPage(tm::Page::Chat).action == tm::RouteAction::Switched);
  CHECK_EQ(core.handleKey(tm::Key::Esc), true);
  CHECK_EQ(core.page() == tm::Page::Launcher, true);

  // an ordinary key IS the module's, but only while its page is on screen
  CHECK_EQ(core.goPage(tm::Page::Nodes).action == tm::RouteAction::Switched, true);
  CHECK_EQ(core.handleKey(tm::Key::Enter), true);
  CHECK_EQ(g_om_keys, 1);
  core.router().home();
  CHECK_EQ(core.handleKey(tm::Key::Enter), false);         // the Launcher page: the shell's key
  CHECK_EQ(g_om_keys, 1);
}

// ------------------------------------------- 4b. own symbol key -> the page's own top (S514/EST-444)
void test_own_symbol_key_returns_to_the_page_top() {
  printf("test  own page key goes to the page's top; at the top it is a no-op\n");
  g_om_starts = g_om_draws = g_om_keys = 0;
  g_om_tops = 0;
  g_om_inner = false;
  g_om_block = nullptr;

  static const tm::Module om = {"om-lite", "OM Lite", "mesh client", kAllOmPages, true,
                                tm::Resource::Radio, om_start, om_stop_clean, nullptr, om_draw,
                                om_key, om_top, nullptr};
  tm::Core core;
  CHECK(core.registry().add(om));
  CHECK(core.start("om-lite"));
  CHECK_EQ(core.goPage(tm::Page::Chat).action == tm::RouteAction::Switched, true);

  // the table itself: only the SQUARE is the Chat page's own key now (S516/EST-478).
  CHECK_EQ(tm::is_own_symbol(tm::Key::SymSquare, tm::Page::Chat), true);
  CHECK_EQ(tm::is_own_symbol(tm::Key::SymCircle, tm::Page::Chat), false);
  CHECK_EQ(tm::is_own_symbol(tm::Key::SymX, tm::Page::Launcher), true);
  CHECK_EQ(tm::is_own_symbol(tm::Key::SymDiamond, tm::Page::Settings), true);
  CHECK_EQ(tm::is_own_symbol(tm::Key::SymDiamond, tm::Page::Rptr), false);
  // …and the whole new map, page by page.
  CHECK_EQ(tm::page_for_symbol(tm::Key::SymX) == tm::Page::Launcher, true);
  CHECK_EQ(tm::page_for_symbol(tm::Key::SymTriangle) == tm::Page::Nodes, true);
  CHECK_EQ(tm::page_for_symbol(tm::Key::SymSquare) == tm::Page::Chat, true);
  CHECK_EQ(tm::page_for_symbol(tm::Key::SymCircle) == tm::Page::Map, true);
  CHECK_EQ(tm::page_for_symbol(tm::Key::SymCloud) == tm::Page::Rptr, true);
  CHECK_EQ(tm::page_for_symbol(tm::Key::SymDiamond) == tm::Page::Settings, true);
  // A typed comma is not a page/Settings key: core/keys.h says so, and main.cpp never maps it.
  CHECK_EQ(tm::is_settings_key(tm::Key::SymDiamond), true);
  CHECK_EQ(tm::is_settings_key(tm::Key::Char), false);

  // INSIDE a level: the router says Top and the core hands it to the page's owner, which closes it.
  // The page does NOT move, and the module's ordinary onKey never sees a page key.
  g_om_inner = true;
  const tm::RouteResult own = core.router().symbol(tm::Key::SymSquare, core.registry());
  CHECK(own.action == tm::RouteAction::Top);
  CHECK_EQ(own.page == tm::Page::Chat, true);
  CHECK(own.module != nullptr && strcmp(own.module, "om-lite") == 0);
  CHECK_EQ(core.handleKey(tm::Key::SymSquare), true);
  CHECK_EQ(g_om_tops, 1);
  CHECK_EQ(g_om_top_page == tm::Page::Chat, true);   // the module was told WHICH page
  CHECK_EQ(g_om_inner, false);                     // the inner level is closed
  CHECK_EQ(core.page() == tm::Page::Chat, true);   // still the same page
  CHECK_EQ(g_om_keys, 0);

  // AT THE TOP: consumed, the owner is asked, and it answers "nothing to close" — a no-op.
  CHECK_EQ(core.handleKey(tm::Key::SymSquare), true);
  CHECK_EQ(g_om_tops, 2);
  CHECK_EQ(g_om_inner, false);
  CHECK_EQ(core.page() == tm::Page::Chat, true);

  // A DIFFERENT page's own key must NOT close the Chat level: OM Lite owns six pages. Put the fake
  // level back, go to Nodes and press triangle (Nodes' own key); the hook is told Nodes and leaves it.
  g_om_inner = true;
  CHECK_EQ(core.goPage(tm::Page::Nodes).action == tm::RouteAction::Switched, true);
  CHECK_EQ(core.handleKey(tm::Key::SymTriangle), true);
  CHECK_EQ(core.page() == tm::Page::Nodes, true);
  CHECK_EQ(g_om_tops, 3);
  CHECK_EQ(g_om_top_page == tm::Page::Nodes, true);
  CHECK_EQ(g_om_inner, true);                        // the Chat level is untouched
  // … and back on Chat, the square does close it.
  CHECK_EQ(core.goPage(tm::Page::Chat).action == tm::RouteAction::Switched, true);
  CHECK_EQ(core.handleKey(tm::Key::SymSquare), true);
  CHECK_EQ(g_om_tops, 4);
  CHECK_EQ(g_om_top_page == tm::Page::Chat, true);
  CHECK_EQ(g_om_inner, false);

  // ANOTHER page's key routes as before and does not touch this page's level hook.
  CHECK_EQ(core.handleKey(tm::Key::SymTriangle), true);
  CHECK_EQ(core.page() == tm::Page::Nodes, true);
  CHECK_EQ(g_om_tops, 4);

  // Settings' own key is the DIAMOND: on Settings the router answers Top and the owner is told Settings.
  g_om_settings_sub = true;
  CHECK_EQ(core.goPage(tm::Page::Settings).action == tm::RouteAction::Switched, true);
  CHECK_EQ(core.handleKey(tm::Key::SymDiamond), true);
  CHECK_EQ(core.page() == tm::Page::Settings, true);
  CHECK_EQ(g_om_tops, 5);
  CHECK_EQ(g_om_top_page == tm::Page::Settings, true);
  CHECK_EQ(g_om_settings_sub, false);   // its sub-page closed
  CHECK_EQ(core.handleKey(tm::Key::SymDiamond), true);
  CHECK_EQ(g_om_tops, 6);               // asked again at the root …
  CHECK_EQ(g_om_settings_sub, false);   // … and nothing to close

  // The Launcher's own key (X) has no module: it is consumed and changes nothing.
  core.router().home();
  CHECK(core.router().symbol(tm::Key::SymX, core.registry()).action == tm::RouteAction::Noop);
  CHECK_EQ(core.handleKey(tm::Key::SymX), true);
  CHECK_EQ(core.page() == tm::Page::Launcher, true);
  CHECK_EQ(g_om_tops, 6);
}

// ------------------------------------------- 4c. own key AT the top -> the first row (S514/EST-454)
void test_own_key_at_the_top_selects_the_first_row() {
  printf("test  own key at the top selects the first row; inside a level it only closes one step\n");
  om_top_reset_focus();
  g_om_starts = g_om_draws = g_om_keys = 0;
  g_om_block  = nullptr;

  static const tm::Module om = {"om-lite", "OM Lite", "mesh client", kAllOmPages, true,
                                tm::Resource::Radio, om_start, om_stop_clean, nullptr, om_draw,
                                om_key, om_top, nullptr};
  tm::Core core;
  CHECK(core.registry().add(om));
  CHECK(core.start("om-lite"));
  CHECK_EQ(core.goPage(tm::Page::Chat).action == tm::RouteAction::Switched, true);

  // Chat list, focus on row 5 in the DM column with both columns scrolled: the own key goes to the
  // first CHANNEL card (column 0, row 0) and resets BOTH column scrolls.
  g_om_col       = om::mc::kColDms;
  g_om_row       = 5;
  g_om_scroll[0] = 3;
  g_om_scroll[1] = 4;
  CHECK_EQ(core.handleKey(tm::Key::SymSquare), true);
  CHECK_EQ(g_om_tops, 1);
  CHECK_EQ(g_om_col, om::mc::kColChannels);
  CHECK_EQ(g_om_row, 0);
  CHECK_EQ(g_om_scroll[0], 0);
  CHECK_EQ(g_om_scroll[1], 0);
  CHECK_EQ(g_om_inner, false);
  CHECK_EQ(core.page() == tm::Page::Chat, true);

  // Inside an open channel: the first press only closes it; the focus and the column scrolls stay put.
  g_om_inner     = true;
  g_om_col       = om::mc::kColChannels;
  g_om_row       = 5;
  g_om_scroll[0] = 3;
  g_om_scroll[1] = 4;
  CHECK_EQ(core.handleKey(tm::Key::SymSquare), true);
  CHECK_EQ(g_om_tops, 2);
  CHECK_EQ(g_om_inner, false);
  CHECK_EQ(g_om_col, om::mc::kColChannels);
  CHECK_EQ(g_om_row, 5);
  CHECK_EQ(g_om_scroll[0], 3);
  CHECK_EQ(g_om_scroll[1], 4);

  // The second press is now at the top: first row again.
  CHECK_EQ(core.handleKey(tm::Key::SymSquare), true);
  CHECK_EQ(g_om_tops, 3);
  CHECK_EQ(g_om_col, om::mc::kColChannels);
  CHECK_EQ(g_om_row, 0);
  CHECK_EQ(g_om_scroll[0], 0);
  CHECK_EQ(g_om_scroll[1], 0);

  // Nodes: row 4 -> the first node. The search bar is the inner level, so there one press only closes it.
  CHECK_EQ(core.goPage(tm::Page::Nodes).action == tm::RouteAction::Switched, true);
  g_om_nodes_bar = false;
  g_om_nodes_row = 4;
  CHECK_EQ(core.handleKey(tm::Key::SymTriangle), true);
  CHECK_EQ(g_om_tops, 4);
  CHECK_EQ(g_om_nodes_row, 0);
  CHECK_EQ(g_om_nodes_bar, false);
  g_om_nodes_bar = true;
  g_om_nodes_row = 4;
  CHECK_EQ(core.handleKey(tm::Key::SymTriangle), true);
  CHECK_EQ(g_om_tops, 5);
  CHECK_EQ(g_om_nodes_bar, false);
  CHECK_EQ(g_om_nodes_row, 4);   // one step, no jump
  CHECK_EQ(core.handleKey(tm::Key::SymTriangle), true);
  CHECK_EQ(g_om_nodes_row, 0);   // the next press is at the top

  // RPTR: the same rule because it has a list; login/status is its inner level.
  CHECK_EQ(core.goPage(tm::Page::Rptr).action == tm::RouteAction::Switched, true);
  g_om_rptr_mode = 0;
  g_om_rptr_row  = 3;
  CHECK_EQ(core.handleKey(tm::Key::SymCloud), true);
  CHECK_EQ(g_om_rptr_row, 0);
  g_om_rptr_mode = 1;
  g_om_rptr_row  = 3;
  CHECK_EQ(core.handleKey(tm::Key::SymCloud), true);
  CHECK_EQ(g_om_rptr_mode, 0);
  CHECK_EQ(g_om_rptr_row, 3);    // one step
  CHECK_EQ(core.handleKey(tm::Key::SymCloud), true);
  CHECK_EQ(g_om_rptr_row, 0);

  // Map has no list: the hook is asked and answers false, and there is no row to move.
  CHECK_EQ(core.goPage(tm::Page::Map).action == tm::RouteAction::Switched, true);
  const int tops_before_map = g_om_tops;
  CHECK_EQ(core.handleKey(tm::Key::SymCircle), true);
  CHECK_EQ(g_om_tops, tops_before_map + 1);
}

// ------------------------------------------------------------------- 5. states + one drawing owner
void test_states_and_one_drawing_owner() {
  printf("test  states (running/stopped/later) and one drawing owner\n");
  g_om_starts = g_om_draws = 0;
  g_om_block = nullptr;

  static const tm::Module om = {"om-lite", "OM Lite", "mesh client", kAllOmPages, true,
                                tm::Resource::Radio, om_start, om_stop_clean, nullptr, om_draw,
                                om_key, nullptr, nullptr};
  static const tm::Module later = {"mesh-torry", "Mesh-Torry", "game", 0, false, tm::Resource::None,
                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  tm::Core core;
  CHECK(core.registry().add(om));
  CHECK(core.registry().add(later));

  CHECK(core.registry().state("mesh-torry") == tm::ModuleState::Later);
  CHECK_EQ(static_cast<int>(core.start("mesh-torry").code),
           static_cast<int>(tm::StartResult::NotAvailable));
  CHECK_EQ(core.registry().state("mesh-torry") == tm::ModuleState::Later, true);

  CHECK(core.start("om-lite"));
  CHECK_EQ(core.goPage(tm::Page::Nodes).action == tm::RouteAction::Switched, true);

  DummyCanvas canvas;
  CHECK_EQ(g_om_draws, 0);
  const char* who = core.draw(canvas);
  CHECK(who != nullptr && strcmp(who, "om-lite") == 0);
  CHECK_EQ(g_om_draws, 1);                                   // the module drew …
  const int after_module_draw = g_om_draws;

  // the Launcher page draws INSTEAD of the module, never beside it (its own key is X now)
  CHECK_EQ(core.handleKey(tm::Key::SymX), true);
  CHECK_EQ(core.page() == tm::Page::Launcher, true);
  who = core.draw(canvas);
  CHECK(who != nullptr && strcmp(who, "launcher") == 0);
  CHECK_EQ(g_om_draws, after_module_draw);                   // … and the module did not draw again
  CHECK_EQ(core.registry().isRunning("om-lite"), true);      // it keeps running off screen

  // stopping the module that is on screen drops the page back to the Launcher
  CHECK_EQ(core.goPage(tm::Page::Chat).action == tm::RouteAction::Switched, true);
  CHECK_EQ(core.page() == tm::Page::Chat, true);
  const tm::StopResult off = core.stop("om-lite");
  CHECK(off.code == tm::StopResult::Ok);
  CHECK(off.leaked == nullptr);
  CHECK_EQ(core.page() == tm::Page::Launcher, true);
  who = core.draw(canvas);
  CHECK(who != nullptr && strcmp(who, "launcher") == 0);

  CHECK_EQ(core.alloc().total().blocks, 0);
  CHECK_EQ(core.registry().runningCount(), 0u);
}

// ---- step 3a: the radio plan and the beacon -------------------------------------------------------
// These exist because two of my own first-pass values were WRONG and only the sources caught them: the
// preamble (8 instead of MeshCore's 32 for SF<=8) and the bandwidth unit (62.5 kHz must be the integer
// 62; anything else silently becomes 125 kHz in the component). A host test is cheaper than finding it
// on the air at 22 dBm.
void test_radio_plan_matches_the_mesh() {
  // The SLO preset, as the mesh runs it.
  CHECK_EQ(tm::kSloPlan.freq_hz, 869618000u);
  CHECK_EQ(tm::kSloPlan.bw_khz, 62);          // the chip's 62.5 kHz step, as an integer
  CHECK_EQ(tm::kSloPlan.sf, 8);
  CHECK_EQ(tm::kSloPlan.cr, 8);               // 4/8, not RadioLib's usual default
  CHECK_EQ(tm::kSloPlan.sync_word, 0x12);     // MeshCore's sync word
  CHECK_EQ(tm::kSloPlan.preamble, 32);        // MeshCore: sf <= 8 ? 32 : 16
  CHECK_EQ(tm::kSloPlan.power_dbm, 22);

  CHECK_EQ(tm::radio_plan_valid(tm::kSloPlan), true);

  // The two traps, armed: each of these is a value a reasonable person could write.
  tm::RadioPlan bad_bw = tm::kSloPlan;
  bad_bw.bw_khz = 62500;                      // Hz instead of kHz
  CHECK_EQ(tm::radio_plan_valid(bad_bw), false);
  bad_bw.bw_khz = 63;                         // rounded up from "62.5"
  CHECK_EQ(tm::radio_plan_valid(bad_bw), false);

  tm::RadioPlan bad_power = tm::kSloPlan;
  bad_power.power_dbm = 23;                   // sx126x rejects >22 in high-power mode
  CHECK_EQ(tm::radio_plan_valid(bad_power), false);

  tm::RadioPlan bad_sf = tm::kSloPlan;
  bad_sf.sf = 13;
  CHECK_EQ(tm::radio_plan_valid(bad_sf), false);

  // LDRO stays off at SF8 / 62.5 kHz (a symbol is 4.096 ms, under the 16 ms rule)...
  CHECK_EQ(tm::radio_ldro_recommended(tm::kSloPlan), false);
  CHECK_EQ(tm::kSloPlan.ldro, false);
  // ...and the rule still fires where it should, including at the edge that would overflow a 32-bit
  // microsecond count.
  tm::RadioPlan slow = tm::kSloPlan;
  slow.sf = 12;
  slow.bw_khz = 7;
  CHECK_EQ(tm::radio_ldro_recommended(slow), true);
}

void test_beacon_roundtrips() {
  uint8_t buf[16];
  CHECK_EQ(tm::radio_beacon_build(buf, sizeof(buf), 0x01020304u, 4154), tm::kBeaconBytes);
  CHECK_EQ(buf[0], 'O');
  CHECK_EQ(buf[4], tm::kBeaconVersion);
  CHECK_EQ(buf[5], 0x04);                     // little-endian sequence
  CHECK_EQ(buf[6], 0x03);
  CHECK_EQ(buf[8], 0x01);

  uint32_t seq = 0;
  int16_t  mv  = 0;
  CHECK_EQ(tm::radio_beacon_parse(buf, tm::kBeaconBytes, &seq, &mv), true);
  CHECK_EQ(seq, 0x01020304u);
  CHECK_EQ(mv, 4154);

  // A negative value survives the trip (kBeaconNoBattery = -1): the sign is not lost.
  tm::radio_beacon_build(buf, sizeof(buf), 1, tm::kBeaconNoBattery);
  CHECK_EQ(tm::radio_beacon_parse(buf, tm::kBeaconBytes, &seq, &mv), true);
  CHECK_EQ(mv, tm::kBeaconNoBattery);

  // A short buffer is refused rather than overrun, and parsing is exact-length.
  CHECK_EQ(tm::radio_beacon_build(buf, 4, 1, 1), 0u);
  CHECK_EQ(tm::radio_beacon_parse(buf, tm::kBeaconBytes + 1, &seq, &mv), false);
  CHECK_EQ(tm::radio_beacon_parse(buf, tm::kBeaconBytes - 1, &seq, &mv), false);

  // Wrong magic is rejected and leaves the outputs alone.
  buf[0] = 'X';
  seq = 7;
  mv  = 7;
  CHECK_EQ(tm::radio_beacon_parse(buf, tm::kBeaconBytes, &seq, &mv), false);
  CHECK_EQ(seq, 7u);
  CHECK_EQ(mv, 7);
}

}  // namespace

int main() {
  test_start_stop_start_keeps_memory();
  test_leak_is_reported_by_name();
  test_second_radio_owner_is_refused();
  test_symbol_keys_route_and_are_not_swallowed();
  test_own_symbol_key_returns_to_the_page_top();
  test_own_key_at_the_top_selects_the_first_row();
  test_states_and_one_drawing_owner();
  test_radio_plan_matches_the_mesh();
  test_beacon_roundtrips();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
