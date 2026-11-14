#include "screen_overlay.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

#include "common/settings.h"
#include "core/3ds.h"

#include <lucent/json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

std::optional<uint32_t> readPointer(const Emulator &emulator, uint32_t address) {
  std::array<uint8_t, sizeof(uint32_t)> bytes{};
  if (!emulator.read(address, bytes)) {
    return std::nullopt;
  }
  uint32_t value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
}

} // namespace

OverlayRect parseOverlayRect(const lucent::json::Value *value, const std::string &where) {
  if (value == nullptr || value->array() == nullptr || value->array()->size() != 4) {
    throw std::runtime_error(where + " wants [x, y, width, height]");
  }
  std::array<float, 4> sides{};
  for (size_t i = 0; i < sides.size(); ++i) {
    const std::optional<double> side = (*value->array())[i].real();
    if (!side) {
      throw std::runtime_error(where + " holds numbers");
    }
    sides[i] = static_cast<float>(*side);
  }
  return {sides[0], sides[1], sides[2], sides[3]};
}

namespace {

float parseReal(const lucent::json::Value &object, std::string_view key, float otherwise) {
  const lucent::json::Value *value = object.find(key);
  if (value == nullptr) {
    return otherwise;
  }
  const std::optional<double> real = value->real();
  if (!real) {
    throw std::runtime_error("\"" + std::string(key) + "\" in a layout piece is a number");
  }
  return static_cast<float>(*real);
}

OverlayLayout parseLayout(const lucent::json::Value &value, const std::string &name) {
  OverlayLayout layout;
  if (const lucent::json::Value *top = value.find("top")) {
    layout.top = parseOverlayRect(top, "layout " + name + ": \"top\"");
  }
  const lucent::json::Value *pieces = value.find("pieces");
  if (pieces == nullptr || pieces->array() == nullptr) {
    throw std::runtime_error("layout " + name + " wants \"pieces\"");
  }
  for (const lucent::json::Value &piece : *pieces->array()) {
    layout.pieces.push_back({parseOverlayRect(piece.find("from"), "layout " + name + ": \"from\""),
                             parseOverlayRect(piece.find("to"), "layout " + name + ": \"to\""),
                             parseReal(piece, "radius", 0.0F), parseReal(piece, "opacity", 1.0F)});
  }
  if (const lucent::json::Value *hud = value.find("hud")) {
    layout.hud = hud->boolean() != nullptr && *hud->boolean();
  }
  return layout;
}

// The rectangle touches are mapped through: all the pieces' extent.
OverlayRect extent(const OverlayLayout &layout) {
  if (layout.pieces.empty()) {
    return {};
  }
  float left = kOverlayWidth;
  float top = kOverlayHeight;
  float right = 0.0F;
  float bottom = 0.0F;
  for (const OverlayPiece &piece : layout.pieces) {
    left = std::min(left, piece.to.x);
    top = std::min(top, piece.to.y);
    right = std::max(right, piece.to.x + piece.to.w);
    bottom = std::max(bottom, piece.to.y + piece.to.h);
  }
  return {left, top, right - left, bottom - top};
}

} // namespace

OverlayLayout splitLayout() {
  return {{0.0F, 48.0F, 240.0F, 144.0F},
          {{{0.0F, 0.0F, Core::kScreenBottomWidth, Core::kScreenBottomHeight},
            {240.0F, 60.0F, 160.0F, 120.0F}}}};
}

OverlayRect overlayWindowRect(unsigned width, unsigned height, const OverlayRect &rect) {
  const float unit = static_cast<float>(height) / kOverlayHeight;
  const float left = (static_cast<float>(width) - kOverlayWidth * unit) / 2.0F;
  return {left + rect.x * unit, rect.y * unit, rect.w * unit, rect.h * unit};
}

void placeOverlay(unsigned width, unsigned height, const OverlayLayout &layout) {
  auto &values = Settings::values;
  const auto place = [&](const OverlayRect &rect) {
    return overlayWindowRect(width, height, rect);
  };
  const OverlayRect top = place(layout.top);
  const OverlayRect touch = place(extent(layout));
  values.custom_top_x = static_cast<uint16_t>(std::max(top.x, 0.0F));
  values.custom_top_y = static_cast<uint16_t>(top.y);
  values.custom_top_width = static_cast<uint16_t>(top.w);
  values.custom_top_height = static_cast<uint16_t>(top.h);
  values.custom_bottom_x = static_cast<uint16_t>(std::max(touch.x, 0.0F));
  values.custom_bottom_y = static_cast<uint16_t>(touch.y);
  values.custom_bottom_width = static_cast<uint16_t>(touch.w);
  values.custom_bottom_height = static_cast<uint16_t>(touch.h);
  values.custom_second_layer_opacity = 100;
  values.custom_bottom_pieces.clear();
  for (const OverlayPiece &piece : layout.pieces) {
    const OverlayRect to = place(piece.to);
    values.custom_bottom_pieces.push_back(
        {piece.from.x, piece.from.y, piece.from.w, piece.from.h, to.x, to.y, to.w, to.h,
         piece.radius * to.w / piece.from.w, piece.opacity});
  }
}

std::optional<TouchPoint> overlayTouch(unsigned width, unsigned height,
                                       const OverlayLayout &layout, float x, float y) {
  const float unit = static_cast<float>(height) / kOverlayHeight;
  const float ux = (x - (static_cast<float>(width) - kOverlayWidth * unit) / 2.0F) / unit;
  const float uy = y / unit;
  for (auto piece = layout.pieces.rbegin(); piece != layout.pieces.rend(); ++piece) {
    const OverlayRect &to = piece->to;
    if (ux < to.x || uy < to.y || ux >= to.x + to.w || uy >= to.y + to.h) {
      continue;
    }
    const float tx = piece->from.x + (ux - to.x) * piece->from.w / to.w;
    const float ty = piece->from.y + (uy - to.y) * piece->from.h / to.h;
    return TouchPoint{
        static_cast<uint16_t>(std::clamp(tx, 0.0F, static_cast<float>(Core::kScreenBottomWidth - 1))),
        static_cast<uint16_t>(
            std::clamp(ty, 0.0F, static_cast<float>(Core::kScreenBottomHeight - 1)))};
  }
  return std::nullopt;
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
  for (const auto &[name, field] :
       {std::pair{"active", &found.active}, std::pair{"table", &found.table},
        std::pair{"hud", &found.hud}}) {
    const lucent::json::Value *value = screens->find(name);
    if (value == nullptr || !value->integer()) {
      throw std::runtime_error(manifest.string() + ": \"screens\" wants active, table and hud");
    }
    *field = static_cast<uint32_t>(*value->integer());
  }
  const lucent::json::Value *layouts = screens->find("layouts");
  const lucent::json::Value *placements = screens->find("placements");
  const lucent::json::Value *hidden = screens->find("hud_hidden");
  const lucent::json::Value *outside = screens->find("outside");
  if (layouts == nullptr || layouts->object() == nullptr || placements == nullptr ||
      placements->array() == nullptr || hidden == nullptr || hidden->array() == nullptr ||
      outside == nullptr || outside->string() == nullptr) {
    throw std::runtime_error(manifest.string() +
                             ": \"screens\" wants layouts, placements, hud_hidden and outside");
  }
  std::vector<std::string> names{"split"};
  found.layouts.push_back(splitLayout());
  for (const auto &[name, layout] : *layouts->object()) {
    names.push_back(name);
    found.layouts.push_back(parseLayout(layout, name));
  }
  const auto named = [&](const lucent::json::Value *name) {
    const auto at = name != nullptr && name->string() != nullptr
                        ? std::ranges::find(names, *name->string())
                        : names.end();
    if (at == names.end()) {
      throw std::runtime_error(manifest.string() + ": a screen names a layout that is not there");
    }
    return static_cast<size_t>(at - names.begin());
  };
  for (const lucent::json::Value &entry : *hidden->array()) {
    if (!entry.integer()) {
      throw std::runtime_error(manifest.string() + ": hud_hidden holds numbers");
    }
    found.hudHidden.push_back(static_cast<uint16_t>(*entry.integer()));
  }
  for (const lucent::json::Value &entry : *placements->array()) {
    ScreenPlacement placement{named(entry.find("layout")), std::nullopt};
    if (const lucent::json::Value *whenHidden = entry.find("hidden")) {
      placement.hidden = named(whenHidden);
    }
    found.placements.push_back(placement);
  }
  found.outside = named(outside);
  screens_ = std::move(found);
  update();
  emulator_.onFrame([this] {
    update();
  });
}

size_t ScreenOverlay::layout() const {
  const std::optional<uint32_t> active = readPointer(emulator_, screens_->active);
  if (!active || *active == 0) {
    return screens_->outside;
  }
  for (size_t i = 0; i < screens_->placements.size(); ++i) {
    if (readPointer(emulator_, screens_->table + static_cast<uint32_t>(i * sizeof(uint32_t))) ==
        active) {
      const ScreenPlacement &screen = screens_->placements[i];
      return screen.hidden && hudHidden() ? *screen.hidden : screen.layout;
    }
  }
  return screens_->outside;
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
  const size_t now = layout();
  if (now != layout_) {
    layout_ = now;
    window_.placeOverlay(screens_->layouts[now]);
  }
}

} // namespace z3d::app
