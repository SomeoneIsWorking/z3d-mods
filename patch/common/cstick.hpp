// The New 3DS C-stick, read from the ir:rst service's shared memory (3dbrew IR_RST layout).
#pragma once

#include "ctr.hpp"

#include <stdint.h>

namespace z3dmods {

struct StickPosition {
  int16_t x = 0;
  int16_t y = 0;
};

class CStick {
public:
  // Opens ir:rst on first use; false while the service cannot be reached.
  bool read(StickPosition *out) {
    if (state_ == State::Closed) {
      state_ = open() ? State::Open : State::Failed;
    }
    if (state_ != State::Open) {
      return false;
    }
    const auto *memory = reinterpret_cast<const volatile uint32_t *>(memory_);
    const uint32_t index = memory[kLatestIndex] & (kEntries - 1);
    const auto *entry = reinterpret_cast<const volatile int16_t *>(memory + kEntriesStart + index * kEntryWords);
    out->x = entry[kStickHalfword];
    out->y = entry[kStickHalfword + 1];
    return true;
  }

private:
  enum class State : uint8_t { Closed, Open, Failed };

  // Header: two tick words, then the index of the newest of 8 entries at word 4; entries of four
  // words from word 6, the C-stick's x and y in the fourth.
  static constexpr uint32_t kLatestIndex = 4;
  static constexpr uint32_t kEntries = 8;
  static constexpr uint32_t kEntriesStart = 6;
  static constexpr uint32_t kEntryWords = 4;
  static constexpr uint32_t kStickHalfword = 6;
  static constexpr uint32_t kSize = 0x98;
  // Where a title's own mappings go.
  static constexpr uint32_t kMapStart = 0x10000000;
  static constexpr uint32_t kMapEnd = 0x14000000;
  static constexpr uint32_t kUpdatePeriodMs = 10;

  bool open() {
    ctr::ServiceManager srv;
    ctr::Handle session = 0;
    if (srv.open() < 0 || srv.serviceHandle(&session, "ir:rst") < 0) {
      return false;
    }
    uint32_t *cmd = ctr::commandBuffer();
    cmd[0] = ctr::header(0x1, 0, 0); // GetHandles
    if (ctr::call(session) < 0) {
      return false;
    }
    const ctr::Handle block = cmd[3];
    cmd = ctr::commandBuffer();
    cmd[0] = ctr::header(0x2, 2, 0); // Initialize
    cmd[1] = kUpdatePeriodMs;
    cmd[2] = 0;
    if (ctr::call(session) < 0) {
      return false;
    }
    const uint32_t address = ctr::freeRange(kMapStart, kMapEnd, (kSize + ctr::kPageSize - 1) & ~(ctr::kPageSize - 1));
    if (address == 0 || ctr::mapMemoryBlock(block, address, ctr::kMemPermRead, ctr::kMemPermDontCare) < 0) {
      return false;
    }
    memory_ = address;
    return true;
  }

  State state_ = State::Closed;
  uint32_t memory_ = 0;
};

} // namespace z3dmods
