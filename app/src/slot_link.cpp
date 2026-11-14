#include "slot_link.hpp"

#include "emulator.hpp"

#include <lucent/json.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace z3d::app {
namespace {

// The patch's value for a slot without an item (kSlotEmpty in its slot_link.hpp).
constexpr uint8_t kNoItem = 0xFF;
// A slot's full charge (kSlotCharged there).
constexpr float kCharged = 255.0F;
// Frames without a beat after which nothing updates the block: the games update at 20 a second or
// more.
constexpr uint64_t kDeadFrames = 30;

uint32_t address(const lucent::json::Value &link, std::string_view field,
                 const std::filesystem::path &manifest) {
  const lucent::json::Value *value = link.find(field);
  if (value == nullptr || !value->integer()) {
    throw std::runtime_error(manifest.string() + ": \"link\" wants " + std::string(field));
  }
  return static_cast<uint32_t>(*value->integer());
}

} // namespace

SlotLink::SlotLink(Emulator &emulator, PadInput &pad, const std::filesystem::path &manifest)
    : emulator_(emulator), pad_(pad) {
  std::ifstream file(manifest);
  if (!file) {
    return;
  }
  const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(json, error);
  if (!document) {
    throw std::runtime_error(manifest.string() + " is not JSON " + error);
  }
  const lucent::json::Value *link = document->find("link");
  if (link == nullptr) {
    return;
  }
  if (address(*link, "slots", manifest) != kSlotCount) {
    throw std::runtime_error(manifest.string() +
                             ": the patch's slots are not the app's; install the game's set again");
  }
  layout_ = Layout{address(*link, "held", manifest),  address(*link, "mode", manifest),
                   address(*link, "items", manifest), address(*link, "songs", manifest),
                   address(*link, "lanes", manifest), address(*link, "beat", manifest),
                   address(*link, "charge", manifest)};
  emulator_.onFrame([this] {
    update();
  });
}

SlotLink::~SlotLink() {
  pad_.setMode(PadMode::ByName);
}

void SlotLink::update() {
  const uint16_t held = pad_.slotsHeld();
  const std::array<uint8_t, 2> heldBytes{static_cast<uint8_t>(held),
                                         static_cast<uint8_t>(held >> 8)};
  std::array<uint8_t, 1> mode{};
  std::array<uint8_t, kSlotCount> items{};
  std::array<uint8_t, kSlotCount> songs{};
  std::array<uint8_t, kSlotCount> lanes{};
  std::array<uint8_t, 1> beat{};
  std::array<uint8_t, kSlotCount> charge{};
  // A process without the patch's pages (a game still loading) has no slots yet.
  if (!emulator_.write(layout_->held, heldBytes) || !emulator_.read(layout_->mode, mode) ||
      !emulator_.read(layout_->items, items) || !emulator_.read(layout_->songs, songs) ||
      !emulator_.read(layout_->lanes, lanes) || !emulator_.read(layout_->beat, beat) ||
      !emulator_.read(layout_->charge, charge) || mode[0] > static_cast<uint8_t>(PadMode::Assign)) {
    alive_.reset();
  } else if (beat[0] != beat_ || !alive_) {
    alive_ = beat[0] != beat_ ? std::optional(emulator_.framesRun()) : alive_;
    beat_ = beat[0];
  }
  if (!alive_ || emulator_.framesRun() - *alive_ >= kDeadFrames) {
    mode_ = PadMode::ByName;
    slots_ = {};
  } else {
    mode_ = static_cast<PadMode>(mode[0]);
    for (size_t i = 0; i < kSlotCount; ++i) {
      slots_[i] = {items[i] != kNoItem ? std::optional(items[i]) : std::nullopt,
                   songs[i] != 0 ? std::optional(static_cast<uint8_t>(songs[i] - 1)) : std::nullopt,
                   lanes[i], static_cast<float>(charge[i]) / kCharged};
    }
  }
  pad_.setMode(mode_);
}

} // namespace z3d::app
