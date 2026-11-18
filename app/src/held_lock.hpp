// A lock the draw section takes that another thread can hold as the section starts (MM3D's GSP
// interrupt lock, decomp/mm3d docs/frame.md "The GSP interrupt lock"). No other thread runs in a
// replay, so the replay starts with it as its holder leaves it: free.
#pragma once

#include "core/section_replay.h"

#include <cstdint>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct HeldLockAddresses {
  uint32_t lock;  // an nn::os critical section
  uint32_t event; // offsets in it: the light event, 1 when free
  uint32_t owner; // the holder's TLS address
  uint32_t count; // the holder's nested entries
};

class HeldLock {
public:
  explicit HeldLock(const HeldLockAddresses &addresses) : addresses_(addresses) {
  }

  // Adds the lock's release to `out` when the section started with a thread other than
  // `sectionTls` holding it; true then.
  bool release(Core::SectionReplay &replay, uint32_t sectionTls,
               std::vector<Core::SectionReplay::Input> &out) const;

private:
  HeldLockAddresses addresses_;
};

} // namespace z3d::app
