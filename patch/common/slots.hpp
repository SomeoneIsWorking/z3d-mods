// The item slots shared by both games' patches (docs/plans/pc-controls-hud.md): what a slot
// holds, packed for a save's spare bytes, and which of the game's item buttons (lanes) carry the
// slots in use.
#pragma once

#include "slot_link.hpp"

#include <stdint.h>

namespace z3dmods {

// Slot values of `Bits` bits (below 8) packed low bits first into bytes of a save. A value is
// what the game's patch makes of it: an inventory slot, a song, or kNone for nothing.
template <uint32_t Bits> class SlotTable {
public:
  static constexpr uint8_t kNone = (1U << Bits) - 1;

  // The bytes `count` values take.
  static constexpr uint32_t bytesFor(uint32_t count) {
    return (count * Bits + 7) / 8;
  }

  explicit SlotTable(volatile uint8_t *bytes) : bytes_(bytes) {
  }

  uint8_t get(uint32_t index) const {
    const uint32_t bit = index * Bits;
    return static_cast<uint8_t>((word(bit) >> (bit % 8)) & kNone);
  }

  void set(uint32_t index, uint8_t value) {
    const uint32_t bit = index * Bits;
    const uint32_t shift = bit % 8;
    const uint32_t changed = (word(bit) & ~(uint32_t{kNone} << shift)) | (uint32_t{value} << shift);
    bytes_[bit / 8] = static_cast<uint8_t>(changed);
    if (spills(bit)) {
      bytes_[bit / 8 + 1] = static_cast<uint8_t>(changed >> 8);
    }
  }

  // Puts `value` in each slot of `pressed` (a bit a slot) among the `count` slots from `first`; a
  // slot that held it takes what that slot had.
  void assign(uint32_t first, uint32_t count, uint32_t pressed, uint8_t value) {
    for (uint32_t slot = 0; slot < count; ++slot) {
      if (((pressed >> slot) & 1U) == 0) {
        continue;
      }
      for (uint32_t other = 0; value != kNone && other < count; ++other) {
        if (other != slot && get(first + other) == value) {
          set(first + other, get(first + slot));
        }
      }
      set(first + slot, value);
    }
  }

private:
  // Whether the value starting at `bit` reaches into the next byte.
  static constexpr bool spills(uint32_t bit) {
    return bit % 8 + Bits > 8;
  }

  // The byte the value at `bit` starts in and, where it spills, the next one above it.
  uint32_t word(uint32_t bit) const {
    return bytes_[bit / 8] | (spills(bit) ? uint32_t{bytes_[bit / 8 + 1]} << 8 : 0U);
  }

  volatile uint8_t *bytes_;
};

// Which slot each of the game's `Count` item buttons carries. A pressed slot keeps its lane while
// its item is in hand; a new one takes an empty lane, else the one pressed longest ago.
template <uint32_t Count> class SlotLanes {
public:
  constexpr SlotLanes() {
    for (uint8_t &slot : slots_) {
      slot = kSlotEmpty;
    }
  }

  uint8_t slot(uint32_t lane) const {
    return slots_[lane];
  }

  bool any() const {
    for (const uint8_t slot : slots_) {
      if (slot != kSlotEmpty) {
        return true;
      }
    }
    return false;
  }

  void drop(uint32_t lane) {
    slots_[lane] = kSlotEmpty;
  }

  // The lane `slot` is pressed on, or Count when every other lane is in `busy` (a bit a lane).
  uint32_t take(uint8_t slot, uint32_t busy) {
    uint32_t found = Count;
    for (uint32_t lane = 0; lane < Count; ++lane) {
      if (slots_[lane] == slot) {
        found = lane;
        break;
      }
      if ((busy >> lane) & 1U) {
        continue;
      }
      if (found == Count || (slots_[found] != kSlotEmpty && (slots_[lane] == kSlotEmpty || used_[lane] < used_[found]))) {
        found = lane;
      }
    }
    if (found != Count) {
      slots_[found] = slot;
      used_[found] = ++clock_;
    }
    return found;
  }

private:
  uint8_t slots_[Count];
  uint32_t used_[Count] = {};
  uint32_t clock_ = 0;
};

} // namespace z3dmods
