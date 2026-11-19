// SPDX-License-Identifier: MIT
// RPTR part 1's device glue (S514/EST-453): the radio requests, the receive hooks and the remembered passwords.
//
// ONE owner: nothing here transmits. Every request is a complete frame handed to om::mc::queueRptrFrame(), and the
// radio task performs it exactly like a DM. The receive hooks are called from mc_link's radio task; the page reads
// copies under lock()/unlock().
//
// NVS namespace "mcrp": `rem` (Remember passwords, default On, the L2's behaviour) and up to four {
// name, password } blobs. The password is written only after a successful login and is NEVER logged, drawn or
// returned to the page: passwordFor() exists for the login path alone.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mc_wire.h"
#include "rptr_model.h"

namespace om {
namespace rptrlink {

using rptr::FoundList;
using rptr::LoginMachine;
using rptr::Status;

void begin();       // load the remembered passwords + the switch
void tick();        // scan window, login deadline, status deadline (main task loop)

bool scanStart();                                   // queue the zero-hop discover; false when nothing went out
bool loginStart(int found_index, const char* password);
void logout();
bool statusRequest();                               // refresh the open repeater's status

// Receive hooks, called by mc_link's radio task.
void onControl(const meshcore::View& pkt, int snr_db);
bool onResponse(const meshcore::View& pkt);         // true when it was ours
bool onPathReply(const meshcore::View& pkt);       // S514: a PATH from the login target (route + embedded RESPONSE)

// Snapshot accessors: copy under the lock, then draw from the copy. `status_now_ms` is the caller's millis.
void lock();
void unlock();
const FoundList& found();          // lock() must be held
const LoginMachine& login();       // lock() must be held
const Status& status();            // lock() must be held
uint32_t statusSerial();           // lock() must be held
bool statusPending();              // lock() must be held
const char* target();              // lock() must be held
bool haveTarget();                 // lock() must be held

// Remembered passwords.
bool rememberEnabled();
bool setRememberEnabled(bool on);
int  rememberedCount();
bool hasRemembered(const char* name);
bool passwordFor(const char* name, char* out, size_t cap);
bool forgetAllPasswords();

// ------------------------------------------------------------------- EST-470: the safe settings (part 2a)
// ONE line in flight: every request here goes through settingsSend, which refuses while another is pending, so
// repeater never sees two commands at once and a lost answer can never be mistaken for the next one's.
// `r` (settingsReadStart) runs the read ladder again, and a successful login runs it once by itself - every row
// shows a value the repeater itself answered, never a placeholder (a row not read yet draws "…").
//
// Accessors with "lock() must be held" are the same rule as the status ones above.
const rptr::Settings&    settings();        // lock() must be held
uint32_t                 settingsSerial();  // lock() must be held: bumps when a value changed
const rptr::CliPending&  cli();             // lock() must be held
bool                     cliBusy();         // lock() must be held: one line is still waiting for its answer
const char*              lastAnswer();      // lock() must be held: "" until the repeater has said something
bool                     lastAnswerBad();   // lock() must be held: the answer was an Error/ERR:/??: line
uint32_t                 answerSerial();    // lock() must be held: bumps on every answer and every "no answer"

bool settingsReadStart();                              // run the read ladder from the top
// Queue ONE line; false when one is already in flight. `second` is the follow-up line a Position change needs
// (`set lat` and `set lon` are two commands): it goes out only if the first one answers OK, so a refused half
// never leaves the repeater with a half-applied position.
bool settingsSend(const char* line, int row, bool is_read, const char* second = nullptr);
// The repeater's CLI answer, from the radio task. True only when it IS one (a TXT_MSG sealed with the login
// target's key whose type is CLI_DATA), so a PLAIN text from that same node falls through to the DM code and a
// CLI answer never lands in a DM thread, never raises the new-message pop-up and never lights the Chat dot.
bool onCliAnswer(const meshcore::View& pkt);

}  // namespace rptrlink
}  // namespace om
