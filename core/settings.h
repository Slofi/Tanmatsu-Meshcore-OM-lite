// SPDX-License-Identifier: MIT
// Settings (EST-445, restructured per app in S514/EST-448): the FRAME, the tab registry and the General
// page's rules, as plain C++.
//
// The L2's Settings is still the model: tabs, a flat list of rows per page, a section header that is not
// selectable, a focused row, and the EST-422 confirmation sheet. What changed in EST-448 is WHO OWNS the
// content: the top tabs are now DERIVED from the module registry — General (the core) first, then the
// modules in the start menu's order — and each module supplies its own sections through
// Module::settings (core/settings_model.h). The frame draws whatever it is given.
//
// Everything here is pure: no Arduino, no LovyanGFX, no IDF, no NVS. The host tests
// (test/host/test_settings.cpp) drive the SAME functions the panel runs.
//
// The rules worth pinning on their own (a wrong one fails silently):
//   * the tab order comes from the registry, never from a second hard-coded table;
//   * a module with no settings data is one honest dim line, not an empty page;
//   * the `,` key from an OM Lite page lands on that page's SECTION in the OM Lite tab;
//   * the frame's key arithmetic: tabs clamped, focus clamped and never on a section header, the scroll
//     keeping the focused row fully visible, and Esc's two levels (sub-page -> root -> the page Settings
//     was opened from);
//   * General's node name and advert interval rules (unchanged from EST-445).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pages.h"
#include "registry.h"
#include "settings_model.h"

namespace tm {

// --------------------------------------------------------------------------------------------- tabs
// General plus EVERY registry row: the registry's own capacity, not a smaller number that would silently
// drop the last app's tab. (S514/EST-448 self-sweep: 8 here vs 8 modules + General = 9 was one tab short.)
constexpr int kSettingsMaxTabs = (int)Registry::kMaxModules + 1;
constexpr int kSettingsGeneralTab = 0;

// One top tab. `module == nullptr` is the core's General tab; every other tab is exactly one registry
// row, so its label, id and LATER state cannot drift from the start menu.
struct SettingsTabDef {
  const char* id = "";
  const char* label = "";
  SettingsHook settings = nullptr;   // nullptr = no data (one honest line, unless this is General)
  bool later = false;                // available == false in the registry
  const Module* module = nullptr;    // nullptr = the core's General
};

// Fills `out` with General first, then every module in registry order (the start menu's own order) and
// returns how many were written. `count` is clamped to `cap`; the caller sizes the array with
// kSettingsMaxTabs so nothing is lost in the app.
inline int settingsTabDefs(const Registry& reg, SettingsTabDef* out, int cap) {
  if (out == nullptr || cap <= 0) return 0;
  int n = 0;
  out[n++] = SettingsTabDef{"general", "General", nullptr, false, nullptr};
  for (size_t i = 0; i < reg.count() && n < cap; ++i) {
    const Module* m = reg.at(i);
    if (m == nullptr) continue;
    out[n++] = SettingsTabDef{m->id, m->name, m->settings, !m->available, m};
  }
  return n;
}

inline int settingsTabIndex(int tab, int tab_count) {
  if (tab_count <= 0) return 0;
  if (tab < 0) return 0;
  if (tab >= tab_count) return tab_count - 1;
  return tab;
}

inline const char* settingsTabLabel(const SettingsTabDef* tabs, int tab_count, int index) {
  if (tabs == nullptr || tab_count <= 0) return "General";
  return tabs[settingsTabIndex(index, tab_count)].label;
}

// Left/Right: one tab, CLAMPED at both ends (no wrap). With arrows a wrap would make "Right" from the
// last app jump to General, which is a surprise on a settings page.
constexpr int settingsTabMove(int tab, int dir, int tab_count) {
  if (tab_count <= 0) return 0;
  if (tab < 0) tab = 0;
  if (tab >= tab_count) tab = tab_count - 1;
  if (dir < 0) {
    if (tab > 0) --tab;
  } else if (dir > 0) {
    if (tab < tab_count - 1) ++tab;
  }
  return tab;
}

// The module hook, called through one helper so a null provider is handled in exactly one place.
inline const SettingsSection* settingsTabContent(const SettingsTabDef& tab, Core& core, int* count) {
  if (count != nullptr) *count = 0;
  if (tab.settings == nullptr) return nullptr;
  return tab.settings(core, count);
}

// Which SECTION of the OM Lite tab a page's `,` key lands on. The order is the tab's own (NODES, CHAT,
// MAP, RPTR); EST-446/447 add rows INSIDE these sections, never new tabs.
constexpr int settingsSectionForPage(Page p) {
  switch (p) {
    case Page::Nodes: return 0;
    case Page::Chat:  return 1;
    case Page::Map:   return 2;
    case Page::Rptr:  return 3;
    default:          return -1;
  }
}

struct SettingsTarget {
  int tab = kSettingsGeneralTab;
  int section = -1;   // -1 = just the tab (focus its first row)
};

// The `,` key's destination. An OM Lite page maps to the tab owned by the module that draws it and to
// that page's section; the menu and anything else map to General. The tab index follows the registry,
// so reordering the start menu reorders these tabs without a second edit.
inline SettingsTarget settingsTargetForPage(const Registry& reg, Page p) {
  SettingsTarget t;
  if (p == Page::Nodes || p == Page::Chat || p == Page::Map || p == Page::Rptr) {
    const Module* m = reg.moduleForPage(p);
    if (m != nullptr) {
      const size_t i = reg.indexOf(m->id);
      if (i != Registry::kNone) {
        t.tab     = 1 + (int)i;
        t.section = settingsSectionForPage(p);
        return t;
      }
    }
  }
  return t;   // General
}

// --------------------------------------------------------------------------------------------- rows
// The panel's numbers (mirrored by tools/preview_tanmatsu.py): a body row 34 px, a section header 22.
constexpr int kSettingsRowH     = 34;
constexpr int kSettingsSectionH = 22;

constexpr int settingsRowHeight(const SettingRow&) { return kSettingsRowH; }

// One flat element index addresses a section header and every row (the focus and scroll arithmetic only
// needs one number). `header` is the one thing that may never be focused.
struct SettingsElement {
  int section = -1;
  int row = -1;
  bool header = false;
};

constexpr SettingsElement settingsElementAt(const SettingsSection* secs, int count, int index) {
  if (secs == nullptr || index < 0) return SettingsElement{};
  int k = 0;
  for (int i = 0; i < count; ++i) {
    if (k == index) return SettingsElement{i, -1, true};
    ++k;
    const int n = secs[i].rows != nullptr && secs[i].row_count > 0 ? secs[i].row_count : 0;
    for (int j = 0; j < n; ++j, ++k)
      if (k == index) return SettingsElement{i, j, false};
  }
  return SettingsElement{};
}

constexpr int settingsElementCount(const SettingsSection* secs, int count) {
  if (secs == nullptr || count <= 0) return 0;
  int n = 0;
  for (int i = 0; i < count; ++i)
    n += 1 + (secs[i].rows != nullptr && secs[i].row_count > 0 ? secs[i].row_count : 0);
  return n;
}

constexpr const SettingRow* settingsRowAt(const SettingsSection* secs, int count, int index) {
  const SettingsElement e = settingsElementAt(secs, count, index);
  if (e.section < 0 || e.header) return nullptr;
  const SettingsSection& s = secs[e.section];
  if (s.rows == nullptr || e.row < 0 || e.row >= s.row_count) return nullptr;
  return &s.rows[e.row];
}

constexpr int settingsElementHeight(const SettingsSection* secs, int count, int index) {
  const SettingsElement e = settingsElementAt(secs, count, index);
  if (e.section < 0) return 0;
  return e.header ? kSettingsSectionH : kSettingsRowH;
}

constexpr int settingsRowTop(const SettingsSection* secs, int count, int index) {
  int y = 0;
  for (int i = 0; i < index; ++i) y += settingsElementHeight(secs, count, i);
  return y;
}

constexpr int settingsContentHeight(const SettingsSection* secs, int count) {
  return settingsRowTop(secs, count, settingsElementCount(secs, count));
}

constexpr int settingsMaxScroll(const SettingsSection* secs, int count, int body_h) {
  const int total = settingsContentHeight(secs, count);
  return total > body_h ? total - body_h : 0;
}

// A LIST sub-page (the sound picker, channels, contacts) draws from one pixel below the page head's line
// to the hint bar. Its key arithmetic must use the SAME window: EST-451's picker passed the TAB body height
// plus the tab row (386 px) while the renderer drew 385 px, so the focused row could be scrolled one pixel
// past the bottom and was then skipped as "partly on the chrome" — the gold frame vanished at the end of
// the list. These two functions are that one window; the shell uses them for both drawing and keys.
constexpr int settingsListBodyTop(int head_top, int head_h) { return head_top + head_h + 1; }

constexpr int settingsListBodyHeight(int panel_h, int hint_h, int head_top, int head_h) {
  return panel_h - hint_h - settingsListBodyTop(head_top, head_h);
}

// First / last focusable element, or -1 when there are no rows. Section headers are skipped, so a tab
// with only sections can never leave Enter pointing at a header.
constexpr int settingsFirstFocusable(const SettingsSection* secs, int count) {
  const int n = settingsElementCount(secs, count);
  for (int i = 0; i < n; ++i)
    if (!settingsElementAt(secs, count, i).header) return i;
  return -1;
}

constexpr int settingsLastFocusable(const SettingsSection* secs, int count) {
  const int n = settingsElementCount(secs, count);
  for (int i = n - 1; i >= 0; --i)
    if (!settingsElementAt(secs, count, i).header) return i;
  return -1;
}

// Up/Down: one focusable element in `dir`, skipping headers and CLAMPING at both ends. A focus that
// starts outside the page is normalised first, so the first key press is never spent on a bogus index.
constexpr int settingsFocusMove(const SettingsSection* secs, int count, int focus, int dir) {
  const int n = settingsElementCount(secs, count);
  if (n <= 0) return -1;
  const int first = settingsFirstFocusable(secs, count);
  if (first < 0) return -1;
  if (focus < 0 || focus >= n || settingsElementAt(secs, count, focus).header) focus = first;
  if (dir > 0) {
    for (int i = focus + 1; i < n; ++i)
      if (!settingsElementAt(secs, count, i).header) return i;
    return settingsLastFocusable(secs, count);
  }
  if (dir < 0) {
    for (int i = focus - 1; i >= 0; --i)
      if (!settingsElementAt(secs, count, i).header) return i;
    return settingsFirstFocusable(secs, count);
  }
  return focus;
}

// The scroll that keeps the focused element FULLY visible, moving as little as possible. The L2's rule
// is "the focused row is always on screen"; a row exactly one pixel over the floor lifts the page by one.
constexpr int settingsScrollFor(const SettingsSection* secs, int count, int focus, int scroll,
                                int body_h) {
  if (secs == nullptr || count <= 0 || body_h <= 0) return 0;
  const int maxScroll = settingsMaxScroll(secs, count, body_h);
  if (scroll < 0) scroll = 0;
  if (scroll > maxScroll) scroll = maxScroll;
  if (focus < 0 || focus >= settingsElementCount(secs, count)) return scroll;
  const int top    = settingsRowTop(secs, count, focus);
  const int bottom = top + settingsElementHeight(secs, count, focus);
  if (top < scroll) scroll = top;
  if (bottom > scroll + body_h) scroll = bottom - body_h;
  if (scroll < 0) scroll = 0;
  if (scroll > maxScroll) scroll = maxScroll;
  return scroll;
}

// The flat index of a section's header, or -1 when the section does not exist.
constexpr int settingsSectionElement(const SettingsSection* secs, int count, int section) {
  if (secs == nullptr || section < 0 || section >= count) return -1;
  int k = 0;
  for (int i = 0; i < section; ++i)
    k += 1 + (secs[i].rows != nullptr && secs[i].row_count > 0 ? secs[i].row_count : 0);
  return k;
}

constexpr int settingsFirstFocusableInSection(const SettingsSection* secs, int count, int section) {
  const int header = settingsSectionElement(secs, count, section);
  if (header < 0) return -1;
  const int rows = secs[section].rows != nullptr && secs[section].row_count > 0
                       ? secs[section].row_count
                       : 0;
  return rows > 0 ? header + 1 : -1;
}

// `,` from an OM Lite page: put the SECTION's header at the top of the body where possible, and focus its
// first row. Clamped like every other scroll, so a short tab simply shows everything from the top.
constexpr int settingsScrollToSection(const SettingsSection* secs, int count, int section,
                                      int body_h) {
  const int header = settingsSectionElement(secs, count, section);
  if (header < 0) return 0;
  const int maxScroll = settingsMaxScroll(secs, count, body_h);
  int scroll = settingsRowTop(secs, count, header);
  if (scroll > maxScroll) scroll = maxScroll;
  return scroll < 0 ? 0 : scroll;
}

// --------------------------------------------------------------------------------------------- keys
// The frame's own key set. `Settings` is the comma: it OPENS Settings from a page, and inside a sub-page
// it goes back to the Settings root (the router's "own key = page top" rule applied to the one key that
// reaches this page). The shell maps tm::Key + the keyboard's ASCII to this enum.
enum class SetKey : uint8_t { None = 0, Up, Down, Left, Right, Enter, Esc, Settings };

enum class SettingsEvent : uint8_t {
  None = 0,
  TabChanged,       // `value` is the new tab index; the shell rebuilds its sections and re-clamps focus
  FocusMoved,       // the frame's focus/scroll moved; nothing else changed
  OpenSubPage,      // Enter on a SubPage row: depth is now 1
  ToggleValue,      // Enter on a Toggle row: the shell runs the row's handler
  CycleValue,       // Enter on a Cycle row: the shell runs the row's handler
  ActionRow,        // Enter on an Action row: the shell runs the row's handler
  NeedsRunning,     // the row's owner is stopped and this action needs it: say so, change nothing
  CloseSubPage,     // Esc or Settings inside a sub-page: back to the root, focus/scroll restored
  LeaveSettings,    // Esc at the root: the shell routes to the page Settings was opened from
  ConfirmCancelled, // Esc on the shared sheet: nothing changes
  ConfirmApplied,   // Enter on the shared sheet: `row` is the row the shell must now apply
};

struct SettingsFrame {
  int  tab = kSettingsGeneralTab;
  int  tab_count = 1;
  int  focus = -1;      // flat element index into the current tab's sections
  int  scroll = 0;      // px
  int  depth = 0;       // 0 = a tab root; 1 = a sub-page (Name today)
  int  confirm_row = -1;   // >= 0 while the ONE shared sheet is open
  bool tab_running = true; // false = the tab's module is stopped; action rows with needs_running refuse

  bool confirmOpen() const { return confirm_row >= 0; }
  bool subPage() const { return depth > 0; }

  // Start on a tab. The shell calls focusFirst()/focusSection() after it has built that tab's sections.
  void reset(int tab_index, int tabs) {
    tab_count = tabs > 0 ? tabs : 1;
    tab = settingsTabIndex(tab_index, tab_count);
    focus = -1;
    scroll = 0;
    depth = 0;
    confirm_row = -1;
    tab_running = true;
    root_focus_ = -1;
    root_scroll_ = 0;
  }

  void setTabRunning(bool running) { tab_running = running; }

  void focusFirst(const SettingsSection* secs, int count) {
    focus = settingsFirstFocusable(secs, count);
    scroll = settingsScrollFor(secs, count, focus, scroll, body_h_);
  }

  // The `,`-from-a-page destination: the section's first row and the section header at the top.
  void focusSection(const SettingsSection* secs, int count, int section) {
    const int first = settingsFirstFocusableInSection(secs, count, section);
    focus = first >= 0 ? first : settingsFirstFocusable(secs, count);
    scroll = settingsScrollToSection(secs, count, section, body_h_);
    if (focus >= 0) scroll = settingsScrollFor(secs, count, focus, scroll, body_h_);
  }

  // The shell tells the frame the body height it draws into, so the scroll it returns matches the panel.
  void setBodyHeight(int h) { body_h_ = h > 0 ? h : 0; }
  int  bodyHeight() const { return body_h_; }

  void openSubPage() {
    root_focus_ = focus;
    root_scroll_ = scroll;
    depth = 1;
  }

  void closeSubPage() {
    depth = 0;
    focus = root_focus_;
    scroll = root_scroll_;
  }

  // The ONE shared confirmation sheet (EST-422). The rows are NOT touched here: the shell keeps showing
  // the old value until applyConfirm() returns the row and it performs the change. Cancel therefore
  // cannot change anything, even by accident.
  bool beginConfirm(int row) {
    if (row < 0) return false;
    confirm_row = row;
    return true;
  }

  void cancelConfirm() { confirm_row = -1; }

  int applyConfirm() {
    const int row = confirm_row;
    confirm_row = -1;
    return row;
  }

 private:
  int body_h_ = 0;
  int root_focus_ = -1;
  int root_scroll_ = 0;
};

struct SettingsKeyResult {
  SettingsEvent event = SettingsEvent::None;
  int row = -1;
  int value = 0;
};

// One key. `secs`/`count` are the CURRENT tab's sections (the root's when depth == 0). The shell owns
// the sub-page's text field and its OK key, so inside a sub-page only Esc and the Settings key are
// decided here; every other key returns None.
inline SettingsKeyResult settingsKey(SettingsFrame& f, SetKey k, const SettingsSection* secs,
                                     int count) {
  SettingsKeyResult r;

  // The sheet owns every key while it is up. Enter = Apply, Esc = Cancel; the arrows do nothing, so a
  // held arrow cannot move the page behind the question.
  if (f.confirmOpen()) {
    if (k == SetKey::Enter) {
      r.row = f.applyConfirm();
      r.event = SettingsEvent::ConfirmApplied;
    } else if (k == SetKey::Esc) {
      f.cancelConfirm();
      r.event = SettingsEvent::ConfirmCancelled;
    }
    return r;
  }

  if (f.depth > 0) {
    if (k == SetKey::Esc || k == SetKey::Settings) {
      f.closeSubPage();
      r.event = SettingsEvent::CloseSubPage;
    }
    return r;
  }

  switch (k) {
    case SetKey::Left:
    case SetKey::Right: {
      const int next = settingsTabMove(f.tab, k == SetKey::Right ? 1 : -1, f.tab_count);
      if (next == f.tab) return r;
      f.tab = next;
      f.focus = -1;          // the shell clamps after it builds the new tab's sections
      f.scroll = 0;
      r.event = SettingsEvent::TabChanged;
      r.value = next;
      return r;
    }
    case SetKey::Up:
    case SetKey::Down: {
      if (f.focus < 0) f.focus = settingsFirstFocusable(secs, count);
      const int next = settingsFocusMove(secs, count, f.focus, k == SetKey::Down ? 1 : -1);
      f.focus = next;
      f.scroll = settingsScrollFor(secs, count, f.focus, f.scroll, f.bodyHeight());
      r.row = next;
      r.event = SettingsEvent::FocusMoved;
      return r;
    }
    case SetKey::Enter: {
      const SettingRow* row = settingsRowAt(secs, count, f.focus);
      if (row == nullptr) return r;
      switch (row->kind) {
        case SettingKind::SubPage:
          f.openSubPage();
          r.event = SettingsEvent::OpenSubPage;
          r.row = f.focus;
          return r;
        case SettingKind::Toggle:
        case SettingKind::Cycle:
        case SettingKind::Action:
          if (row->needs_running && !f.tab_running) {
            r.event = SettingsEvent::NeedsRunning;
            r.row = f.focus;
            return r;
          }
          r.event = row->kind == SettingKind::Toggle ? SettingsEvent::ToggleValue
                    : row->kind == SettingKind::Cycle ? SettingsEvent::CycleValue
                                                      : SettingsEvent::ActionRow;
          r.row = f.focus;
          return r;
        default:
          return r;   // Info: nothing to do
      }
    }
    case SetKey::Esc:
      r.event = SettingsEvent::LeaveSettings;
      return r;
    case SetKey::Settings:
      // At the root the Settings key is already where it would go: nothing changes.
      return r;
    default:
      return r;
  }
}

// A LIST sub-page (S514/EST-446: the channel page, its scope choice, add-by-key). Not a new frame: the
// same row arithmetic, its own focus/scroll, and the same event vocabulary. The shell owns the sections
// (they change with the live table) and the text field a SubPage row inside it opens.
//
// Esc/Settings closes the sub-page; Up/Down move its focus (clamped, headers skipped); Enter reports the
// row exactly as the root does. Left/Right do nothing here: a sub-page has no tabs.
inline SettingsKeyResult settingsSubPageKey(SettingsFrame& f, SetKey k, const SettingsSection* secs,
                                            int count, int* focus, int* scroll, int body_h) {
  SettingsKeyResult r;
  if (focus == nullptr || scroll == nullptr) return r;

  if (k == SetKey::Esc || k == SetKey::Settings) {
    f.closeSubPage();
    r.event = SettingsEvent::CloseSubPage;
    return r;
  }
  switch (k) {
    case SetKey::Up:
    case SetKey::Down: {
      if (*focus < 0) *focus = settingsFirstFocusable(secs, count);
      *focus = settingsFocusMove(secs, count, *focus, k == SetKey::Down ? 1 : -1);
      *scroll = settingsScrollFor(secs, count, *focus, *scroll, body_h);
      r.row = *focus;
      r.event = SettingsEvent::FocusMoved;
      return r;
    }
    case SetKey::Enter: {
      const SettingRow* row = settingsRowAt(secs, count, *focus);
      if (row == nullptr) return r;
      switch (row->kind) {
        case SettingKind::SubPage:
        case SettingKind::Toggle:
        case SettingKind::Cycle:
        case SettingKind::Action:
          if (row->needs_running && !f.tab_running) {
            r.event = SettingsEvent::NeedsRunning;
          } else {
            r.event = row->kind == SettingKind::SubPage ? SettingsEvent::OpenSubPage
                      : row->kind == SettingKind::Toggle ? SettingsEvent::ToggleValue
                      : row->kind == SettingKind::Cycle  ? SettingsEvent::CycleValue
                                                         : SettingsEvent::ActionRow;
          }
          r.row = *focus;
          return r;
        default:
          return r;   // Info
      }
    }
    default:
      return r;
  }
}

// -------------------------------------------------------------------------------------------- naming
// MeshCore's node-name rule, read from the L2 (ui/chat_layout.h::keyboardAllowsChar, NodeName): the name
// is 1..31 bytes of printable ASCII except [ ] \ : , ? * (AdvertDataParser::isValidName). The UI additionally
// trims the leading/trailing spaces the editor's space key can leave behind.
constexpr size_t kNodeNameMaxBytes = 31;
constexpr const char* kDefaultNodeName = "TNMT (SI)";   // the one default when nothing is stored in mcid/name

constexpr bool nodeNameCharAllowed(char c) {
  const unsigned char u = (unsigned char)c;
  if (u < 0x20 || u > 0x7E) return false;   // printable ASCII only
  switch (u) {
    case '[': case ']': case '\\': case ':': case ',': case '?': case '*': return false;
    default: return true;
  }
}

// Writes the trimmed name to `out` (always NUL-terminated when cap > 0) and returns its byte length.
// 0 means empty after trimming. This does NOT validate; nodeNameApply is the rule callers use.
inline size_t nodeNameTrim(const char* in, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (in == nullptr) return 0;
  size_t first = 0;
  while (in[first] == ' ') ++first;
  size_t last = first;
  while (in[last] != '\0') ++last;
  while (last > first && in[last - 1] == ' ') --last;
  size_t n = last - first;
  if (n >= cap) n = cap - 1;   // the validation will refuse an over-long name before it is stored
  for (size_t i = 0; i < n; ++i) out[i] = in[first + i];
  out[n] = '\0';
  return n;
}

// True only for a name that can be stored AS IS: 1..31 bytes, every byte allowed, and no leading/trailing
// space. Values typed in the editor go through nodeNameApply first.
inline bool nodeNameValid(const char* s) {
  if (s == nullptr) return false;
  size_t n = 0;
  while (s[n] != '\0') {
    if (!nodeNameCharAllowed(s[n])) return false;
    ++n;
  }
  if (n == 0 || n > kNodeNameMaxBytes) return false;
  if (s[0] == ' ' || s[n - 1] == ' ') return false;
  return true;
}

// Trim, then validate. On success `out` holds the name to store and the answer is true; on failure `out`
// is left untouched (a rejected draft must not overwrite the confirmed name).
inline bool nodeNameApply(const char* in, char* out, size_t cap) {
  if (in == nullptr) return false;
  // Measure the TRIMMED input before any buffer truncation: a 32-byte name must be REFUSED, not silently
  // shortened to 31 (the editor's cap normally prevents it; a restored/corrupt value cannot).
  size_t first = 0;
  while (in[first] == ' ') ++first;
  size_t last = first;
  while (in[last] != '\0') ++last;
  while (last > first && in[last - 1] == ' ') --last;
  const size_t raw = last - first;
  if (raw == 0 || raw > kNodeNameMaxBytes) return false;
  char trimmed[kNodeNameMaxBytes + 1];
  const size_t t = nodeNameTrim(in, trimmed, sizeof(trimmed));
  if (t == 0 || t > kNodeNameMaxBytes) return false;
  if (!nodeNameValid(trimmed)) return false;
  if (out != nullptr && cap > 0) {
    const size_t copy = t < cap - 1 ? t : cap - 1;
    for (size_t i = 0; i < copy; ++i) out[i] = trimmed[i];
    out[copy] = '\0';
  }
  return true;
}

// The draft is what the shared text field holds: 1..31 bytes of allowed characters, spaces included. The
// field refuses a character at the moment it is typed (like the composer), so the rule is one function.
inline bool nodeNameDraftPut(char c, size_t used, bool* ok) {
  if (ok != nullptr) *ok = false;
  if (!nodeNameCharAllowed(c)) return false;
  if (used >= kNodeNameMaxBytes) return false;
  if (ok != nullptr) *ok = true;
  return true;
}

// ---------------------------------------------------------------------------------- automatic advert
// General's "Automatic advert" row. Off is a real choice: serviceRequests() then schedules NO advert at
// all (today's 3-hour default is the default, not a hard-coded timer). The manual paths ('a' on the
// menu/Nodes, and "Advert now" on this page) do not read this value; they still obey the 5 s gate.
enum class AdvertInterval : uint8_t { Off = 0, H1, H3, H6, H12, Count };
constexpr int kAdvertIntervalCount = (int)AdvertInterval::Count;

constexpr const char* advertIntervalLabel(AdvertInterval v) {
  switch (v) {
    case AdvertInterval::Off: return "Off";
    case AdvertInterval::H1:  return "1 h";
    case AdvertInterval::H3:  return "3 h";
    case AdvertInterval::H6:  return "6 h";
    case AdvertInterval::H12: return "12 h";
    default:                  return "Off";
  }
}

constexpr uint32_t advertIntervalSeconds(AdvertInterval v) {
  switch (v) {
    case AdvertInterval::Off: return 0;
    case AdvertInterval::H1:  return 1u * 60u * 60u;
    case AdvertInterval::H3:  return 3u * 60u * 60u;
    case AdvertInterval::H6:  return 6u * 60u * 60u;
    case AdvertInterval::H12: return 12u * 60u * 60u;
    default:                  return 0;
  }
}

// S514 (Filip): "set the off as default for adverts". Without a stored choice there is no scheduled advert; the node
// is announced by the 'a' key or Settings > Advert now, or by choosing an interval in Settings (stored, wins).
constexpr AdvertInterval advertIntervalDefault() { return AdvertInterval::Off; }

// The stored value is seconds (NVS mcid/advint). Any value that is not one of the five choices is treated
// as the DEFAULT, which is Off since S514 (Filip): a corrupt value behaves like no choice at all.
constexpr AdvertInterval advertIntervalFromSeconds(uint32_t s) {
  switch (s) {
    case 0:      return AdvertInterval::Off;
    case 3600:   return AdvertInterval::H1;
    case 10800:  return AdvertInterval::H3;
    case 21600:  return AdvertInterval::H6;
    case 43200:  return AdvertInterval::H12;
    default:     return advertIntervalDefault();
  }
}

constexpr uint32_t kAdvertIntervalCorrupt = 0xFFFFFFFFu;
constexpr bool advertIntervalStoredValid(uint32_t s) {
  return s == 0 || s == 3600 || s == 10800 || s == 21600 || s == 43200;
}

// Enter: the next choice, wrapping 12 h -> Off -> 1 h.
constexpr AdvertInterval advertIntervalCycle(AdvertInterval v) {
  const int n = (int)v;
  if (n < 0 || n >= kAdvertIntervalCount) return AdvertInterval::Off;
  return (AdvertInterval)((n + 1) % kAdvertIntervalCount);
}

// May the SCHEDULE queue an advert now? Off (interval 0) is never due. S514 (Filip: "disable the auto-advert on
// start-up"): there is NO advert at boot any more; `elapsed_s` is counted from the last scheduled advert, or from
// boot when there has been none, so the first one goes out one full interval after the app starts. `ever` is kept
// for the callers' readability and no longer changes the answer. The 'a' key and Settings > Advert now are not
// the schedule and still send at once. The 5 s gate is core/advert_rule.h and still applies to both paths.
constexpr bool advertScheduleDue(uint32_t interval_s, uint32_t elapsed_s, bool ever) {
  (void)ever;
  if (interval_s == 0) return false;
  return elapsed_s >= interval_s;
}

// The image's version, shown in General > DEVICE. One constant: the next ticket bumps this line.
constexpr const char* kAppVersion = "60";

}  // namespace tm
