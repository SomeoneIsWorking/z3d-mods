// The item slots' block the running game's patch keeps (tools/z3dmods/slot_link.py, "link" in
// z3d-options.json): after every frame the pad's held slots go in, and the pad table the game
// wants and what each slot holds come out. A block whose patch is out or not running (a title
// screen) has no slots and the pad by name.
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
  // What a slot holds: an item's id, or a song's number; the index in the game's button arrays
  // of the item button the item is on now, 0 for none; and, for a slot that plays only when
  // held, how far it has been held, 0 to 1.
  struct Slot {
    std::optional<uint8_t> item;
    std::optional<uint8_t> song;
    uint8_t lane = 0;
    float charge = 0.0F;
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
    uint32_t beat = 0;
    uint32_t charge = 0;
  };

  void update();

  Emulator &emulator_;
  PadInput &pad_;
  std::optional<Layout> layout_;
  std::array<Slot, kSlotCount> slots_{};
  PadMode mode_ = PadMode::ByName;
  // The block's beat as last read, and the frame it last moved in; none before it ever has.
  uint8_t beat_ = 0;
  std::optional<uint64_t> alive_;
};

} // namespace z3d::app
