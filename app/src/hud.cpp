#include "hud.hpp"

#include "emulator.hpp"
#include "game_window.hpp"
#include "prompt_art.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
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
// A key's capital against its cap.
constexpr float kKeyLabelShare = 0.45F;
// A word on a cap against a single capital there.
constexpr float kWordShare = 0.7F;
// Shares of a button's size: the item icon in its middle and the glyph on its top left corner.
constexpr float kIconShare = 0.68F;
constexpr float kGlyphShare = 0.46F;
// The smallest glyph that still reads, in top-screen pixels.
constexpr float kGlyphLeast = 11.0F;
// An item's count against its button, and a disabled item's opacity.
constexpr float kCountShare = 0.3F;
constexpr float kDimmed = 0.4F;
// A counter's digits against its icon's height, and the gap before them.
constexpr float kDigitShare = 0.62F;
constexpr float kCounterGap = 0.3F;
// A meter's art is this many times as long as it is tall (port-assets hud/bar).
constexpr float kMeterAspect = 9.0F;
// The game's word art for an action is <kWordArt><number>; its cell's height against the button.
constexpr const char *kWordArt = "word_";
constexpr float kLabelShare = 0.5F;

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

// What stands for the chord on the keyboard.
constexpr const char *kChordKey = "Shift";
// A song's note (hud set), white for the song's colour to tint.
constexpr const char *kNote = "note";

std::optional<Verb> verbNamed(const std::string &name) {
  for (const auto &[text, verb] : kVerbNames) {
    if (text == name) {
      return verb;
    }
  }
  return std::nullopt;
}

} // namespace

Hud::Hud(GameWindow &window, Emulator &emulator, const PadInput &pad, const SlotLink &link, const ScreenOverlay &overlay,
         const std::filesystem::path &assets, const std::filesystem::path &manifest)
    : window_(window), emulator_(emulator), pad_(pad), link_(link), overlay_(overlay) {
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
  ammo_ = guest(*hud, "ammo");
  status_ = guest(*hud, "status");
  disabled_ = integer(*hud, "disabled", 0xFF);
  if (const lucent::json::Value *indices = hud->find("ammo_index"); indices != nullptr && indices->array() != nullptr) {
    for (const lucent::json::Value &pair : *indices->array()) {
      if (pair.array() == nullptr || pair.array()->size() != 2 || !(*pair.array())[0].integer() ||
          !(*pair.array())[1].integer()) {
        throw std::runtime_error(manifest.string() + ": hud ammo_index holds [item, index] pairs");
      }
      ammoIndex_.emplace_back(static_cast<uint32_t>(*(*pair.array())[0].integer()),
                              static_cast<uint32_t>(*(*pair.array())[1].integer()));
    }
  }
  if (const lucent::json::Value *colours = hud->find("song_colours"); colours != nullptr && colours->array() != nullptr) {
    for (const lucent::json::Value &colour : *colours->array()) {
      songColours_.push_back(colour.integer() ? static_cast<uint32_t>(*colour.integer()) : HudAtlas::kNoTint);
    }
  }
  const auto slotOf = [&](const lucent::json::Value &object, std::string_view key) -> std::optional<size_t> {
    if (object.find(key) == nullptr) {
      return std::nullopt;
    }
    const uint32_t slot = integer(object, key, 0);
    if (slot >= kSlotCount) {
      throw std::runtime_error(manifest.string() + ": hud \"" + std::string(key) + "\" is a slot below " +
                               std::to_string(kSlotCount));
    }
    return slot;
  };
  constexpr std::array kKinds{std::pair{std::string_view("hearts"), Kind::Hearts},
                              std::pair{std::string_view("meter"), Kind::Meter},
                              std::pair{std::string_view("counter"), Kind::Counter},
                              std::pair{std::string_view("button"), Kind::Button},
                              std::pair{std::string_view("prompt"), Kind::Prompt},
                              std::pair{std::string_view("panel"), Kind::Panel}};
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
    element.slot = slotOf(entry, "slot");
    element.chordSlot = slotOf(entry, "chord_slot");
    element.verb = verbNamed(text(entry, "verb"));
    if (const lucent::json::Value *chord = entry.find("chord"); chord != nullptr && chord->boolean() != nullptr) {
      element.chord = *chord->boolean();
    }
    if (const lucent::json::Value *assign = entry.find("assign"); assign != nullptr && assign->boolean() != nullptr) {
      element.assign = *assign->boolean();
    }
    if (const lucent::json::Value *bare = entry.find("bare"); bare != nullptr && bare->boolean() != nullptr) {
      element.bare = *bare->boolean();
    }
    element.glyph = text(entry, "glyph");
    element.when = conditions(entry, "when");
    element.index = guest(entry, "index");
    element.label = guest(entry, "label");
    element.art = strings(entry, "art");
    element.tier = guest(entry, "tier");
    element.limit = guest(entry, "limit");
    if (element.kind == Kind::Hearts && element.art.size() < 2) {
      throw std::runtime_error(manifest.string() + ": hud hearts want \"art\", a heart from empty to full");
    }
    element.longer = guest(entry, "longer");
    element.status = guest(entry, "status");
    elements_.push_back(std::move(element));
  }
  icons_ = manifest.parent_path() / text(*hud, "icons");
  atlas_.emplace(assets);
  requireArt();
  emulator_.onFrame([this] {
    update();
  });
}

// Everything the elements name is read once here, so a frame never finds art missing. The game's
// art that depends on its state (an item's icon, an action's word) is what the install cut.
void Hud::requireArt() {
  std::error_code error;
  for (const std::filesystem::directory_entry &file : std::filesystem::directory_iterator(icons_, error)) {
    if (file.path().extension() == ".png") {
      HudAtlas::requireImage(file.path());
      art_.insert(file.path().stem().string());
    }
  }
  if (error) {
    throw std::runtime_error("the HUD's game art is not installed: " + icons_.string() + " (" + error.message() +
                             "); install the game's set again");
  }
  const auto piece = [this](const std::string &name) {
    if (!art_.contains(name)) {
      HudAtlas::requireImage(icons_ / (name + ".png"));
    }
  };
  bool buttons = false;
  for (const Element &element : elements_) {
    std::ranges::for_each(element.art, piece);
    switch (element.kind) {
    case Kind::Hearts:
      break;
    case Kind::Meter:
      atlas_->requireSvg(kHudSet, "bar");
      atlas_->requireSvg(kHudSet, "bar_fill");
      break;
    case Kind::Counter:
      if (element.art.empty()) {
        piece(element.icon);
      }
      break;
    case Kind::Button:
      buttons = true;
      [[fallthrough]];
    case Kind::Panel:
      atlas_->requireSvg(kHudSet, element.frame);
      break;
    case Kind::Prompt:
      buttons = true;
      if (!element.glyph.empty()) {
        atlas_->requireSvg(kPadSet, element.glyph);
      }
      break;
    }
  }
  if (buttons) {
    atlas_->requireSvg(kHudSet, kNote);
    atlas_->requireSvg(kKeySet, kKeyCap);
    atlas_->requireSvg(kKeySet, kWideCap);
    for (const auto &[pad, glyph] : kPadGlyphs) {
      atlas_->requireSvg(kPadSet, std::string(glyph));
    }
    for (const auto &[mouse, cap] : kMouseCaps) {
      atlas_->requireSvg(kKeySet, std::string(cap));
    }
  }
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
  // A screen that assigns slots shows the slots; play shows the rest.
  const bool assign = link_.mode() == PadMode::Assign;
  if (assign || overlay_.hudShown() || (!show_.empty() && hold(show_))) {
    const auto [width, height] = window_.pixelSize();
    if (width != width_ || height != height_) {
      width_ = width;
      height_ = height;
      atlas_->clear();
    }
    for (const Element &element : elements_) {
      if (element.assign == assign && hold(element.when)) {
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
    }
    const float digit = std::round(box.h * kDigitShare);
    const HudSprite number = atlas_->text(std::to_string(*value), static_cast<int>(digit),
                                          limit && *value >= *limit ? HudInk::Full : HudInk::Light);
    quad(number, {box.x + box.h + std::round(box.h * kCounterGap),
                  box.y + std::round((box.h + digit) / 2.0F) - number.base, number.w, number.h});
    return;
  }
  case Kind::Button:
    button(element, box);
    return;
  case Kind::Panel:
    quad(sized(kHudSet, element.frame, box.w, box.h), box);
    return;
  case Kind::Prompt: {
    if (!element.glyph.empty()) {
      if (pad_.lastDevice() != HostDevice::Keyboard) {
        quad(sized(kPadSet, element.glyph, box.w, box.h), box);
      }
      return;
    }
    Prompt by = element.verb ? PadInput::verbPrompt(*element.verb) : Prompt{};
    if (element.chord) {
      by.pad = PadInput::kChord;
    }
    if (pad_.lastDevice() == HostDevice::Keyboard && element.chord) {
      // The chord's key is a word on a wide cap.
      const float width = std::round(box.h * kWideCapAspect);
      quad(sized(kKeySet, kWideCap, width, box.h), {box.x, box.y, width, box.h});
      const float capital = std::round(box.h * kKeyLabelShare * kWordShare);
      const HudSprite word = atlas_->text(kChordKey, static_cast<int>(capital), HudInk::Dark);
      quad(word, {box.x + std::round((width - word.w) / 2.0F), box.y + std::round((box.h + capital) / 2.0F) - word.base,
                  word.w, word.h});
      return;
    }
    prompt(by, box.x, box.y, box.h);
    return;
  }
  }
}

void Hud::button(const Element &element, const OverlayRect &box) {
  quad(atlas_->svg(kHudSet, element.frame, static_cast<int>(box.w), static_cast<int>(box.h)), box);
  // What it shows: a slot's content (the chord's slot while that is held), else the item it reads.
  const std::optional<size_t> slot = pad_.chordHeld() && element.chordSlot && !element.assign ? element.chordSlot : element.slot;
  if (slot) {
    const SlotLink::Slot &held = link_.slot(*slot);
    content(box, held.item, held.song, held.lane);
  } else {
    const bool dimmed = element.status.bytes != 0 && read(element.status) == disabled_;
    if (const std::optional<uint32_t> item = read(element.value); item && art_.contains(std::to_string(*item))) {
      const float size = std::round(box.w * kIconShare);
      const float inset = std::round((box.w - size) / 2.0F);
      quad(art(std::to_string(*item), size, size), {box.x + inset, box.y + inset, size, size}, dimmed ? kDimmed : 1.0F);
    }
    if (const std::optional<uint32_t> action = read(element.label);
        action && art_.contains(kWordArt + std::to_string(*action))) {
      // The game's word for the action, centred under the button.
      const float height = std::round(box.h * kLabelShare);
      const HudSprite word = art(kWordArt + std::to_string(*action), 0.0F, height);
      quad(word, {std::round(box.x + (box.w - word.w) / 2.0F), box.y + box.h, word.w, word.h});
    }
  }
  if (element.bare && pad_.lastDevice() != HostDevice::Keyboard) {
    return;
  }
  const float size = std::max(std::round(box.w * kGlyphShare), kGlyphLeast);
  const Prompt by = slot ? PadInput::slotPrompt(*slot) : element.verb ? PadInput::verbPrompt(*element.verb) : Prompt{};
  prompt(by, box.x - std::round(size / 4.0F), box.y - std::round(size / 4.0F), size);
}

void Hud::content(const OverlayRect &box, std::optional<uint32_t> item, std::optional<uint8_t> song, uint8_t lane) {
  const float size = std::round(box.w * kIconShare);
  const float inset = std::round((box.w - size) / 2.0F);
  const OverlayRect at{box.x + inset, box.y + inset, size, size};
  if (song) {
    const uint32_t colour = *song < songColours_.size() ? songColours_[*song] : HudAtlas::kNoTint;
    quad(atlas_->svg(kHudSet, kNote, static_cast<int>(size), static_cast<int>(size), colour), at);
    return;
  }
  if (!item || !art_.contains(std::to_string(*item))) {
    return;
  }
  // The game keeps a status for the items on its own buttons only.
  const bool dimmed = lane != 0 && status_.bytes != 0 &&
                      read({status_.address + lane * status_.bytes, status_.bytes, status_.pointer}) == disabled_;
  quad(art(std::to_string(*item), size, size), at, dimmed ? kDimmed : 1.0F);
  const auto counted = std::ranges::find(ammoIndex_, *item, &std::pair<uint32_t, uint32_t>::first);
  const std::optional<uint32_t> count =
      counted != ammoIndex_.end() ? read({ammo_.address + counted->second * ammo_.bytes, ammo_.bytes}) : std::nullopt;
  if (count) {
    // A plate hanging off the ring's bottom, centred: it starts where the icon ends and hides the rim.
    const HudSprite plate =
        atlas_->text(std::to_string(*count), static_cast<int>(std::round(box.w * kCountShare)), HudInk::Plate);
    quad(plate, {box.x + std::round((box.w - plate.w) / 2.0F), box.y + inset + size, plate.w, plate.h});
  }
}

void Hud::prompt(const Prompt &by, float x, float y, float size) {
  const OverlayRect at{x, y, size, size};
  const auto sized = [&](const char *set, std::string_view name) {
    return atlas_->svg(set, std::string(name), static_cast<int>(size), static_cast<int>(size));
  };
  if (pad_.lastDevice() != HostDevice::Keyboard) {
    if (by.pad && !glyphOf(*by.pad).empty()) {
      quad(sized(kPadSet, glyphOf(*by.pad)), at);
    }
    return;
  }
  if (by.mouse) {
    if (const auto *cap = std::ranges::find(kMouseCaps, *by.mouse, &std::pair<uint8_t, std::string_view>::first);
        cap != kMouseCaps.end()) {
      quad(sized(kKeySet, cap->second), at);
    }
    return;
  }
  if (!by.key) {
    return;
  }
  quad(sized(kKeySet, kKeyCap), at);
  const float capital = std::round(size * kKeyLabelShare);
  const HudSprite label = atlas_->text(SDL_GetScancodeName(*by.key), static_cast<int>(capital), HudInk::Dark);
  quad(label, {at.x + std::round((size - label.w) / 2.0F), at.y + std::round((size + capital) / 2.0F) - label.base, label.w,
               label.h});
}

} // namespace z3d::app
