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
// A key's letters against its cap.
constexpr float kKeyLabelShare = 0.6F;
// Shares of a button's size: the item icon in its middle and the glyph on its top left corner.
constexpr float kIconShare = 0.68F;
constexpr float kGlyphShare = 0.46F;
// An item's count against its button, and a disabled item's opacity.
constexpr float kCountShare = 0.36F;
constexpr float kDimmed = 0.4F;
// A counter's digits against its icon's height, and the gap before them.
constexpr float kDigitShare = 0.8F;
constexpr float kCounterGap = 0.25F;
// A meter's art is this many times as long as it is tall (port-assets hud/bar).
constexpr float kMeterAspect = 9.0F;
// A button's action word against its size.
constexpr float kLabelShare = 0.4F;

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
                      integer(*value, "pointer", 0)};
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
    element.label = guest(entry, "label");
    if (const lucent::json::Value *labels = entry.find("labels"); labels != nullptr && labels->array() != nullptr) {
      for (const lucent::json::Value &label : *labels->array()) {
        element.labels.push_back(label.string() != nullptr ? *label.string() : std::string());
      }
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
    return base && *base != 0 ? read({*base + value.address, value.bytes}) : std::nullopt;
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
  return result;
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

// `sprite`, or its left `share`, drawn at `at` window pixels.
void Hud::quad(const HudSprite &sprite, const OverlayRect &at, float share, float opacity) {
  if (sprite.w <= 0.0F || share <= 0.0F) {
    return;
  }
  quads_.push_back({sprite.x, sprite.y, sprite.w * share, sprite.h, at.x, at.y, at.w * share, at.h, opacity});
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
    const uint32_t lengths = read(element.longer).value_or(0) != 0 ? 2 : 1;
    const uint32_t full = element.full != 0 ? element.full * lengths : read(element.capacity).value_or(0);
    if (!value || full == 0) {
      return;
    }
    box.w = std::round(box.h * kMeterAspect * static_cast<float>(lengths));
    quad(sized(kHudSet, "bar", box.w, box.h), box);
    quad(sized(kHudSet, "bar_fill", box.w, box.h), box,
         std::min(1.0F, static_cast<float>(*value) / static_cast<float>(full)));
    return;
  }
  case Kind::Counter: {
    const uint32_t index = element.index.bytes != 0 ? read(element.index).value_or(0) : 0;
    const std::optional<uint32_t> value =
        read({element.value.address + index * element.value.bytes, element.value.bytes, element.value.pointer});
    if (!value) {
      return;
    }
    quad(sized(kHudSet, element.icon, box.h, box.h), {box.x, box.y, box.h, box.h});
    const float digit = std::round(box.h * kDigitShare);
    float x = box.x + box.h + std::round(box.h * kCounterGap);
    for (const char character : std::to_string(*value)) {
      const HudSprite sprite = atlas_->text(std::string(1, character), static_cast<int>(digit), false);
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
      const bool dimmed = element.status.bytes != 0 && read(element.status) == element.disabled;
      quad(atlas_->image(icons_ / (std::to_string(*item) + ".png"), static_cast<int>(size), static_cast<int>(size)),
           {box.x + inset, box.y + inset, size, size}, 1.0F, dimmed ? kDimmed : 1.0F);
      const auto counted = std::ranges::find(element.ammoIndex, *item, &std::pair<uint32_t, uint32_t>::first);
      const std::optional<uint32_t> count =
          counted != element.ammoIndex.end()
              ? read({element.ammo.address + counted->second * element.ammo.bytes, element.ammo.bytes})
              : std::nullopt;
      if (count) {
        // Right-aligned on the button's bottom edge.
        const float digit = std::round(box.w * kCountShare);
        float x = box.x + box.w;
        const std::string digits = std::to_string(*count);
        for (auto character = digits.rbegin(); character != digits.rend(); ++character) {
          const HudSprite sprite = atlas_->text(std::string(1, *character), static_cast<int>(digit), false);
          x -= sprite.w;
          quad(sprite, {x, box.y + box.h - sprite.h, sprite.w, sprite.h});
        }
      }
    }
    if (const std::optional<uint32_t> action = read(element.label);
        action && *action < element.labels.size() && !element.labels[*action].empty()) {
      // Centred under the button.
      const HudSprite word = atlas_->text(element.labels[*action], static_cast<int>(std::round(box.h * kLabelShare)), false);
      quad(word, {std::round(box.x + (box.w - word.w) / 2.0F), box.y + box.h, word.w, word.h});
    }
    prompt(element, box);
    return;
  }
  }
}

void Hud::prompt(const Element &element, const OverlayRect &box) {
  const bool secondPage = pad_.state().buttons[Settings::NativeButton::Debug];
  const bool keyboard = pad_.lastDevice() == HostDevice::Keyboard;
  Prompt by;
  if (element.itemButton && (keyboard || secondPage)) {
    by = pad_.itemPrompt(*element.itemButton, secondPage);
  } else if (element.button) {
    by = PadInput::prompt(*element.button);
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
  const HudSprite label = atlas_->text(SDL_GetScancodeName(*by.key), static_cast<int>(std::round(size * kKeyLabelShare)), true);
  quad(label, {at.x + std::round((size - label.w) / 2.0F), at.y + std::round((size - label.h) / 2.0F), label.w, label.h});
}

} // namespace z3d::app
