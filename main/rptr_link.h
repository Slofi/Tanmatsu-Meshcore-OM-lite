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

}  // namespace rptrlink
}  // namespace om
