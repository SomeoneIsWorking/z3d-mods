// Our own play HUD, drawn over the top screen from the game's state: hearts, a meter, counters and
// item buttons with the pad's glyphs. A game is data: the manifest's "hud" (z3d-options.json,
// from patch/<game>/hud.toml) places the elements and says which guest memory each reads. The
// game's own text prompts follow the same device: the texture pack's keyboard variant.
#pragma once

#include "hud_atlas.hpp"
#include "pad_input.hpp"
#include "screen_overlay.hpp"
#include "slot_link.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace z3d::app {

class Emulator;
class GameWindow;

class Hud {
public:
  // Draws nothing without `assets` (a port-assets copy) or without "hud" in `manifest`.
  Hud(GameWindow &window, Emulator &emulator, const PadInput &pad, const SlotLink &link, const ScreenOverlay &overlay,
      const std::filesystem::path &assets, const std::filesystem::path &manifest);
  ~Hud();
  Hud(const Hud &) = delete;
  Hud &operator=(const Hud &) = delete;
  Hud(Hud &&) = delete;
  Hud &operator=(Hud &&) = delete;

private:
  // A little-endian unsigned value in guest memory; `bytes` 0 is no value. With `pointer`, the
  // address is an offset from what that guest pointer holds. A bit field is masked, then shifted down.
  struct GuestValue {
    uint32_t address = 0;
    uint8_t bytes = 0;
    uint32_t pointer = 0;
    uint8_t shift = 0;
    uint32_t mask = UINT32_MAX;
  };

  // Holds while the value reads `least` to `most`.
  struct Condition {
    GuestValue value;
    uint32_t least = 0;
    uint32_t most = 0;
  };

  enum class Kind : uint8_t { Hearts, Meter, Counter, Button, Prompt, Panel };

  // Positions and sizes are in the top screen's 400x240 pixels.
  struct Element {
    Kind kind = Kind::Hearts;
    OverlayRect rect;
    GuestValue value;
    // Drawn only while all of these hold.
    std::vector<Condition> when;
    // Hearts: the health all hearts hold. Meter: what a full bar holds, unless `full` says.
    GuestValue capacity;
    // Hearts: the health in one heart, and hearts in a row. Meter: a full bar.
    uint32_t unit = 0;
    uint32_t columns = 0;
    uint32_t full = 0;
    // Meter: twice as long while this reads nonzero, and `full` twice as much.
    GuestValue longer;
    // Counter: the game's art before the number, and which of the game's array of values to
    // read (if it is one).
    std::string icon;
    GuestValue index;
    // Hearts: the art of a heart from empty to full. Counter: the art for each `tier`, where it
    // changes with one (a wallet's size).
    std::vector<std::string> art;
    GuestValue tier;
    // Counter: the most it holds, drawn in the full colour: the game's table by `tier`.
    GuestValue limit;
    // Button and Panel: the frame (hud set). A button shows an item slot's content (`slot`), or
    // the item `value` reads, under the prompt of its slot or of the verb that presses it; while
    // the chord is held it shows `chordSlot` instead, where it has one. An item without an icon
    // is an empty button.
    std::string frame;
    std::optional<size_t> slot;
    std::optional<size_t> chordSlot;
    std::optional<Verb> verb;
    // Button: the game's number of the action it does now; its word is the game's art. The
    // status of the button whose item `value` reads.
    GuestValue label;
    GuestValue status;
    // Prompt: the chord's own glyph, on its own; or a pad glyph by name, for a pad only.
    bool chord = false;
    std::string glyph;
    // Button: no pad glyph of its own, where a shared one beside it says which it is.
    bool bare = false;
    // Drawn while a screen that assigns slots is up, instead of in play.
    bool assign = false;
  };

  void requireArt();

  [[nodiscard]] std::optional<uint32_t> read(const GuestValue &value) const;
  [[nodiscard]] bool hold(const std::vector<Condition> &conditions) const;
  void update();
  void draw(const Element &element);
  void quad(const HudSprite &sprite, const OverlayRect &at, float opacity = 1.0F);
  // The game's art of that name, scaled to the size.
  [[nodiscard]] HudSprite art(const std::string &name, float width, float height);
  // A button element with what it shows now.
  void button(const Element &element, const OverlayRect &box);
  // An item's icon in a button's box, its count on a plate below; a song's note.
  void content(const OverlayRect &box, std::optional<uint32_t> item, std::optional<uint8_t> song, uint8_t lane);
  // The pad glyph or key cap of `by`, `size` pixels, its top left corner at (x, y).
  void prompt(const Prompt &by, float x, float y, float size);

  GameWindow &window_;
  Emulator &emulator_;
  const PadInput &pad_;
  const SlotLink &link_;
  const ScreenOverlay &overlay_;
  std::optional<HudAtlas> atlas_;
  std::vector<Element> elements_;
  // A game without an overlay table (screen_overlay.hpp) is in play while all of these hold.
  std::vector<Condition> show_;
  // The game's array of counts and the index in it of each item that has one; the game's array
  // of button statuses, by the game's button index, and the value that dims an item.
  GuestValue ammo_;
  std::vector<std::pair<uint32_t, uint32_t>> ammoIndex_;
  GuestValue status_;
  uint32_t disabled_ = 0;
  // A song's colour by its number, 0xRRGGBB.
  std::vector<uint32_t> songColours_;
  // <icons>/<name>.png: the game's or the pack's art, an item's icon by its id; `art_` names
  // what is there.
  std::filesystem::path icons_;
  std::set<std::string> art_;
  unsigned width_ = 0;
  unsigned height_ = 0;
  std::vector<Settings::OverlayQuad> quads_;
};

} // namespace z3d::app
