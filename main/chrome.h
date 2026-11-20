// SPDX-License-Identifier: MIT
// The parts every page draws the same way: the status bar, the key-hint bar, the six page-key sigils, and the
// page-head row. They were inside launcher_page.cpp until the second and third page needed them (S502, step 4);
// the numbers are the approved ones, moved, not changed.
//
// One home matters here: the symbol key bar is how Filip knows which page he is on, so it must not be possible
// for the Chat page to draw a differently-spaced bar from the Launcher's.
#pragma once

#include <stdint.h>

#include "msg_popup.h"   // S515/EST-467: the pop-up's decisions (pure, host-tested)
#include "pages.h"
#include "screen.h"

namespace om {
namespace ui {

// One chip in the hint bar: the key as printed on it, and what it does here.
struct HintItem {
    const char* key;     // "up/dn", "Enter", "esc"
    const char* label;   // "move", "open", "back"
};

// Clock left; the two notification icons next to it (S516/EST-482); battery, wifi and gear sigils right.
// battery_pct < 0 hides the battery.
void drawStatusBar(Screen& s, const char* clock, int battery_pct, bool charging);

// Page-head band: title left, right-hand text right, and the line under it. Returns the y of the line.
int drawPageHead(Screen& s, const char* title, const char* right_text);

// The bottom bar: this page's actions on the left; on the LAUNCHER only, the six page keys on the right with
// `active` in gold. S516/EST-482: every other page carries its hints alone — the sigils without their words
// were no use there, and the freed width means no page drops a chip.
// S513/EST-435: it also draws the app's ONE notice, just above itself, when one is live — see noticeSet().
void drawHintBar(Screen& s, tm::Page active, const HintItem* hints, int hint_count);

// One key cap, the hint bar's own style (S514/EST-444): dark plate, 1 px outline, dim ink. The bar's chips and
// the Chat composer's Enter cap both call this, so "the same key-cap style" is one function, not two lookalikes.
void drawKeyCap(Screen& s, int x, int y, int w, int h, const char* text, uint16_t ink);

// The composer's Enter cap: the same cap with a drawn return arrow before the word (the generated font carries
// Latin-1 + Latin Extended-A only, so there is no U+21B5 glyph), centred as one group. `ink` is the whole cap's
// ink: kDimmer while the draft is empty, kInk once there is text.
void drawEnterKeyCap(Screen& s, int x, int y, int w, int h, uint16_t ink);

// --------------------------------------------------------------------------------- the one notice (S513/EST-435)
// Short-lived texts ("Advert already sent, wait 4 s", later "New node: …") appear on EVERY page, so they cannot
// belong to a page's view: they belong to the chrome. One slot for the whole app, the newest wins, and it is
// drawn by drawHintBar() above the hint bar — every page already calls that, so no page can forget it and no page
// needs a clock of its own.
//
// ⚠️ The WRITER may be the radio task (a notice decided while a packet is handled) and the READER is always the
// main task, so the slot is published under a critical section — the `time_source` pattern, not a shared buffer.
// Set from the task that decides; drawn only by the main task.
void noticeSet(const char* text, uint32_t ttl_ms);
void noticeClear();

// ------------------------------------------------------------------------ the new-message pop-up (S515/EST-467)
// The SAME one-slot pattern as the notice, and the SAME reader/writer split: the RADIO task publishes a
// message it just stored (msgPopupOffer), the MAIN task applies the rules (msgPopupPoll: screen off, the same
// chat already open, the 10 s timeout) and draws it. The decision logic is core/msg_popup.h, host-tested.
//
// The pop-up is drawn by drawHintBar(), so every page gets it without a line of its own, and while it is up
// it WINS over the one notice. `head` is already the finished first line ("#Test Channel · ERA-3", or
// "DM · ERA-3"); `text` is the message.
struct MsgPopupView {
    const char* head  = "";
    const char* text  = "";
    uint32_t    extra = 0;   // messages replaced while it was up ("+N more")
};

void msgPopupOffer(const char* head, const char* text, PopupChat chat, uint8_t channel,
                   const uint8_t peer[32]);

// Main task, once per loop pass. `chat_open` plus the OPEN chat's identity let the rule "not while that same
// chat is on screen" be a comparison here, not a page lookup. Returns true when the glass must be repainted.
bool msgPopupPoll(uint32_t now_ms, bool screen_off, bool chat_open, PopupChat chat, uint8_t channel,
                  const uint8_t peer[32]);

// Main task: a key was pressed. If a pop-up is up, dismiss it and return true — the caller SWALLOWS the key.
bool msgPopupKey();

// A copy of the live pop-up, taken under the lock; false when there is none.
bool msgPopupView(MsgPopupView* out);

// True once after the pop-up appeared, changed or went, so the page repaints with no key behind it.
bool msgPopupNeedsRepaint();

// The live text, or nullptr. Returns a copy taken under the lock, so the string cannot change while it is drawn.
const char* noticeText();

// S513 (Claude): does the screen need a repaint for the notice? True once after a notice was set or cleared, and
// once when a live notice runs out. The radio task sets "Advert sent" / "New node" with no key press behind it,
// so without this the strip only appeared if something else happened to repaint within its 3 s (Filip: "a sends
// an ADV, but still no pop-up"). Main task only; call it every loop pass.
bool noticeNeedsRepaint();

// The six page keys as vectors (BRIEF § 5; S516/EST-478): kind 0 X (F1), 1 triangle, 2 square, 3 circle,
// 4 cloud, 5 diamond.
void drawSymbolSigil(Screen& s, int kind, int cx, int cy, int r, uint16_t c, bool active);

// ------------------------------------------------------------------------ the volume pop-up (S516/EST-478)
// The volume KEYS are global: main.cpp decides the level and calls volumePopupPress() on every press (even
// at a clamp, so the plate re-arms), then volumePopupTick() once per loop pass. drawHintBar() draws the
// plate, so every page — and the Launcher — shows it without a line of its own. It is NOT modal: no key is
// taken from the page. It sits just above the hint bar, below the message pop-up's lowest edge, so the two
// fit together and the message pop-up's content is never hidden.
void volumePopupPress(uint32_t now_ms, uint8_t level, bool master_on);

// True once when the 1.5 s ran out, so the caller repaints the page under the plate.
bool volumePopupTick(uint32_t now_ms);

// ------------------------------------------------------------ the status bar's notification icons (S516/EST-482)
// Filip's ask moved the red hint-bar dots into the TOP bar: a chat icon while a channel or a DM has unread,
// and a node (pin) icon while a fresh node is still unacked. NO red dots are drawn anywhere.
//
// The CALLER supplies the data (main.cpp reads the store's own counters), exactly like the notice; drawStatusBar()
// draws the icons, and every page (including the Launcher) already calls it. The icons use FIXED slots just
// after the clock (x = 66 and 88, the widest clock ends at 50), so one appearing or going never moves the
// clock, the radio dot, the battery or the gear.
constexpr uint8_t kStatusIconChat = 1u << 0;
constexpr uint8_t kStatusIconNode = 1u << 1;

// Main task only: set it once per loop pass (the mask is published by the task that draws).
void    statusIconsSet(uint8_t mask);
uint8_t statusIcons();

// True once after the mask CHANGED — the noticeNeedsRepaint() pattern, so a message arriving while the Map is
// on screen makes the Chat icon appear with no key press behind it.
bool statusIconsNeedsRepaint();

// The rounded card style of S513/EST-430: the Chat list's cards and now the start menu's three app cards.
// `drawRoundedRect` is a per-row span fill with an integer corner (no float, no allocation); `drawCardPlate`
// draws the outline as the outer plate and the fill inside it, so the ring follows the corners exactly
// (thickness 1 = a resting card, 2 = the gold focus outline). One implementation, so the two card pages
// cannot drift apart.
void drawRoundedRect(Screen& s, int x, int y, int w, int h, int r, uint16_t c);
void drawCardPlate(Screen& s, int x, int y, int w, int h, int radius, int thickness, uint16_t outline,
                   uint16_t fill);

}  // namespace ui
}  // namespace om
