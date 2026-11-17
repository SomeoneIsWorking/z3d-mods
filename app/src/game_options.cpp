#include "game_options.hpp"

#include "emulator.hpp"
#include "pad_input.hpp"

#include <lucent/json.h>
#include <lucent/log.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace z3d::app {
namespace {

constexpr const char *kLayoutId = "layout";
constexpr const char *kTexturesId = "textures";
constexpr const char *kHudSizeId = "hud_size";
constexpr const char *kHudOpacityId = "hud_opacity";
// The HUD's sizes and opacities, the default of each its 1; past 1.5 the D-pad meets the keys.
constexpr std::array kHudSizes{0.5, 0.75, 1.0, 1.25, 1.5};
constexpr std::array kHudOpacities{0.25, 0.5, 0.75, 1.0};
constexpr double kPercent = 100.0;
constexpr std::string_view kCStickInvertX = "cstick_invert_x";
constexpr std::string_view kCStickInvertY = "cstick_invert_y";
constexpr std::string_view kMouseScale = "mouse_scale";
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
  bool hud = false;
  if (const auto document = readJson(manifest)) {
    hud = document->find("hud") != nullptr;
    const lucent::json::Value *address = document->find("address");
    const lucent::json::Value *options = document->find("options");
    if (address == nullptr || !address->integer() || options == nullptr || options->array() == nullptr) {
      throw std::runtime_error(manifest.string() + " wants \"address\" and \"options\"");
    }
    address_ = static_cast<uint32_t>(*address->integer());
    for (const lucent::json::Value &entry : *options->array()) {
      const lucent::json::Value *id = entry.find("id");
      const lucent::json::Value *label = entry.find("label");
      const lucent::json::Value *fallback = entry.find("default");
      if (id == nullptr || id->string() == nullptr || label == nullptr ||
          label->string() == nullptr || fallback == nullptr) {
        throw std::runtime_error(manifest.string() + ": an option wants id, label and default");
      }
      // A switch is on or off by default; a choice of scales starts on one of them.
      std::vector<std::string> choices = switchChoices();
      std::vector<double> scales;
      std::vector<std::string> layers;
      std::optional<int64_t> value;
      if (const lucent::json::Value *list = entry.find("scales");
          list != nullptr && list->array() != nullptr) {
        choices.clear();
        for (const lucent::json::Value &scale : *list->array()) {
          if (!scale.real()) {
            throw std::runtime_error(manifest.string() + ": " + *id->string() +
                                     "'s scales are numbers");
          }
          scales.push_back(*scale.real());
          choices.push_back(std::format("{}x", scales.back()));
        }
        value = fallback->integer();
      } else if (const lucent::json::Value *list = entry.find("layers");
                 list != nullptr && list->array() != nullptr) {
        // Off, then each layer by its label.
        choices.resize(1);
        for (const lucent::json::Value &layer : *list->array()) {
          const lucent::json::Value *name = layer.find("label");
          const lucent::json::Value *folder = layer.find("folder");
          if (name == nullptr || name->string() == nullptr || folder == nullptr ||
              folder->string() == nullptr) {
            throw std::runtime_error(manifest.string() + ": " + *id->string() +
                                     "'s layers want a label and a folder");
          }
          choices.push_back(*name->string());
          layers.push_back(*folder->string());
        }
        value = fallback->integer();
      } else if (fallback->boolean() != nullptr) {
        value = *fallback->boolean() ? 1 : 0;
      }
      if (!value || *value < 0 || static_cast<size_t>(*value) >= choices.size()) {
        throw std::runtime_error(manifest.string() + ": " + *id->string() +
                                 "'s default is not one of its choices");
      }
      const lucent::json::Value *pad = entry.find("pad");
      const bool onPad = pad != nullptr && pad->string() != nullptr;
      const bool inBlock = !onPad && layers.empty();
      if (inBlock && blockCount_ != items_.size()) {
        throw std::runtime_error(manifest.string() + ": the patch's options come before the app's");
      }
      items_.push_back({*id->string(), *label->string(), std::move(choices),
                        static_cast<uint8_t>(*value), onPad ? *pad->string() : std::string(),
                        std::move(scales), std::move(layers)});
      blockCount_ += inBlock ? 1 : 0;
    }
  }
  items_.push_back({kLayoutId,
                    "Screen layout",
                    std::vector<std::string>(kLayoutNames.begin(), kLayoutNames.end()),
                    static_cast<uint8_t>(setup_.layout),
                    {},
                    {},
                    {}});
  if (hud) {
    Option size;
    size.id = kHudSizeId;
    size.label = "HUD size";
    for (const double scale : kHudSizes) {
      size.scales.push_back(scale);
      size.choices.push_back(std::format("{}x", scale));
    }
    size.value = static_cast<uint8_t>(std::ranges::find(kHudSizes, 1.0) - kHudSizes.begin());
    items_.push_back(std::move(size));
    Option opacity;
    opacity.id = kHudOpacityId;
    opacity.label = "HUD opacity";
    for (const double share : kHudOpacities) {
      opacity.scales.push_back(share);
      opacity.choices.push_back(std::format("{}%", share * kPercent));
    }
    opacity.value = static_cast<uint8_t>(kHudOpacities.size() - 1);
    items_.push_back(std::move(opacity));
  }
  if (setup_.customTextures) {
    items_.push_back({kTexturesId, "Texture pack", switchChoices(), 1, {}, {}, {}});
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
    } else if (!item.layers.empty() && item.value != 0) {
      result.romfsLayers.push_back(item.layers.at(item.value - 1));
    }
  }
  return result;
}

HudLook GameOptions::hudLook() const {
  HudLook look;
  for (const Option &item : items_) {
    if (item.id == kHudSizeId) {
      look.size = static_cast<float>(item.scales.at(item.value));
    } else if (item.id == kHudOpacityId) {
      look.opacity = static_cast<float>(item.scales.at(item.value));
    }
  }
  return look;
}

void GameOptions::apply(Emulator &emulator, PadInput &pad) const {
  bool invertX = false;
  bool invertY = false;
  double mouseScale = 1.0;
  for (const Option &item : items_) {
    invertX = invertX || (item.pad == kCStickInvertX && item.value != 0);
    invertY = invertY || (item.pad == kCStickInvertY && item.value != 0);
    if (item.pad == kMouseScale) {
      mouseScale = item.scales.at(item.value);
    }
  }
  pad.invertCStick(invertX, invertY);
  pad.scaleMouse(mouseScale);
  // The manifest sits in the mod folder named by the title ID.
  pad.bindGame(setup_.options.parent_path().filename().string());
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
