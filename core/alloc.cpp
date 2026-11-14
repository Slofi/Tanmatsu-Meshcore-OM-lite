// SPDX-License-Identifier: MIT
// The counted allocator — see alloc.h for the contract.
#include "alloc.h"

#include <stdlib.h>
#include <string.h>

namespace tm {

namespace {
void* host_alloc(size_t n) { return ::malloc(n); }
void host_free(void* p) { ::free(p); }
}  // namespace

Allocator::Allocator() : Allocator(host_alloc, host_free) {}

Allocator::Allocator(MemAlloc alloc_fn, MemFree free_fn)
    : alloc_fn_(alloc_fn ? alloc_fn : host_alloc),
      free_fn_(free_fn ? free_fn : host_free) {}

void* Allocator::alloc(const char* owner, size_t bytes) {
  if (!owner) return nullptr;  // an anonymous block could not be reported by name
  Block* b = static_cast<Block*>(alloc_fn_(sizeof(Block) + bytes));
  if (!b) return nullptr;
  b->next = live_;
  b->owner = owner;
  b->bytes = bytes;
  b->magic = kMagic;
  live_ = b;
  return static_cast<void*>(b + 1);
}

void Allocator::release(const char* owner, void* p) {
  if (!p) return;
  Block* target = static_cast<Block*>(p) - 1;
  if (target->magic != kMagic) return;  // not a block of ours (or already freed)
  if (owner && ::strcmp(owner, target->owner) != 0) return;  // not its block to free

  Block** pp = &live_;
  while (*pp) {
    if (*pp == target) {
      *pp = target->next;
      target->magic = 0;
      free_fn_(target);
      return;
    }
    pp = &(*pp)->next;
  }
}

Allocator::Stats Allocator::stats(const char* owner) const {
  Stats s;
  if (!owner) return s;
  for (const Block* b = live_; b; b = b->next)
    if (::strcmp(b->owner, owner) == 0) {
      s.bytes += b->bytes;
      s.blocks += 1;
    }
  return s;
}

const char* Allocator::leak(size_t i) const {
  size_t seen = 0;
  for (const Block* b = live_; b; b = b->next) {
    bool seen_before = false;
    for (const Block* q = live_; q != b; q = q->next)
      if (::strcmp(q->owner, b->owner) == 0) {
        seen_before = true;
        break;
      }
    if (seen_before) continue;
    if (seen == i) return b->owner;
    ++seen;
  }
  return nullptr;
}

size_t Allocator::leakCount() const {
  size_t n = 0;
  while (leak(n)) ++n;
  return n;
}

Allocator::Stats Allocator::total() const {
  Stats s;
  for (const Block* b = live_; b; b = b->next) {
    s.bytes += b->bytes;
    s.blocks += 1;
  }
  return s;
}

}  // namespace tm
