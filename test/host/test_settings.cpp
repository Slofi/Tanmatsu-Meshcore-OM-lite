// SPDX-License-Identifier: MIT
// Host tests for Settings (EST-445, restructured per app in S514/EST-448). Real exit code: 0 = every check
// passed.
//
// These are the decisions the panel and the radio task run; a wrong name or interval fails silently on the
// mesh, a wrong scroll/focus rule is only found by a person with the device in their hands, and a tab order
// that drifts from the registry would put another app's settings under the wrong name.
#include <stdio.h>
#include <string.h>

#include "advert_rule.h"
#include "core.h"
#include "menu_cards.h"
#include "settings.h"

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++g_checks;                                                           \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
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
      printf("FAIL %s:%d  %s == %s (got %ld, want %ld)\n", __FILE__, __LINE__,      \
             #got, #want, g_, w_);                                                    \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

#define CHECK_STR(got, want)                                                         \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (strcmp((got), (want)) != 0) {                                                \
      printf("FAIL %s:%d  \"%s\" == \"%s\"\n", __FILE__, __LINE__, (got), (want));  \
      ++g_failures;                                                                  \
      return;                                                                        \
    }                                                                                \
  } while (0)

// -------------------------------------------------------------------------------------------- 1. name
void test_name_chars() {
  printf("test  MeshCore node-name characters\n");
  CHECK(tm::nodeNameCharAllowed('A'));
  CHECK(tm::nodeNameCharAllowed('z'));
  CHECK(tm::nodeNameCharAllowed('0'));
  CHECK(tm::nodeNameCharAllowed(' '));      // "TNMT (SI)" has a space
  CHECK(tm::nodeNameCharAllowed('-'));
  CHECK(tm::nodeNameCharAllowed('('));
  CHECK(tm::nodeNameCharAllowed(')'));

  CHECK(!tm::nodeNameCharAllowed('['));
  CHECK(!tm::nodeNameCharAllowed(']'));
  CHECK(!tm::nodeNameCharAllowed('\\'));
  CHECK(!tm::nodeNameCharAllowed(':'));
  CHECK(!tm::nodeNameCharAllowed(','));
  CHECK(!tm::nodeNameCharAllowed('?'));
  CHECK(!tm::nodeNameCharAllowed('*'));
  CHECK(!tm::nodeNameCharAllowed('\x1f'));   // a control byte
  CHECK(!tm::nodeNameCharAllowed('\x7f'));
  CHECK(!tm::nodeNameCharAllowed('\xc5'));   // a UTF-8 lead byte: the L2's rule is printable ASCII
}

void test_name_trim_and_length() {
  printf("test  node name: trim, 1..31 bytes, nothing invalid\n");
  char out[tm::kNodeNameMaxBytes + 1];

  CHECK_EQ(tm::nodeNameTrim("  TNMT (SI)  ", out, sizeof(out)), 9);
  CHECK_STR(out, "TNMT (SI)");
  CHECK_EQ(tm::nodeNameTrim("A  B", out, sizeof(out)), 4);   // internal spaces are the user's
  CHECK_STR(out, "A  B");
  CHECK_EQ(tm::nodeNameTrim("     ", out, sizeof(out)), 0);
  CHECK_STR(out, "");

  CHECK(tm::nodeNameValid("TNMT (SI)"));
  CHECK(tm::nodeNameValid("ABC 123"));

  char max31[32];
  for (int i = 0; i < 31; ++i) max31[i] = 'A';
  max31[31] = '\0';
  CHECK(tm::nodeNameValid(max31));

  char max32[33];
  for (int i = 0; i < 32; ++i) max32[i] = 'A';
  max32[32] = '\0';
  CHECK(!tm::nodeNameValid(max32));

  CHECK(!tm::nodeNameValid(""));
  CHECK(!tm::nodeNameValid(" leading"));
  CHECK(!tm::nodeNameValid("trailing "));
  CHECK(!tm::nodeNameValid("A["));
  CHECK(!tm::nodeNameValid("A?B"));

  char applied[32];
  CHECK(tm::nodeNameApply("  TNMT (SI)  ", applied, sizeof(applied)));
  CHECK_STR(applied, "TNMT (SI)");
  CHECK(!tm::nodeNameApply("   ", applied, sizeof(applied)));
  CHECK(!tm::nodeNameApply("bad*name", applied, sizeof(applied)));
  CHECK(!tm::nodeNameApply(max32, applied, sizeof(applied)));
  CHECK_STR(applied, "TNMT (SI)");   // a rejected draft never overwrites the confirmed one

  CHECK_STR(tm::kDefaultNodeName, "TNMT (SI)");
  CHECK_EQ(tm::kNodeNameMaxBytes, 31);
}

void test_name_draft_typing() {
  printf("test  the shared text field stops at 31 and refuses a forbidden key\n");
  bool ok = false;
  CHECK(tm::nodeNameDraftPut('A', 0, &ok));
  CHECK(ok);
  CHECK(tm::nodeNameDraftPut('A', 30, &ok));
  CHECK(ok);
  CHECK(!tm::nodeNameDraftPut('A', 31, &ok));
  CHECK(!ok);
  CHECK(!tm::nodeNameDraftPut('[', 3, &ok));
  CHECK(!ok);
  CHECK(!tm::nodeNameDraftPut('\x7f', 3, &ok));
}

// ------------------------------------------------------------------------------------------ 2. adverts
void test_advert_interval_mapping() {
  printf("test  automatic advert: Off / 1 / 3 / 6 / 12 h, default Off (S514)\n");
  CHECK_STR(tm::advertIntervalLabel(tm::AdvertInterval::Off), "Off");
  CHECK_STR(tm::advertIntervalLabel(tm::AdvertInterval::H1), "1 h");
  CHECK_STR(tm::advertIntervalLabel(tm::AdvertInterval::H3), "3 h");
  CHECK_STR(tm::advertIntervalLabel(tm::AdvertInterval::H6), "6 h");
  CHECK_STR(tm::advertIntervalLabel(tm::AdvertInterval::H12), "12 h");

  CHECK_EQ(tm::advertIntervalSeconds(tm::AdvertInterval::Off), 0);
  CHECK_EQ(tm::advertIntervalSeconds(tm::AdvertInterval::H1), 3600);
  CHECK_EQ(tm::advertIntervalSeconds(tm::AdvertInterval::H3), 10800);
  CHECK_EQ(tm::advertIntervalSeconds(tm::AdvertInterval::H6), 21600);
  CHECK_EQ(tm::advertIntervalSeconds(tm::AdvertInterval::H12), 43200);

  CHECK(tm::advertIntervalDefault() == tm::AdvertInterval::Off);           // S514, Filip: Off by default
  CHECK_EQ(tm::advertIntervalSeconds(tm::advertIntervalDefault()), 0);

  CHECK(tm::advertIntervalCycle(tm::AdvertInterval::Off) == tm::AdvertInterval::H1);
  CHECK(tm::advertIntervalCycle(tm::AdvertInterval::H1) == tm::AdvertInterval::H3);
  CHECK(tm::advertIntervalCycle(tm::AdvertInterval::H3) == tm::AdvertInterval::H6);
  CHECK(tm::advertIntervalCycle(tm::AdvertInterval::H6) == tm::AdvertInterval::H12);
  CHECK(tm::advertIntervalCycle(tm::AdvertInterval::H12) == tm::AdvertInterval::Off);

  CHECK(tm::advertIntervalFromSeconds(0) == tm::AdvertInterval::Off);
  CHECK(tm::advertIntervalFromSeconds(3600) == tm::AdvertInterval::H1);
  CHECK(tm::advertIntervalFromSeconds(10800) == tm::AdvertInterval::H3);
  CHECK(tm::advertIntervalFromSeconds(21600) == tm::AdvertInterval::H6);
  CHECK(tm::advertIntervalFromSeconds(43200) == tm::AdvertInterval::H12);
  CHECK(tm::advertIntervalFromSeconds(7200) == tm::advertIntervalDefault());   // corrupt -> default (Off since S514)
  CHECK(tm::advertIntervalFromSeconds(0xFFFFFFFFu) == tm::AdvertInterval::Off);

  CHECK(tm::advertIntervalStoredValid(0));
  CHECK(tm::advertIntervalStoredValid(10800));
  CHECK(!tm::advertIntervalStoredValid(7200));
}

void test_advert_schedule_and_manual_path() {
  printf("test  Off = no scheduled adverts; the manual 'a' path still works\n");

  // Off is never due, not even on the first pass after a boot.
  CHECK(!tm::advertScheduleDue(0, 0, false));
  CHECK(!tm::advertScheduleDue(0, 999999, true));
  // 3 h: due on the first pass, then only once the interval has elapsed.
  CHECK(!tm::advertScheduleDue(10800, 0, false));      // S514: no advert at boot any more
  CHECK(!tm::advertScheduleDue(10800, 10799, false));  // ... nor before one interval since boot
  CHECK(tm::advertScheduleDue(10800, 10800, false));   // the first one: one interval after start
  CHECK(!tm::advertScheduleDue(10800, 10799, true));
  CHECK(tm::advertScheduleDue(10800, 10800, true));
  CHECK(tm::advertScheduleDue(10800, 99999, true));

  // The manual path is core/advert_rule.h, which never reads the interval: a fresh gate may send even
  // when the stored interval is Off, and the 5 s gate still applies afterwards.
  om::mc::AdvertGate g;
  CHECK(om::mc::advertRequest(g, 0).verdict == om::mc::AdvertVerdict::Send);
  om::mc::advertQueued(g);
  CHECK(om::mc::advertRequest(g, 1000).verdict == om::mc::AdvertVerdict::Busy);
  om::mc::advertResolved(g, 1000, true);
  const om::mc::AdvertDecision wait = om::mc::advertRequest(g, 2000);
  CHECK(wait.verdict == om::mc::AdvertVerdict::Wait);
  CHECK_EQ(wait.wait_s, 4);
}

// ------------------------------------------------------------------------------- 3. module tab registry
int g_actions = 0;
int g_cycles  = 0;
void onAction(tm::Core&, const tm::SettingRow&) { ++g_actions; }
void onCycle(tm::Core&, const tm::SettingRow&) { ++g_cycles; }

// A General-shaped tab: the same section order and row kinds the shell supplies.
const tm::SettingRow kNodeRows[] = {
    {tm::SettingKind::SubPage, 1, "Name", "TNMT (SI)"},
    {tm::SettingKind::Info, 2, "Public key", "a1b2c3d4e5f60718"},
    {tm::SettingKind::Action, 3, "Advert now", "Enter", nullptr, onAction, true},
};
const tm::SettingRow kAdvertRows[] = {
    {tm::SettingKind::Cycle, 4, "Automatic advert", "3 h", nullptr, onCycle},
};
const tm::SettingRow kRadioRows[] = {
    {tm::SettingKind::Info, 5, "Frequency", "869.618 MHz"},
    {tm::SettingKind::Info, 6, "Bandwidth", "62.5 kHz"},
    {tm::SettingKind::Info, 7, "Spreading", "SF 8"},
    {tm::SettingKind::Info, 8, "Coding", "4/8"},
    {tm::SettingKind::Info, 9, "TX power", "22 dBm"},
    {tm::SettingKind::Info, 10, "Path hash", "2 B"},
};
const tm::SettingRow kDeviceRows[] = {
    {tm::SettingKind::Info, 11, "Time", "22:31 · rtc"},
    {tm::SettingKind::Info, 12, "SD card", "mounted · history on"},
    {tm::SettingKind::Info, 13, "Contacts", "3 / 32"},
    {tm::SettingKind::Info, 14, "App version", "44"},
    {tm::SettingKind::Info, 15, "Free internal heap", "120 KB"},
    {tm::SettingKind::Info, 16, "Free PSRAM", "26.1 MB"},
};
const tm::SettingsSection kGeneral[] = {
    {"NODE", kNodeRows, 3},
    {"ADVERT", kAdvertRows, 1},
    {"RADIO (READ ONLY)", kRadioRows, 6},
    {"DEVICE (READ ONLY)", kDeviceRows, 6},
};
constexpr int kGeneralCount = 4;

// The real OM Lite shape: ONE scrolling list, NODES · CHAT · MAP · RPTR. RPTR is the honest one-liner.
const tm::SettingRow kOmNodeRow[] = {{tm::SettingKind::Info, 0, "Node list, keys and favourites: EST-447", ""}};
const tm::SettingRow kOmChatRow[] = {{tm::SettingKind::Info, 0, "Channels and per-channel scope: EST-446", ""}};
const tm::SettingRow kOmMapRow[]  = {{tm::SettingKind::Info, 0, "Map tiles, zoom range and centre: EST-447", ""}};
const tm::SettingRow kOmRptrRow[] = {{tm::SettingKind::Info, 0, "Repeater admin comes later", ""}};
const tm::SettingsSection kOmSections[] = {
    {"NODES", kOmNodeRow, 1},
    {"CHAT", kOmChatRow, 1},
    {"MAP", kOmMapRow, 1},
    {"RPTR", kOmRptrRow, 1},
};
constexpr int kOmSectionCount = 4;

const tm::SettingsSection* omSettingsHook(tm::Core&, int* count) {
  if (count != nullptr) *count = kOmSectionCount;
  return kOmSections;
}

// Ten rows per section, so a section lower down really has to scroll into view.
const tm::SettingRow kTallRows[] = {
    {tm::SettingKind::Info, 0, "chat 0", "v"}, {tm::SettingKind::Info, 1, "row 1", "v"},
    {tm::SettingKind::Info, 2, "row 2", "v"},  {tm::SettingKind::Info, 3, "row 3", "v"},
    {tm::SettingKind::Info, 4, "row 4", "v"},  {tm::SettingKind::Info, 5, "row 5", "v"},
    {tm::SettingKind::Info, 6, "row 6", "v"},  {tm::SettingKind::Info, 7, "row 7", "v"},
    {tm::SettingKind::Info, 8, "row 8", "v"},  {tm::SettingKind::Info, 9, "row 9", "v"},
};
const tm::SettingsSection kTallSections[] = {
    {"NODES", kTallRows, 10},
    {"CHAT", kTallRows, 10},
    {"MAP", kTallRows, 10},
    {"RPTR", kTallRows, 10},
};

// Module field order is core/module.h: id, name, meta, pages, available, owns, start, stop, tick, draw,
// onKey, onTop, settings.
const tm::Module kOmModule = {"om-lite", "OM Lite", "mesh companion", 0x3F, true, tm::Resource::None,
                              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, omSettingsHook};
const tm::Module kOpsModule = {"ops-toc", "OPS-TOC", "field ops", 0, false, tm::Resource::None,
                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
const tm::Module kTorryModule = {"mesh-torry", "Mesh-Torry", "the game", 0, false, tm::Resource::None,
                                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

void addApps(tm::Registry& reg, bool reversed = false) {
  if (reversed) {
    CHECK(reg.add(kTorryModule));
    CHECK(reg.add(kOpsModule));
    CHECK(reg.add(kOmModule));
  } else {
    CHECK(reg.add(kOmModule));
    CHECK(reg.add(kOpsModule));
    CHECK(reg.add(kTorryModule));
  }
}

void test_tab_order_from_registry() {
  printf("test  top tabs are General + the registry, in the registry's own order\n");
  tm::Registry reg;
  addApps(reg);
  tm::SettingsTabDef tabs[tm::kSettingsMaxTabs];
  const int n = tm::settingsTabDefs(reg, tabs, tm::kSettingsMaxTabs);
  CHECK_EQ(n, 4);

  CHECK_STR(tabs[0].id, "general");
  CHECK_STR(tabs[0].label, "General");
  CHECK(tabs[0].module == nullptr);       // the core owns General
  CHECK(!tabs[0].later);
  CHECK_STR(tabs[1].label, "OM Lite");
  CHECK_STR(tabs[2].label, "OPS-TOC");
  CHECK_STR(tabs[3].label, "Mesh-Torry");

  CHECK(!tabs[1].later);
  CHECK(tabs[2].later);                   // LATER module: tab PRESENT
  CHECK(tabs[3].later);
  CHECK(tabs[1].settings == omSettingsHook);
  CHECK(tabs[2].settings == nullptr);     // no settings data yet
  CHECK(tabs[3].settings == nullptr);

  // Reordering the start menu reorders the tabs: nothing here is a second hard-coded app list.
  tm::Registry reversed;
  addApps(reversed, true);
  const int rn = tm::settingsTabDefs(reversed, tabs, tm::kSettingsMaxTabs);
  CHECK_EQ(rn, 4);
  CHECK_STR(tabs[0].label, "General");
  CHECK_STR(tabs[1].label, "Mesh-Torry");
  CHECK_STR(tabs[2].label, "OPS-TOC");
  CHECK_STR(tabs[3].label, "OM Lite");
}

void test_module_content_and_placeholder() {
  printf("test  a stopped module still shows its settings; no data = the one honest line\n");
  tm::Registry reg;
  addApps(reg);
  tm::SettingsTabDef tabs[tm::kSettingsMaxTabs];
  const int n = tm::settingsTabDefs(reg, tabs, tm::kSettingsMaxTabs);
  CHECK_EQ(n, 4);
  tm::Core core;

  // OM Lite is STOPPED here; the hook is data, not a running service, so all four sections still answer.
  CHECK(reg.state("om-lite") == tm::ModuleState::Stopped);
  int count = 99;
  const tm::SettingsSection* om = tm::settingsTabContent(tabs[1], core, &count);
  CHECK(om != nullptr);
  CHECK_EQ(count, 4);
  CHECK_STR(om[0].header, "NODES");
  CHECK_STR(om[1].header, "CHAT");
  CHECK_STR(om[2].header, "MAP");
  CHECK_STR(om[3].header, "RPTR");
  CHECK_STR(om[3].rows[0].label, "Repeater admin comes later");

  // A module with no settings data: no sections at all, and the frame's one-line state is what remains.
  count = 99;
  CHECK(tm::settingsTabContent(tabs[2], core, &count) == nullptr);
  CHECK_EQ(count, 0);
  CHECK_STR(tm::kSettingsPlaceholder, "Settings come with the app");
  CHECK_EQ(tm::settingsElementCount(nullptr, 0), 0);
  CHECK_EQ(tm::settingsFirstFocusable(nullptr, 0), -1);

  // A LATER tab is present and still draws that line: the ticket's OPS-TOC preview.
  CHECK(tabs[2].later);
  CHECK_STR(tabs[2].label, "OPS-TOC");
}

void test_target_for_page() {
  printf("test  ',' from an OM Lite page -> its tab and SECTION; menu -> General\n");
  tm::Registry reg;
  addApps(reg);

  tm::SettingsTarget t = tm::settingsTargetForPage(reg, tm::Page::Nodes);
  CHECK_EQ(t.tab, 1);
  CHECK_EQ(t.section, 0);
  t = tm::settingsTargetForPage(reg, tm::Page::Chat);
  CHECK_EQ(t.tab, 1);
  CHECK_EQ(t.section, 1);
  t = tm::settingsTargetForPage(reg, tm::Page::Map);
  CHECK_EQ(t.tab, 1);
  CHECK_EQ(t.section, 2);
  t = tm::settingsTargetForPage(reg, tm::Page::Rptr);
  CHECK_EQ(t.tab, 1);
  CHECK_EQ(t.section, 3);

  t = tm::settingsTargetForPage(reg, tm::Page::Launcher);
  CHECK_EQ(t.tab, 0);
  CHECK_EQ(t.section, -1);
  t = tm::settingsTargetForPage(reg, tm::Page::Settings);
  CHECK_EQ(t.tab, 0);
  CHECK_EQ(t.section, -1);

  // The tab index follows the registry too: with Mesh-Torry first and OM Lite third, OM Lite is tab 3.
  tm::Registry reversed;
  addApps(reversed, true);
  t = tm::settingsTargetForPage(reversed, tm::Page::Chat);
  CHECK_EQ(t.tab, 3);
  CHECK_EQ(t.section, 1);
}

void test_section_jump() {
  printf("test  ',' lands on the section's first row with the section at the top\n");
  tm::SettingsFrame f;
  f.setBodyHeight(100);
  f.reset(1, 4);
  f.focusSection(kTallSections, 4, 1);   // CHAT
  CHECK_EQ(f.focus, 12);                 // header 11, first CHAT row 12
  CHECK_EQ(f.scroll, tm::settingsScrollToSection(kTallSections, 4, 1, 100));
  CHECK_EQ(f.scroll, 22 + 10 * 34);      // NODES: header + ten rows
  const tm::SettingRow* row = tm::settingsRowAt(kTallSections, 4, f.focus);
  CHECK(row != nullptr);
  CHECK_STR(row->label, "chat 0");

  f.focusSection(kTallSections, 4, 3);   // RPTR
  CHECK_EQ(f.focus, 34);
  CHECK_EQ(f.scroll, 3 * (22 + 10 * 34));

  // A section that does not exist falls back to the tab's first row, never a bogus index.
  f.focusSection(kTallSections, 4, 99);
  CHECK_EQ(f.focus, 1);
}

// -------------------------------------------------------------------------------------------- 4. frame
void test_focus_and_scroll() {
  printf("test  Up/Down skip sections, clamp, and the scroll keeps focus visible\n");
  CHECK_EQ(tm::settingsFirstFocusable(kGeneral, kGeneralCount), 1);
  CHECK_EQ(tm::settingsLastFocusable(kGeneral, kGeneralCount), 19);
  CHECK_EQ(tm::settingsElementCount(kGeneral, kGeneralCount), 20);

  // the section headers are 22 px and the rows 34: an index * constant would fail here
  CHECK_EQ(tm::settingsRowHeight(kNodeRows[0]), 34);
  CHECK_EQ(tm::settingsElementHeight(kGeneral, kGeneralCount, 0), 22);
  CHECK_EQ(tm::settingsElementHeight(kGeneral, kGeneralCount, 1), 34);
  CHECK_EQ(tm::settingsRowTop(kGeneral, kGeneralCount, 1), 22);
  CHECK_EQ(tm::settingsRowTop(kGeneral, kGeneralCount, 5), 22 + 34 * 3 + 22);

  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, 1, 1), 2);
  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, 3, 1), 5);   // skips ADVERT's header
  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, 5, -1), 3);
  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, 1, -1), 1);  // clamp at the first focusable
  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, 19, 1), 19); // clamp at the last
  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, 0, 1), 2);   // a header normalises to the first row
  CHECK_EQ(tm::settingsFocusMove(kGeneral, kGeneralCount, -3, -1), 1);

  const int body = 100;
  CHECK_EQ(tm::settingsScrollFor(kGeneral, kGeneralCount, 1, 0, body), 0);
  const int s = tm::settingsScrollFor(kGeneral, kGeneralCount, 19, 0, body);
  CHECK(s > 0);
  CHECK(tm::settingsRowTop(kGeneral, kGeneralCount, 19) - s + 34 <= body);
  CHECK(tm::settingsRowTop(kGeneral, kGeneralCount, 19) >= s);
  const int back = tm::settingsScrollFor(kGeneral, kGeneralCount, 1, s, body);
  CHECK_EQ(tm::settingsRowTop(kGeneral, kGeneralCount, 1) - back, 0);
  const int maxScroll = tm::settingsMaxScroll(kGeneral, kGeneralCount, body);
  const int stale = tm::settingsScrollFor(kGeneral, kGeneralCount, 19, 99999, body);
  CHECK(stale >= 0 && stale <= maxScroll);
  CHECK(tm::settingsRowTop(kGeneral, kGeneralCount, 19) - stale + 34 <= body);
  CHECK_EQ(tm::settingsScrollFor(kGeneral, kGeneralCount, 19, -5, body),
           tm::settingsScrollFor(kGeneral, kGeneralCount, 19, 0, body));
}

void test_tab_move() {
  printf("test  Left/Right clamped at both ends; labels come from the tabs array\n");
  CHECK_EQ(tm::settingsTabMove(0, -1, 4), 0);
  CHECK_EQ(tm::settingsTabMove(0, 1, 4), 1);
  CHECK_EQ(tm::settingsTabMove(3, 1, 4), 3);   // the last owner is the right-hand end
  CHECK_EQ(tm::settingsTabMove(3, -1, 4), 2);
  CHECK_EQ(tm::settingsTabMove(3, 0, 4), 3);
  CHECK_EQ(tm::settingsTabMove(-5, 1, 4), 1);  // clamped into the row first
  CHECK_EQ(tm::settingsTabMove(99, -1, 4), 2);
  CHECK_EQ(tm::settingsTabMove(0, 1, 0), 0);

  tm::Registry reg;
  addApps(reg);
  tm::SettingsTabDef tabs[tm::kSettingsMaxTabs];
  tm::settingsTabDefs(reg, tabs, tm::kSettingsMaxTabs);
  CHECK_STR(tm::settingsTabLabel(tabs, 4, 0), "General");
  CHECK_STR(tm::settingsTabLabel(tabs, 4, 1), "OM Lite");
  CHECK_STR(tm::settingsTabLabel(tabs, 4, 3), "Mesh-Torry");
}

void test_frame_keys() {
  printf("test  Left/Right tabs, Up/Down rows, Enter by row kind, NeedsRunning\n");
  tm::Core core;
  tm::SettingsFrame f;
  f.setBodyHeight(100);
  f.reset(0, 4);
  f.focusFirst(kGeneral, kGeneralCount);
  CHECK_EQ(f.focus, 1);

  // Left is clamped at General; Right changes the tab and asks the shell to rebuild its sections.
  tm::SettingsKeyResult r = tm::settingsKey(f, tm::SetKey::Left, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::None);
  CHECK_EQ(f.tab, 0);

  r = tm::settingsKey(f, tm::SetKey::Right, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::TabChanged);
  CHECK_EQ(r.value, 1);
  CHECK_EQ(f.tab, 1);
  CHECK_EQ(f.focus, -1);

  f.reset(3, 4);
  f.focusFirst(kGeneral, kGeneralCount);
  r = tm::settingsKey(f, tm::SetKey::Right, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::None);

  f.reset(0, 4);
  f.focusFirst(kGeneral, kGeneralCount);
  r = tm::settingsKey(f, tm::SetKey::Down, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::FocusMoved);
  CHECK_EQ(f.focus, 2);
  r = tm::settingsKey(f, tm::SetKey::Down, kGeneral, kGeneralCount);
  CHECK_EQ(f.focus, 3);
  r = tm::settingsKey(f, tm::SetKey::Down, kGeneral, kGeneralCount);
  CHECK_EQ(f.focus, 5);   // ADVERT section skipped

  // Enter on Info changes nothing.
  f.focus = 2;
  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::None);

  // Enter on Cycle and Action are told apart by the row kind. The FRAME only reports; the row's handler
  // is the shell's call, so nothing has run yet.
  f.focus = 5;
  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::CycleValue);
  CHECK_EQ(r.row, 5);
  f.focus = 3;
  f.setTabRunning(true);
  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::ActionRow);
  CHECK_EQ(r.row, 3);
  CHECK_EQ(g_actions, 0);

  // The same action while its owner is stopped: refused, and it says why instead of doing nothing.
  f.setTabRunning(false);
  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::NeedsRunning);
  CHECK_EQ(r.row, 3);
  CHECK_EQ(g_actions, 0);

  // A SubPage opens; Esc and the Settings key both close it to the ROOT (not out of Settings).
  f.setTabRunning(true);
  f.focus = 1;
  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::OpenSubPage);
  CHECK_EQ(f.depth, 1);
  r = tm::settingsKey(f, tm::SetKey::Esc, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::CloseSubPage);
  CHECK_EQ(f.depth, 0);
  CHECK_EQ(f.focus, 1);   // the row that opened it is focused again

  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::OpenSubPage);
  r = tm::settingsKey(f, tm::SetKey::Settings, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::CloseSubPage);
  CHECK_EQ(f.depth, 0);

  // Esc at the root is the only thing that leaves Settings.
  r = tm::settingsKey(f, tm::SetKey::Esc, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::LeaveSettings);

  // An empty tab (the OPS-TOC placeholder) has no focus and no key event that could point at a header.
  tm::SettingsFrame empty;
  empty.setBodyHeight(100);
  empty.reset(2, 4);
  empty.focusFirst(nullptr, 0);
  CHECK_EQ(empty.focus, -1);
  r = tm::settingsKey(empty, tm::SetKey::Enter, nullptr, 0);
  CHECK(r.event == tm::SettingsEvent::None);
}

void test_confirm_sheet() {
  printf("test  the shared sheet: Cancel changes nothing, Apply answers the row\n");
  tm::SettingsFrame f;
  f.setBodyHeight(100);
  f.reset(0, 4);
  f.focusFirst(kGeneral, kGeneralCount);

  char row_value[16];
  snprintf(row_value, sizeof(row_value), "%s", kNodeRows[0].value);   // the OLD value behind the sheet

  CHECK(f.beginConfirm(1));
  CHECK(f.confirmOpen());
  // arrows are swallowed while the sheet is up: no tab or focus movement
  const int tabBefore   = f.tab;
  const int focusBefore = f.focus;
  tm::SettingsKeyResult r = tm::settingsKey(f, tm::SetKey::Down, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::None);
  CHECK_EQ(f.tab, tabBefore);
  CHECK_EQ(f.focus, focusBefore);

  r = tm::settingsKey(f, tm::SetKey::Esc, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::ConfirmCancelled);
  CHECK(!f.confirmOpen());
  CHECK_STR(row_value, "TNMT (SI)");   // nothing touched the row
  CHECK_STR(kNodeRows[0].value, "TNMT (SI)");

  CHECK(f.beginConfirm(1));
  r = tm::settingsKey(f, tm::SetKey::Enter, kGeneral, kGeneralCount);
  CHECK(r.event == tm::SettingsEvent::ConfirmApplied);
  CHECK_EQ(r.row, 1);
  CHECK(!f.confirmOpen());
  // even Apply only TELLS the shell which row to change; the frame itself never rewrites the row
  CHECK_STR(kNodeRows[0].value, "TNMT (SI)");
}

// ------------------------------------------------------------------------------------------ 5. menu bar
void test_menu_settings_bar() {
  printf("test  Down from the cards lands on the Settings bar; Up returns\n");
  CHECK_EQ(tm::kMenuCards, 3);
  CHECK_EQ(tm::kMenuSettingsFocus, 3);

  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Down), 3);   // OM Lite -> Settings bar
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Down), 3);
  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Down), 3);
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Down), 3);   // clamped on the bar
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Up), 0);     // back to the first card
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Up), 0);     // clamped on the card row
  CHECK_EQ(tm::menuFocusMove(1, tm::MenuMove::Up), 1);
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Left), 3);   // the bar has no horizontal neighbour
  CHECK_EQ(tm::menuFocusMove(3, tm::MenuMove::Right), 3);
  CHECK_EQ(tm::menuFocusMove(2, tm::MenuMove::Right), 2);  // cards still clamp at the last
  CHECK_EQ(tm::menuFocusMove(0, tm::MenuMove::Left), 0);

  CHECK(tm::menuEnterActionAt(3, false, 3) == tm::MenuAction::OpenSettings);
  CHECK(tm::menuEnterActionAt(0, true, 3) == tm::MenuAction::Open);
  CHECK(tm::menuEnterActionAt(1, false, 3) == tm::MenuAction::ComingLater);
  CHECK(tm::menuEnterActionAt(-1, true, 3) == tm::MenuAction::None);
  CHECK(tm::menuEnterActionAt(9, true, 3) == tm::MenuAction::None);
}

}  // namespace

// S514/EST-446: a LIST sub-page (the channel / scope / add-by-key pages) uses the SAME row arithmetic and
// event vocabulary as a tab root, with its own focus/scroll, and Esc closes it.
void test_sub_page_keys() {
  printf("test  a list sub-page: rows, focus, Enter, Esc\n");
  static const tm::SettingRow rows[] = {
      {tm::SettingKind::Info, 1, "Name", "alpha", nullptr, nullptr},
      {tm::SettingKind::SubPage, 2, "Region / scope", "default", nullptr, nullptr},
      {tm::SettingKind::Action, 3, "Remove channel", "", nullptr, nullptr},
  };
  static const tm::SettingsSection sections[] = {{"CHANNEL", rows, 3}};

  tm::SettingsFrame f;
  f.reset(1, 4);
  f.openSubPage();
  int focus = -1, scroll = 0;

  // Down from nothing lands on the first row (the Info row IS focusable; only headers are skipped).
  tm::SettingsKeyResult r =
      tm::settingsSubPageKey(f, tm::SetKey::Down, sections, 1, &focus, &scroll, 356);
  CHECK(r.event == tm::SettingsEvent::FocusMoved);
  CHECK_EQ(focus, 2);   // an unset focus is normalised to the first row, then Down moves one
  r = tm::settingsSubPageKey(f, tm::SetKey::Down, sections, 1, &focus, &scroll, 356);
  CHECK_EQ(focus, 3);
  r = tm::settingsSubPageKey(f, tm::SetKey::Down, sections, 1, &focus, &scroll, 356);
  CHECK_EQ(focus, 3);
  CHECK_EQ(focus, 3);   // CLAMPED at the last row, no wrap

  // Up from the first row clamps there too.
  focus = 1;
  tm::settingsSubPageKey(f, tm::SetKey::Up, sections, 1, &focus, &scroll, 356);
  CHECK_EQ(focus, 1);

  // Enter reports the row's kind: SubPage opens, Action runs.
  focus = 2;
  r = tm::settingsSubPageKey(f, tm::SetKey::Enter, sections, 1, &focus, &scroll, 356);
  CHECK(r.event == tm::SettingsEvent::OpenSubPage);
  CHECK_EQ(r.row, 2);
  focus = 3;
  r = tm::settingsSubPageKey(f, tm::SetKey::Enter, sections, 1, &focus, &scroll, 356);
  CHECK(r.event == tm::SettingsEvent::ActionRow);

  // Info does nothing, and Left/Right do not move (a sub-page has no tabs).
  focus = 1;
  r = tm::settingsSubPageKey(f, tm::SetKey::Enter, sections, 1, &focus, &scroll, 356);
  CHECK(r.event == tm::SettingsEvent::None);
  r = tm::settingsSubPageKey(f, tm::SetKey::Right, sections, 1, &focus, &scroll, 356);
  CHECK(r.event == tm::SettingsEvent::None);
  CHECK_EQ(focus, 1);

  // Esc closes the sub-page and the frame is back at depth 0.
  r = tm::settingsSubPageKey(f, tm::SetKey::Esc, sections, 1, &focus, &scroll, 356);
  CHECK(r.event == tm::SettingsEvent::CloseSubPage);
  CHECK(!f.subPage());
}

// S514/EST-451: a LIST sub-page longer than its body. The key arithmetic must use the SAME window the
// renderer draws — from one pixel below the page head's line to the hint bar. The sound picker used to pass
// the TAB body + the tab row (386 px) while the renderer drew 385, so at the end of the list the focused row
// was scrolled one pixel past the bottom and then skipped (the gold frame vanished). This pins the height
// AND the one-row-at-a-time scroll at both ends.
void test_sub_page_list_scroll() {
  printf("test  a long sub-page list: Down from the last visible row scrolls one, stays visible; Up mirrors\n");

  // The real panel: 800x480, status 26, head 44, hint 24. drawSettingsRows starts a LIST sub-page at
  // head_bottom + 1 and gives it kTmH - kTmHintH - that = 385 px.
  constexpr int kPanelH = 480, kHintH = 24, kStatusH = 26, kHeadH = 44;
  constexpr int kDrawTop = kStatusH + kHeadH + 1;
  constexpr int kDrawH   = kPanelH - kHintH - kDrawTop;   // 385

  static const tm::SettingRow rows[] = {
      {tm::SettingKind::Action, 0, "s0", "", nullptr, nullptr},
      {tm::SettingKind::Action, 1, "s1", "", nullptr, nullptr},
      {tm::SettingKind::Action, 2, "s2", "", nullptr, nullptr},
      {tm::SettingKind::Action, 3, "s3", "", nullptr, nullptr},
      {tm::SettingKind::Action, 4, "s4", "", nullptr, nullptr},
      {tm::SettingKind::Action, 5, "s5", "", nullptr, nullptr},
      {tm::SettingKind::Action, 6, "s6", "", nullptr, nullptr},
      {tm::SettingKind::Action, 7, "s7", "", nullptr, nullptr},
      {tm::SettingKind::Action, 8, "s8", "", nullptr, nullptr},
      {tm::SettingKind::Action, 9, "s9", "", nullptr, nullptr},
      {tm::SettingKind::Action, 10, "s10", "", nullptr, nullptr},
      {tm::SettingKind::Action, 11, "s11", "", nullptr, nullptr},
      {tm::SettingKind::Action, 12, "s12", "", nullptr, nullptr},
      {tm::SettingKind::Action, 13, "s13", "", nullptr, nullptr},
      {tm::SettingKind::Action, 14, "s14", "", nullptr, nullptr},
      {tm::SettingKind::Action, 15, "s15", "", nullptr, nullptr},
      {tm::SettingKind::Action, 16, "s16", "", nullptr, nullptr},
      {tm::SettingKind::Action, 17, "s17", "", nullptr, nullptr},
      {tm::SettingKind::Action, 18, "s18", "", nullptr, nullptr},
      {tm::SettingKind::Action, 19, "s19", "", nullptr, nullptr},
      {tm::SettingKind::Action, 20, "s20", "", nullptr, nullptr},
      {tm::SettingKind::Action, 21, "s21", "", nullptr, nullptr},
      {tm::SettingKind::Action, 22, "s22", "", nullptr, nullptr},
      {tm::SettingKind::Action, 23, "off", "", nullptr, nullptr},
  };
  static const tm::SettingsSection sections[] = {{"SHAPES", rows, 23}, {"OFF", &rows[23], 1}};
  constexpr int kSectionCount = 2;

  // The key arithmetic and the renderer take this ONE height (the ARM changed it back to 386).
  const int body = tm::settingsListBodyHeight(kPanelH, kHintH, kStatusH, kHeadH);

  tm::SettingsFrame f;
  f.setBodyHeight(body);
  f.reset(0, 1);
  f.openSubPage();
  int focus = 10;   // flat 0 = SHAPES header; rows 1..23; flat 10 = s9, the last FULLY visible row
  int scroll = 0;
  CHECK(tm::settingsRowTop(sections, kSectionCount, focus) + 34 <= scroll + kDrawH);

  // One Down from the last visible row: the focus moves one row and the list scrolls just enough for that
  // row to land ON the last visible line (not below the drawn window).
  tm::SettingsKeyResult r =
      tm::settingsSubPageKey(f, tm::SetKey::Down, sections, kSectionCount, &focus, &scroll, body);
  CHECK(r.event == tm::SettingsEvent::FocusMoved);
  CHECK_EQ(focus, 11);
  CHECK(tm::settingsRowTop(sections, kSectionCount, focus) >= scroll);
  CHECK_EQ(tm::settingsRowTop(sections, kSectionCount, focus) - scroll + 34, kDrawH);

  // A second Down advances exactly one row height (34 px), like the Settings root rows.
  const int before = scroll;
  r = tm::settingsSubPageKey(f, tm::SetKey::Down, sections, kSectionCount, &focus, &scroll, body);
  CHECK_EQ(focus, 12);
  CHECK_EQ(scroll - before, 34);
  CHECK_EQ(tm::settingsRowTop(sections, kSectionCount, focus) - scroll + 34, kDrawH);

  // Up mirrors it: with the first fully visible row focused, Up lifts the list by one row and the new focus
  // becomes the window's first line. (Scroll 124 puts s3 at the top; focus 4 is s3.)
  focus = 4;
  scroll = tm::settingsRowTop(sections, kSectionCount, focus);
  r = tm::settingsSubPageKey(f, tm::SetKey::Up, sections, kSectionCount, &focus, &scroll, body);
  CHECK(r.event == tm::SettingsEvent::FocusMoved);
  CHECK_EQ(focus, 3);
  CHECK_EQ(tm::settingsRowTop(sections, kSectionCount, focus) - scroll, 0);

  // The last row is fully inside the window, and the scroll never exceeds the DRAWN max.
  focus = 25;   // flat 24 = OFF header, flat 25 = the Off row
  scroll = tm::settingsScrollFor(sections, kSectionCount, focus, 0, body);
  CHECK(scroll <= tm::settingsMaxScroll(sections, kSectionCount, kDrawH));
  CHECK(tm::settingsRowTop(sections, kSectionCount, focus) >= scroll);
  CHECK(tm::settingsRowTop(sections, kSectionCount, focus) + 34 <= scroll + kDrawH);

  // And the one height really is what the renderer draws.
  CHECK_EQ(tm::settingsListBodyTop(kStatusH, kHeadH), kDrawTop);
  CHECK_EQ(body, kDrawH);
}

int main() {
  test_name_chars();
  test_name_trim_and_length();
  test_name_draft_typing();
  test_advert_interval_mapping();
  test_advert_schedule_and_manual_path();
  test_tab_order_from_registry();
  test_module_content_and_placeholder();
  test_target_for_page();
  test_section_jump();
  test_focus_and_scroll();
  test_tab_move();
  test_frame_keys();
  test_confirm_sheet();
  test_menu_settings_bar();
  test_sub_page_keys();
  test_sub_page_list_scroll();

  printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
