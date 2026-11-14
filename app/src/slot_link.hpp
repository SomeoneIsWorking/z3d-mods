// The item slots' block the running game's patch keeps (tools/z3dmods/slot_link.py, "link" in
// z3d-options.json): after every frame the pad's held slots go in, and the pad table the game
// wants and what each slot holds come out.
#pragma once

#include "pad_input.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace z3d::app {

class Emulator;

class SlotLink {
public:
  // What a slot holds: an item's id, or a song's number; and the index in the game's button
  // arrays of the item button the item is on now, 0 for none.
  struct Slot {
    std::optional<uint8_t> item;
    std::optional<uint8_t> song;
    uint8_t lane = 0;
  };

  // Without "link" in `manifest` the game has no slots and its pad stays by name.
  SlotLink(Emulator &emulator, PadInput &pad, const std::filesystem::path &manifest);
  ~SlotLink();
  SlotLink(const SlotLink &) = delete;
  SlotLink &operator=(const SlotLink &) = delete;
  SlotLink(SlotLink &&) = delete;
  SlotLink &operator=(SlotLink &&) = delete;

  // As of the last frame.
  [[nodiscard]] const Slot &slot(size_t index) const {
    return slots_.at(index);
  }
  [[nodiscard]] PadMode mode() const {
    return mode_;
  }

private:
  // Guest addresses of the block's fields.
  struct Layout {
    uint32_t held = 0;
    uint32_t mode = 0;
    uint32_t items = 0;
    uint32_t songs = 0;
    uint32_t lanes = 0;
  };

  void update();

  Emulator &emulator_;
  PadInput &pad_;
  std::optional<Layout> layout_;
  std::array<Slot, kSlotCount> slots_{};
  PadMode mode_ = PadMode::ByName;
};

} // namespace z3d::app
