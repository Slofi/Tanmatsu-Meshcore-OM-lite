// SPDX-License-Identifier: MIT
// The Tanmatsu app core, part 2: the counted allocator ("Stop frees memory", made a number).
//
// A module NEVER calls malloc/new: it allocates through core.alloc(). Every block is counted by its
// owner's name, so the core can answer the three questions the Launcher and the host test ask:
//   1. how many bytes and blocks does this module hold right now?
//   2. after stop(), did it return to zero — and if not, WHICH module did not?
//   3. how much does the whole app hold?
//
// The heap source is a function pair, so the device plugs the PSRAM arena in and the host test uses
// malloc/free. No exceptions, no RTTI, no STL containers: the same file compiles for ESP-IDF and g++.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tm {

class Allocator {
 public:
  using MemAlloc = void* (*)(size_t);
  using MemFree = void (*)(void*);

  struct Stats {
    size_t bytes = 0;
    size_t blocks = 0;
  };

  Allocator();
  Allocator(MemAlloc alloc_fn, MemFree free_fn);

  // `owner` is the module id ("om-lite"); the core itself uses kOwnerCore.
  // Returns nullptr on failure or on a null owner — a module cannot allocate anonymously.
  void* alloc(const char* owner, size_t bytes);

  // Frees a block taken by `owner`. A pointer that is not one of ours, or belongs to another owner,
  // is ignored (its block stays live, so a wrong-owner free shows up as a leak instead of silently
  // corrupting the count).
  void release(const char* owner, void* p);

  Stats stats(const char* owner) const;

  // The i-th distinct owner still holding blocks, in allocation order; nullptr past the end.
  // Together with stats() this is the leak report: name first, numbers second.
  const char* leak(size_t i) const;
  size_t leakCount() const;

  Stats total() const;

  static constexpr const char* kOwnerCore = "core";

 private:
  struct Block {
    Block* next;
    const char* owner;
    size_t bytes;
    uint32_t magic;
  };
  static constexpr uint32_t kMagic = 0x544d424c;  // "TMBL"

  MemAlloc alloc_fn_;
  MemFree free_fn_;
  Block* live_ = nullptr;
};

}  // namespace tm
