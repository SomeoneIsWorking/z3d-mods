#include "hud.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

#include <SDL3/SDL_keyboard.h>
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
constexpr const char *kKeySet = "keyboard";
// A key's capital against its cap.
constexpr float kKeyLabelShare = 0.45F;
// Shares of a button's size: the item icon in its middle and the glyph on its top left corner.
constexpr float kIconShare = 0.68F;
constexpr float kGlyphShare = 0.46F;
// An item's count against its button, and a disabled item's opacity.
constexpr float kCountShare = 0.34F;
// The clear margin around a frame's art against its size (port-assets hud/ring and hud/slot).
constexpr float kFrameMargin = 3.0F / 72.0F;
constexpr float kDimmed = 0.4F;
// A counter's digits against its icon's height, and the gap before them.
constexpr float kDigitShare = 0.62F;
constexpr float kCounterGap = 0.3F;
// A meter's art is this many times as long as it is tall (port-assets hud/bar).
constexpr float kMeterAspect = 9.0F;
// A button's action word against its size.
constexpr float kLabelShare = 0.3F;

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
                               std::pair{SDL_GAMEPAD_BUTTON_NORTH, std::string_view("face_y")},
                               std::pair{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, std::string_view("lb")},
                               std::pair{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, std::string_view("rb")},
                               std::pair{SDL_GAMEPAD_BUTTON_RIGHT_STICK, std::string_view("rs")},
                               std::pair{SDL_GAMEPAD_BUTTON_START, std::string_view("start")},
                               std::pair{SDL_GAMEPAD_BUTTON_BACK, std::string_view("back")}};
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

std::optional<Settings::NativeButton::Values> nativeButton(const std::string &name) {
  constexpr std::array kNames{std::pair{std::string_view("A"), Settings::NativeButton::A},
                              std::pair{std::string_view("B"), Settings::NativeButton::B},
                              std::pair{std::string_view("X"), Settings::NativeButton::X},
                              std::pair{std::string_view("Y"), Settings::NativeButton::Y},
                              std::pair{std::string_view("L"), Settings::NativeButton::L},
                              std::pair{std::string_view("R"), Settings::NativeButton::R},
                              std::pair{std::string_view("ZL"), Settings::NativeButton::ZL},
                              std::pair{std::string_view("ZR"), Settings::NativeButton::ZR},
                              std::pair{std::string_view("Start"), Settings::NativeButton::Start},
                              std::pair{std::string_view("Select"), Settings::NativeButton::Select}};
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
    return GuestValue{integer(*value, "address", 0), static_cast<uint8_t>(integer(*value, "bytes", 0)),
                      integer(*value, "pointer", 0), static_cast<uint8_t>(integer(*value, "shift", 0)),
                      integer(*value, "mask", UINT32_MAX)};
  };
  const auto strings = [](const lucent::json::Value &object, std::string_view key) {
    std::vector<std::string> found;
    if (const lucent::json::Value *list = object.find(key); list != nullptr && list->array() != nullptr) {
      for (const lucent::json::Value &entry : *list->array()) {
        found.push_back(entry.string() != nullptr ? *entry.string() : std::string());
      }
    }
    return found;
  };
  // { address, bytes, equals } or { address, bytes, least, most }.
  const auto conditions = [&](const lucent::json::Value &object, std::string_view key) {
    std::vector<Condition> found;
    const lucent::json::Value *list = object.find(key);
    if (list == nullptr || list->array() == nullptr) {
      return found;
    }
    for (const lucent::json::Value &entry : *list->array()) {
      const uint32_t least = integer(entry, "equals", integer(entry, "least", 0));
      const uint32_t most = integer(entry, "equals", integer(entry, "most", UINT32_MAX));
      found.push_back({GuestValue{integer(entry, "address", 0), static_cast<uint8_t>(integer(entry, "bytes", 0)),
                                  integer(entry, "pointer", 0)},
                       least, most});
    }
    return found;
  };
  show_ = conditions(*hud, "show");
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
    element.button = nativeButton(text(entry, "button"));
    element.itemButton = itemButton(text(entry, "item_button"));
    element.none = integer(entry, "none", 0xFF);
    element.when = conditions(entry, "when");
    element.index = guest(entry, "index");
    if (const lucent::json::Value *chord = entry.find("chord"); chord != nullptr) {
      element.chordValue = guest(*chord, "value");
      element.chordStatus = guest(*chord, "status");
      element.chordItem = itemButton(text(*chord, "item_button"));
    }
    element.label = guest(entry, "label");
    element.labels = strings(entry, "labels");
    element.art = strings(entry, "art");
    element.tier = guest(entry, "tier");
    element.limit = guest(entry, "limit");
    if (const lucent::json::Value *limits = entry.find("limits"); limits != nullptr && limits->array() != nullptr) {
      for (const lucent::json::Value &limit : *limits->array()) {
        if (!limit.integer()) {
          throw std::runtime_error(manifest.string() + ": hud limits holds whole numbers");
        }
        element.limits.push_back(static_cast<uint32_t>(*limit.integer()));
      }
    }
    if (element.kind == Kind::Hearts && element.art.size() < 2) {
      throw std::runtime_error(manifest.string() + ": hud hearts want \"art\", a heart from empty to full");
    }
    element.longer = guest(entry, "longer");
    element.ammo = guest(entry, "ammo");
    element.status = guest(entry, "status");
    element.disabled = integer(entry, "disabled", 0xFF);
    if (const lucent::json::Value *indices = entry.find("ammo_index"); indices != nullptr && indices->array() != nullptr) {
      for (const lucent::json::Value &pair : *indices->array()) {
        if (pair.array() == nullptr || pair.array()->size() != 2 || !(*pair.array())[0].integer() ||
            !(*pair.array())[1].integer()) {
          throw std::runtime_error(manifest.string() + ": hud ammo_index holds [item, index] pairs");
        }
        element.ammoIndex.emplace_back(static_cast<uint32_t>(*(*pair.array())[0].integer()),
                                       static_cast<uint32_t>(*(*pair.array())[1].integer()));
      }
    }
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
  if (value.pointer != 0) {
    const std::optional<uint32_t> base = read({value.pointer, sizeof(uint32_t)});
    return base && *base != 0 ? read({*base + value.address, value.bytes, 0, value.shift, value.mask}) : std::nullopt;
  }
  std::array<uint8_t, sizeof(uint32_t)> bytes{};
  if (value.bytes == 0 || value.bytes > bytes.size() ||
      !emulator_.read(value.address, std::span(bytes).first(value.bytes))) {
    return std::nullopt;
  }
  uint32_t result = 0;
  for (size_t i = 0; i < value.bytes; ++i) {
    result |= static_cast<uint32_t>(bytes[i]) << (8 * i);
  }
  return (result & value.mask) >> value.shift;
}

bool Hud::hold(const std::vector<Condition> &conditions) const {
  return std::ranges::all_of(conditions, [this](const Condition &condition) {
    const std::optional<uint32_t> value = read(condition.value);
    return value && *value >= condition.least && *value <= condition.most;
  });
}

void Hud::update() {
  quads_.clear();
  if (overlay_.hudShown() || (!show_.empty() && hold(show_))) {
    const auto [width, height] = window_.pixelSize();
    if (width != width_ || height != height_) {
      width_ = width;
      height_ = height;
      atlas_->clear();
    }
    for (const Element &element : elements_) {
      if (hold(element.when)) {
        draw(element);
      }
    }
  }
  Settings::values.overlay_image = atlas_->image();
  Settings::values.overlay_quads = quads_;
}

// `sprite` drawn at `at` window pixels.
void Hud::quad(const HudSprite &sprite, const OverlayRect &at, float opacity) {
  if (sprite.w <= 0.0F) {
    return;
  }
  quads_.push_back({sprite.x, sprite.y, sprite.w, sprite.h, at.x, at.y, at.w, at.h, opacity});
}

HudSprite Hud::art(const std::string &name, float width, float height) {
  return atlas_->image(icons_ / (name + ".png"), static_cast<int>(width), static_cast<int>(height));
}

void Hud::number(uint32_t value, int height, HudInk ink, float baseline, float edge, bool fromRight) {
  const HudSprite sprite = atlas_->text(std::to_string(value), height, ink);
  quad(sprite, {fromRight ? edge - sprite.w : edge, baseline - sprite.base, sprite.w, sprite.h});
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
    // The game's parts of a heart: the last art is a whole one, the ones before it equal shares of the rest.
    const auto parts = static_cast<uint32_t>(element.art.size() - 1);
    for (uint32_t i = 0; i < *capacity / element.unit; ++i) {
      const OverlayRect at{box.x + static_cast<float>(i % element.columns) * box.w,
                           box.y + static_cast<float>(i / element.columns) * box.h, box.w, box.h};
      const uint32_t before = i * element.unit;
      const uint32_t held = *health <= before ? 0 : std::min(element.unit, *health - before);
      const uint32_t part =
          held == element.unit || element.unit < 2 ? (held != 0 ? parts : 0)
                                                   : (held * (parts - 1) + element.unit - 2) / (element.unit - 1);
      quad(art(element.art[part], box.w, box.h), at);
    }
    return;
  }
  case Kind::Meter: {
    const std::optional<uint32_t> value = read(element.value);
    const uint32_t lengths = read(element.longer).value_or(0) != 0 ? 2 : 1;
    const uint32_t full = element.full != 0 ? element.full * lengths : read(element.capacity).value_or(0);
    if (!value || full == 0) {
      return;
    }
    box.w = std::round(box.h * kMeterAspect * static_cast<float>(lengths));
    quad(sized(kHudSet, "bar", box.w, box.h), box);
    // The fill's left share.
    const float share = std::min(1.0F, static_cast<float>(*value) / static_cast<float>(full));
    const HudSprite fill = sized(kHudSet, "bar_fill", box.w, box.h);
    if (fill.w > 0.0F && share > 0.0F) {
      quads_.push_back({fill.x, fill.y, fill.w * share, fill.h, box.x, box.y, box.w * share, box.h, 1.0F});
    }
    return;
  }
  case Kind::Counter: {
    const uint32_t index = element.index.bytes != 0 ? read(element.index).value_or(0) : 0;
    const std::optional<uint32_t> value =
        read({element.value.address + index * element.value.bytes, element.value.bytes, element.value.pointer});
    if (!value) {
      return;
    }
    const uint32_t tier = element.tier.bytes != 0 ? read(element.tier).value_or(0) : 0;
    const std::string &icon = element.art.empty() ? element.icon : element.art[std::min<size_t>(tier, element.art.size() - 1)];
    quad(art(icon, box.h, box.h), {box.x, box.y, box.h, box.h});
    std::optional<uint32_t> limit;
    if (element.limit.bytes != 0) {
      limit = read({element.limit.address + tier * element.limit.bytes, element.limit.bytes, element.limit.pointer});
    } else if (!element.limits.empty()) {
      limit = element.limits[std::min<size_t>(tier, element.limits.size() - 1)];
    }
    const float digit = std::round(box.h * kDigitShare);
    number(*value, static_cast<int>(digit), limit && *value >= *limit ? HudInk::Full : HudInk::Light,
           box.y + std::round((box.h + digit) / 2.0F), box.x + box.h + std::round(box.h * kCounterGap), false);
    return;
  }
  case Kind::Button: {
    const bool chord = chordContent(element);
    const GuestValue &status = chord ? element.chordStatus : element.status;
    quad(sized(kHudSet, element.frame, box.w, box.h), box);
    const std::optional<uint32_t> item = read(chord ? element.chordValue : element.value);
    if (item && *item != element.none) {
      const float size = std::round(box.w * kIconShare);
      const float inset = std::round((box.w - size) / 2.0F);
      const bool dimmed = status.bytes != 0 && read(status) == element.disabled;
      quad(art(std::to_string(*item), size, size), {box.x + inset, box.y + inset, size, size}, dimmed ? kDimmed : 1.0F);
      const auto counted = std::ranges::find(element.ammoIndex, *item, &std::pair<uint32_t, uint32_t>::first);
      const std::optional<uint32_t> count =
          counted != element.ammoIndex.end()
              ? read({element.ammo.address + counted->second * element.ammo.bytes, element.ammo.bytes})
              : std::nullopt;
      if (count) {
        // In the button's bottom right corner on a plate, so the frame's rim never shows through it.
        const int digit = static_cast<int>(std::round(box.w * kCountShare));
        const float margin = std::round(box.w * kFrameMargin);
        number(*count, digit, HudInk::Plate, box.y + box.h - margin - static_cast<float>(HudAtlas::outline(digit)),
               box.x + box.w - margin, true);
      }
    }
    if (const std::optional<uint32_t> action = chord ? std::nullopt : read(element.label);
        action && *action < element.labels.size() && !element.labels[*action].empty()) {
      // Centred under the button.
      const int height = static_cast<int>(std::round(box.h * kLabelShare));
      const HudSprite word = atlas_->text(element.labels[*action], height, HudInk::Light);
      const float baseline = box.y + box.h + static_cast<float>(2 * HudAtlas::outline(height) + height);
      quad(word, {std::round(box.x + (box.w - word.w) / 2.0F), baseline - word.base, word.w, word.h});
    }
    prompt(element, box);
    return;
  }
  }
}

bool Hud::chordContent(const Element &element) const {
  return element.chordValue.bytes != 0 && pad_.state().buttons[Settings::NativeButton::Debug];
}

void Hud::prompt(const Element &element, const OverlayRect &box) {
  const bool secondPage = pad_.state().buttons[Settings::NativeButton::Debug];
  const bool keyboard = pad_.lastDevice() == HostDevice::Keyboard;
  // A button keeps its pad glyph; its key is the one that presses what it shows now.
  Prompt by;
  if (element.itemButton && (keyboard || secondPage)) {
    by = pad_.itemPrompt(*element.itemButton, secondPage);
  } else if (element.button) {
    by = PadInput::prompt(*element.button);
    if (secondPage && element.chordItem) {
      by.key = pad_.itemPrompt(*element.chordItem, true).key;
    }
  }
  const float size = std::round(box.w * kGlyphShare);
  const OverlayRect at{box.x - std::round(size / 4.0F), box.y - std::round(size / 4.0F), size, size};
  if (!keyboard) {
    if (by.pad && !glyphOf(*by.pad).empty()) {
      quad(atlas_->svg(kPadSet, std::string(glyphOf(*by.pad)), static_cast<int>(size), static_cast<int>(size)), at);
    }
    return;
  }
  if (!by.key) {
    return;
  }
  quad(atlas_->svg(kKeySet, "cap", static_cast<int>(size), static_cast<int>(size)), at);
  const float capital = std::round(size * kKeyLabelShare);
  const HudSprite label = atlas_->text(SDL_GetScancodeName(*by.key), static_cast<int>(capital), HudInk::Dark);
  quad(label, {at.x + std::round((size - label.w) / 2.0F), at.y + std::round((size + capital) / 2.0F) - label.base, label.w,
               label.h});
}

} // namespace z3d::app
