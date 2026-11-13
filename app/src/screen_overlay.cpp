#include "screen_overlay.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

#include "common/settings.h"
#include "core/3ds.h"

#include <SDL3/SDL_rect.h>
#include <lucent/json.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

// Shares of the window's height.
constexpr float kInsetHeight = 0.40F;
constexpr float kInsetMargin = 0.015F;
// The game's menus keep a title bar along the top of the top screen.
constexpr float kMenuHeight = 0.88F;
// Side by side: the top screen's height, which leaves the touch screen the rest of the width.
constexpr float kSplitTopHeight = 0.6F;
constexpr uint16_t kInsetOpacity = 85;
constexpr uint16_t kOpaque = 100;
constexpr float kTopAspect =
    static_cast<float>(Core::kScreenTopWidth) / static_cast<float>(Core::kScreenTopHeight);
constexpr float kTouchAspect =
    static_cast<float>(Core::kScreenBottomWidth) / static_cast<float>(Core::kScreenBottomHeight);

std::optional<uint32_t> readPointer(const Emulator &emulator, uint32_t address) {
  std::array<uint8_t, sizeof(uint32_t)> bytes{};
  if (!emulator.read(address, bytes)) {
    return std::nullopt;
  }
  uint32_t value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
}

OverlayPlacement parsePlacement(std::string_view name) {
  constexpr std::array kNames{std::pair{std::string_view("split"), OverlayPlacement::Split},
                              std::pair{std::string_view("inset"), OverlayPlacement::Inset},
                              std::pair{std::string_view("menu"), OverlayPlacement::Menu},
                              std::pair{std::string_view("top_only"), OverlayPlacement::TopOnly}};
  for (const auto &[text, placement] : kNames) {
    if (text == name) {
      return placement;
    }
  }
  throw std::invalid_argument("unknown overlay placement " + std::string(name));
}

} // namespace

void placeOverlay(unsigned width, unsigned height, OverlayPlacement placement) {
  auto &values = Settings::values;
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  SDL_FRect top{0.0F, 0.0F, w, h};
  SDL_FRect touch{};
  switch (placement) {
  case OverlayPlacement::Split:
    top.h = h * kSplitTopHeight;
    top.w = top.h * kTopAspect;
    top.y = (h - top.h) / 2.0F;
    touch.w = w - top.w;
    touch.h = touch.w / kTouchAspect;
    touch.x = top.w;
    touch.y = (h - touch.h) / 2.0F;
    break;
  case OverlayPlacement::Inset:
    touch.h = h * kInsetHeight;
    touch.w = touch.h * kTouchAspect;
    touch.x = h * kInsetMargin;
    touch.y = h * kInsetMargin;
    break;
  case OverlayPlacement::TopOnly:
    break;
  case OverlayPlacement::Menu:
    touch.h = h * kMenuHeight;
    touch.w = touch.h * kTouchAspect;
    touch.x = (w - touch.w) / 2.0F;
    touch.y = h - touch.h;
    break;
  }
  values.custom_top_x = static_cast<uint16_t>(top.x);
  values.custom_top_y = static_cast<uint16_t>(top.y);
  values.custom_top_width = static_cast<uint16_t>(top.w);
  values.custom_top_height = static_cast<uint16_t>(top.h);
  values.custom_bottom_x = static_cast<uint16_t>(touch.x);
  values.custom_bottom_y = static_cast<uint16_t>(touch.y);
  values.custom_bottom_width = static_cast<uint16_t>(touch.w);
  values.custom_bottom_height = static_cast<uint16_t>(touch.h);
  values.custom_second_layer_opacity = placement == OverlayPlacement::Inset ? kInsetOpacity : kOpaque;
}

ScreenOverlay::ScreenOverlay(GameWindow &window, Emulator &emulator, const GameSetup &setup,
                             const std::filesystem::path &manifest)
    : window_(window), emulator_(emulator) {
  if (setup.layout != ScreenLayout::Overlay) {
    return;
  }
  std::ifstream file(manifest);
  if (!file) {
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(text, error);
  if (!document) {
    throw std::runtime_error(manifest.string() + " is not JSON " + error);
  }
  const lucent::json::Value *screens = document->find("screens");
  if (screens == nullptr) {
    return;
  }
  Screens found;
  for (const auto &[name, field] : {std::pair{"active", &found.active}, std::pair{"table", &found.table},
                                    std::pair{"hud", &found.hud}}) {
    const lucent::json::Value *value = screens->find(name);
    if (value == nullptr || !value->integer()) {
      throw std::runtime_error(manifest.string() + ": \"screens\" wants active, table and hud");
    }
    *field = static_cast<uint32_t>(*value->integer());
  }
  const lucent::json::Value *placements = screens->find("placements");
  const lucent::json::Value *hidden = screens->find("hud_hidden");
  if (placements == nullptr || placements->array() == nullptr || hidden == nullptr || hidden->array() == nullptr) {
    throw std::runtime_error(manifest.string() + ": \"screens\" wants placements and hud_hidden");
  }
  for (const lucent::json::Value &entry : *hidden->array()) {
    if (!entry.integer()) {
      throw std::runtime_error(manifest.string() + ": hud_hidden holds numbers");
    }
    found.hudHidden.push_back(static_cast<uint16_t>(*entry.integer()));
  }
  for (const lucent::json::Value &entry : *placements->array()) {
    found.placements.push_back(parsePlacement(entry.string() != nullptr ? *entry.string() : std::string()));
  }
  screens_ = found;
  emulator_.onFrame([this] { update(); });
}

OverlayPlacement ScreenOverlay::placement() const {
  const std::optional<uint32_t> active = readPointer(emulator_, screens_->active);
  if (!active || *active == 0) {
    return OverlayPlacement::Split;
  }
  for (size_t i = 0; i < screens_->placements.size(); ++i) {
    if (readPointer(emulator_, screens_->table + static_cast<uint32_t>(i * sizeof(uint32_t))) == active) {
      const OverlayPlacement placement = screens_->placements[i];
      return placement == OverlayPlacement::Inset && hudHidden() ? OverlayPlacement::TopOnly : placement;
    }
  }
  return OverlayPlacement::Split;
}

bool ScreenOverlay::hudHidden() const {
  std::array<uint8_t, sizeof(uint16_t)> bytes{};
  if (!emulator_.read(screens_->hud, bytes)) {
    return false;
  }
  uint16_t visibility = 0;
  std::memcpy(&visibility, bytes.data(), sizeof(visibility));
  return std::ranges::find(screens_->hudHidden, visibility) != screens_->hudHidden.end();
}

void ScreenOverlay::update() {
  const OverlayPlacement now = placement();
  if (now != placement_) {
    placement_ = now;
    window_.placeOverlay(now);
  }
}

} // namespace z3d::app
