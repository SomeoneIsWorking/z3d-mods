#include "game_options.hpp"

#include "emulator.hpp"
#include "pad_input.hpp"

#include <lucent/json.h>
#include <lucent/log.h>

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace z3d::app {
namespace {

constexpr const char *kLayoutId = "layout";
constexpr const char *kTexturesId = "textures";
constexpr std::string_view kCStickInvertX = "cstick_invert_x";
constexpr std::string_view kCStickInvertY = "cstick_invert_y";
// In ScreenLayout's order.
constexpr std::array<const char *, 5> kLayoutNames{"Both screens", "Top screen only", "Large top screen",
                                                   "Side by side", "Single screen"};

std::vector<std::string> switchChoices() {
  return {"Off", "On"};
}

std::optional<lucent::json::Value> readJson(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  auto document = lucent::json::parse(text, error);
  if (!document) {
    throw std::runtime_error(path.string() + " is not JSON " + error);
  }
  return document;
}

} // namespace

GameOptions::GameOptions(const GameSetup &setup, const std::filesystem::path &manifest,
                         std::filesystem::path stored)
    : setup_(setup), stored_(std::move(stored)) {
  if (const auto document = readJson(manifest)) {
    const lucent::json::Value *address = document->find("address");
    const lucent::json::Value *options = document->find("options");
    if (address == nullptr || !address->integer() || options == nullptr || options->array() == nullptr) {
      throw std::runtime_error(manifest.string() + " wants \"address\" and \"options\"");
    }
    address_ = static_cast<uint32_t>(*address->integer());
    if (const lucent::json::Value *pad = document->find("pad")) {
      const lucent::json::Value *swap = pad->find("swap_xy");
      swapXY_ = swap != nullptr && swap->boolean() != nullptr && *swap->boolean();
    }
    for (const lucent::json::Value &entry : *options->array()) {
      const lucent::json::Value *id = entry.find("id");
      const lucent::json::Value *label = entry.find("label");
      const lucent::json::Value *fallback = entry.find("default");
      if (id == nullptr || id->string() == nullptr || label == nullptr || label->string() == nullptr ||
          fallback == nullptr || fallback->boolean() == nullptr) {
        throw std::runtime_error(manifest.string() + ": an option wants id, label and default");
      }
      const lucent::json::Value *pad = entry.find("pad");
      const bool onPad = pad != nullptr && pad->string() != nullptr;
      if (!onPad && blockCount_ != items_.size()) {
        throw std::runtime_error(manifest.string() + ": the patch's options come before the pad's");
      }
      items_.push_back({*id->string(), *label->string(), switchChoices(),
                        static_cast<uint8_t>(*fallback->boolean() ? 1 : 0),
                        onPad ? *pad->string() : std::string()});
      blockCount_ += onPad ? 0 : 1;
    }
  }
  items_.push_back({kLayoutId, "Screen layout",
                    std::vector<std::string>(kLayoutNames.begin(), kLayoutNames.end()),
                    static_cast<uint8_t>(setup_.layout), {}});
  if (setup_.customTextures) {
    items_.push_back({kTexturesId, "Texture pack", switchChoices(), 1, {}});
  }
  if (stored_.empty()) {
    return;
  }
  if (const auto document = readJson(stored_)) {
    for (Option &item : items_) {
      const lucent::json::Value *value = document->find(item.id);
      if (value != nullptr && value->integer() && *value->integer() >= 0 &&
          static_cast<size_t>(*value->integer()) < item.choices.size()) {
        item.value = static_cast<uint8_t>(*value->integer());
      }
    }
  }
}

void GameOptions::step(size_t index, int direction) {
  Option &item = items_.at(index);
  const auto count = static_cast<int>(item.choices.size());
  item.value = static_cast<uint8_t>((item.value + count + direction) % count);
  save();
}

bool GameOptions::set(std::string_view id, uint8_t value) {
  for (Option &item : items_) {
    if (item.id == id && value < item.choices.size()) {
      item.value = value;
      save();
      return true;
    }
  }
  return false;
}

GameSetup GameOptions::setup() const {
  GameSetup result = setup_;
  for (const Option &item : items_) {
    if (item.id == kLayoutId) {
      result.layout = static_cast<ScreenLayout>(item.value);
    } else if (item.id == kTexturesId) {
      result.customTextures = item.value != 0;
    }
  }
  return result;
}

void GameOptions::apply(Emulator &emulator, PadInput &pad) const {
  bool invertX = false;
  bool invertY = false;
  for (const Option &item : items_) {
    invertX = invertX || (item.pad == kCStickInvertX && item.value != 0);
    invertY = invertY || (item.pad == kCStickInvertY && item.value != 0);
  }
  pad.invertCStick(invertX, invertY);
  pad.swapXY(swapXY_);
  if (blockCount_ == 0) {
    return;
  }
  std::vector<uint8_t> block;
  for (size_t i = 0; i < blockCount_; ++i) {
    block.push_back(items_[i].value);
  }
  if (!emulator.write(address_, block)) {
    throw std::runtime_error("the patch's options block is not mapped in the running game");
  }
  lucent::info("z3d", "wrote {} patch options", block.size());
}

void GameOptions::save() const {
  if (stored_.empty()) {
    return;
  }
  std::filesystem::create_directories(stored_.parent_path());
  std::ofstream file(stored_);
  file << "{";
  for (size_t i = 0; i < items_.size(); ++i) {
    file << (i == 0 ? "\n" : ",\n") << "  \"" << items_[i].id << "\": " << static_cast<int>(items_[i].value);
  }
  file << "\n}\n";
  if (!file) {
    throw std::runtime_error("cannot write " + stored_.string());
  }
}

} // namespace z3d::app
