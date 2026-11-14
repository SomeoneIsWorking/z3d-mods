#include "hud.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

#include <lucent/json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

constexpr const char *kHudSet = "hud";
constexpr const char *kPadSet = "gamepad-xbox360";
// Shares of a button's size: the item icon in its middle and the glyph on its top left corner.
constexpr float kIconShare = 0.68F;
constexpr float kGlyphShare = 0.46F;
// A counter's digits against its icon's height, and the gap before them.
constexpr float kDigitShare = 0.8F;
constexpr float kCounterGap = 0.25F;
// A meter's art is this many times as long as it is tall (port-assets hud/bar).
constexpr float kMeterAspect = 9.0F;

uint32_t integer(const lucent::json::Value &object, std::string_view key, uint32_t otherwise) {
  const lucent::json::Value *value = object.find(key);
  if (value == nullptr) {
    return otherwise;
  }
  if (!value->integer()) {
    throw std::runtime_error("hud: \"" + std::string(key) + "\" is a whole number");
  }
  return static_cast<uint32_t>(*value->integer());
}

std::string text(const lucent::json::Value &object, std::string_view key) {
  const lucent::json::Value *value = object.find(key);
  return value != nullptr && value->string() != nullptr ? *value->string() : std::string();
}

// The port-assets glyph of a pad button.
std::string_view glyphOf(SDL_GamepadButton button) {
  constexpr std::array kGlyphs{std::pair{SDL_GAMEPAD_BUTTON_SOUTH, std::string_view("face_a")},
                               std::pair{SDL_GAMEPAD_BUTTON_EAST, std::string_view("face_b")},
                               std::pair{SDL_GAMEPAD_BUTTON_WEST, std::string_view("face_x")},
                               std::pair{SDL_GAMEPAD_BUTTON_NORTH, std::string_view("face_y")}};
  for (const auto &[pad, glyph] : kGlyphs) {
    if (pad == button) {
      return glyph;
    }
  }
  return {};
}

std::optional<ItemButton> itemButton(const std::string &name) {
  constexpr std::array kNames{std::pair{std::string_view("X"), ItemButton::X},
                              std::pair{std::string_view("Y"), ItemButton::Y},
                              std::pair{std::string_view("I"), ItemButton::I},
                              std::pair{std::string_view("II"), ItemButton::II}};
  for (const auto &[text, button] : kNames) {
    if (text == name) {
      return button;
    }
  }
  return std::nullopt;
}

} // namespace

Hud::Hud(GameWindow &window, Emulator &emulator, const PadInput &pad, const ScreenOverlay &overlay,
         const std::filesystem::path &assets, const std::filesystem::path &manifest)
    : window_(window), emulator_(emulator), pad_(pad), overlay_(overlay) {
  std::ifstream file(manifest);
  if (assets.empty() || !file) {
    return;
  }
  const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(json, error);
  if (!document) {
    throw std::runtime_error(manifest.string() + " is not JSON " + error);
  }
  const lucent::json::Value *hud = document->find("hud");
  if (hud == nullptr) {
    return;
  }
  const lucent::json::Value *elements = hud->find("elements");
  if (elements == nullptr || elements->array() == nullptr) {
    throw std::runtime_error(manifest.string() + ": \"hud\" wants elements");
  }
  const auto guest = [](const lucent::json::Value &object, std::string_view key) {
    const lucent::json::Value *value = object.find(key);
    if (value == nullptr) {
      return GuestValue{};
    }
    return GuestValue{integer(*value, "address", 0), static_cast<uint8_t>(integer(*value, "bytes", 0))};
  };
  constexpr std::array kKinds{std::pair{std::string_view("hearts"), Kind::Hearts},
                              std::pair{std::string_view("meter"), Kind::Meter},
                              std::pair{std::string_view("counter"), Kind::Counter},
                              std::pair{std::string_view("button"), Kind::Button}};
  for (const lucent::json::Value &entry : *elements->array()) {
    const std::string kind = text(entry, "kind");
    const auto *known = std::ranges::find(kKinds, std::string_view(kind), &std::pair<std::string_view, Kind>::first);
    if (known == kKinds.end()) {
      throw std::runtime_error(manifest.string() + ": no hud element kind " + kind);
    }
    Element element;
    element.kind = known->second;
    element.rect = parseOverlayRect(entry.find("rect"), "hud " + kind + ": \"rect\"");
    element.value = guest(entry, "value");
    element.capacity = guest(entry, "capacity");
    element.unit = integer(entry, "unit", 1);
    element.columns = integer(entry, "columns", 10);
    element.full = integer(entry, "full", 0);
    element.icon = text(entry, "icon");
    element.frame = text(entry, "frame");
    element.glyph = text(entry, "glyph");
    element.chordItem = itemButton(text(entry, "chord"));
    element.none = integer(entry, "none", 0xFF);
    elements_.push_back(std::move(element));
  }
  icons_ = manifest.parent_path() / text(*hud, "icons");
  atlas_.emplace(assets);
  emulator_.onFrame([this] {
    update();
  });
}

Hud::~Hud() {
  Settings::values.overlay_quads.clear();
  Settings::values.overlay_image.reset();
}

std::optional<uint32_t> Hud::read(const GuestValue &value) const {
  std::array<uint8_t, sizeof(uint32_t)> bytes{};
  if (value.bytes == 0 || value.bytes > bytes.size() ||
      !emulator_.read(value.address, std::span(bytes).first(value.bytes))) {
    return std::nullopt;
  }
  uint32_t result = 0;
  for (size_t i = 0; i < value.bytes; ++i) {
    result |= static_cast<uint32_t>(bytes[i]) << (8 * i);
  }
  return result;
}

void Hud::update() {
  quads_.clear();
  if (overlay_.hudShown()) {
    const auto [width, height] = window_.pixelSize();
    if (width != width_ || height != height_) {
      width_ = width;
      height_ = height;
      atlas_->clear();
    }
    for (const Element &element : elements_) {
      draw(element);
    }
  }
  Settings::values.overlay_image = atlas_->image();
  Settings::values.overlay_quads = quads_;
}

// `sprite`, or its left `share`, drawn at `at` window pixels.
void Hud::quad(const HudSprite &sprite, const OverlayRect &at, float share) {
  if (sprite.w <= 0.0F || share <= 0.0F) {
    return;
  }
  quads_.push_back({sprite.x, sprite.y, sprite.w * share, sprite.h, at.x, at.y, at.w * share, at.h, 1.0F});
}

void Hud::draw(const Element &element) {
  // Whole window pixels, so a sprite is drawn pixel for pixel.
  OverlayRect box = overlayWindowRect(width_, height_, element.rect);
  box = {std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
  const auto sized = [&](const char *set, const std::string &name, float w, float h) {
    return atlas_->svg(set, name, static_cast<int>(w), static_cast<int>(h));
  };
  switch (element.kind) {
  case Kind::Hearts: {
    const std::optional<uint32_t> health = read(element.value);
    const std::optional<uint32_t> capacity = read(element.capacity);
    if (!health || !capacity || element.unit == 0 || element.columns == 0) {
      return;
    }
    const HudSprite full = sized(kHudSet, "heart", box.w, box.h);
    const HudSprite empty = sized(kHudSet, "heart_empty", box.w, box.h);
    for (uint32_t i = 0; i < *capacity / element.unit; ++i) {
      const OverlayRect at{box.x + static_cast<float>(i % element.columns) * box.w,
                           box.y + static_cast<float>(i / element.columns) * box.h, box.w, box.h};
      const uint32_t before = i * element.unit;
      const float share =
          *health <= before ? 0.0F
                            : std::min(1.0F, static_cast<float>(*health - before) / static_cast<float>(element.unit));
      quad(empty, at);
      quad(full, at, share);
    }
    return;
  }
  case Kind::Meter: {
    const std::optional<uint32_t> value = read(element.value);
    const uint32_t full = element.full != 0 ? element.full : read(element.capacity).value_or(0);
    if (!value || full == 0) {
      return;
    }
    box.w = std::round(box.h * kMeterAspect);
    quad(sized(kHudSet, "bar", box.w, box.h), box);
    quad(sized(kHudSet, "bar_fill", box.w, box.h), box,
         std::min(1.0F, static_cast<float>(*value) / static_cast<float>(full)));
    return;
  }
  case Kind::Counter: {
    const std::optional<uint32_t> value = read(element.value);
    if (!value) {
      return;
    }
    quad(sized(kHudSet, element.icon, box.h, box.h), {box.x, box.y, box.h, box.h});
    const float digit = std::round(box.h * kDigitShare);
    float x = box.x + box.h + std::round(box.h * kCounterGap);
    for (const char character : std::to_string(*value)) {
      const HudSprite sprite = atlas_->text(std::string(1, character), static_cast<int>(digit));
      quad(sprite, {x, box.y + std::round((box.h - sprite.h) / 2.0F), sprite.w, sprite.h});
      x += sprite.w;
    }
    return;
  }
  case Kind::Button: {
    quad(sized(kHudSet, element.frame, box.w, box.h), box);
    const std::optional<uint32_t> item = read(element.value);
    if (item && *item != element.none) {
      const float size = std::round(box.w * kIconShare);
      const float inset = std::round((box.w - size) / 2.0F);
      quad(atlas_->image(icons_ / (std::to_string(*item) + ".png"), static_cast<int>(size), static_cast<int>(size)),
           {box.x + inset, box.y + inset, size, size});
    }
    std::string glyph = element.glyph;
    const ChordLayout *chord = pad_.chord();
    if (chord != nullptr && pad_.state().buttons[Settings::NativeButton::Debug] && element.chordItem) {
      const auto *slot = std::ranges::find(chord->slots, *element.chordItem, &ChordSlot::item);
      glyph = slot != chord->slots.end() ? std::string(glyphOf(slot->face)) : std::string();
    }
    if (!glyph.empty()) {
      const float size = std::round(box.w * kGlyphShare);
      quad(sized(kPadSet, glyph, size, size), {box.x - std::round(size / 4.0F), box.y - std::round(size / 4.0F), size, size});
    }
    return;
  }
  }
}

} // namespace z3d::app
