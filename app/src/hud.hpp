// Our own play HUD, drawn over the top screen from the game's state: hearts, a meter, counters and
// item buttons with the pad's glyphs. A game is data: the manifest's "hud" (z3d-options.json,
// from patch/<game>/hud.toml) places the elements and says which guest memory each reads.
#pragma once

#include "hud_atlas.hpp"
#include "pad_input.hpp"
#include "screen_overlay.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace z3d::app {

class Emulator;
class GameWindow;

class Hud {
public:
  // Draws nothing without `assets` (a port-assets copy) or without "hud" in `manifest`.
  Hud(GameWindow &window, Emulator &emulator, const PadInput &pad, const ScreenOverlay &overlay,
      const std::filesystem::path &assets, const std::filesystem::path &manifest);
  ~Hud();
  Hud(const Hud &) = delete;
  Hud &operator=(const Hud &) = delete;
  Hud(Hud &&) = delete;
  Hud &operator=(Hud &&) = delete;

private:
  // A little-endian unsigned value in guest memory; `bytes` 0 is no value.
  struct GuestValue {
    uint32_t address = 0;
    uint8_t bytes = 0;
  };

  enum class Kind : uint8_t { Hearts, Meter, Counter, Button };

  // Positions and sizes are in the top screen's 400x240 pixels.
  struct Element {
    Kind kind = Kind::Hearts;
    OverlayRect rect;
    GuestValue value;
    // Hearts: the health all hearts hold. Meter: what a full bar holds, unless `full` says.
    GuestValue capacity;
    // Hearts: the health in one heart, and hearts in a row. Meter: a full bar.
    uint32_t unit = 0;
    uint32_t columns = 0;
    uint32_t full = 0;
    // Meter: drawn only while this reads nonzero (if set), and twice as long and as full while
    // `longer` does.
    GuestValue shown;
    GuestValue longer;
    // Counter: the port-assets hud glyph before the number.
    std::string icon;
    // Button: its frame (hud set), the 3DS button that presses it (if one does), the item
    // button it is (if it is one), and the item value that means empty.
    std::string frame;
    std::optional<Settings::NativeButton::Values> button;
    std::optional<ItemButton> itemButton;
    uint32_t none = 0;
    // Button: the game's array of counts, the index in it of each item that has one, and the
    // button's status with the value that dims it.
    GuestValue ammo;
    std::vector<std::pair<uint32_t, uint32_t>> ammoIndex;
    GuestValue status;
    uint32_t disabled = 0;
  };

  [[nodiscard]] std::optional<uint32_t> read(const GuestValue &value) const;
  void update();
  void draw(const Element &element);
  void quad(const HudSprite &sprite, const OverlayRect &at, float share = 1.0F, float opacity = 1.0F);
  // The pad glyph or key cap of what presses a button element, on its top left corner.
  void prompt(const Element &element, const OverlayRect &box);

  GameWindow &window_;
  Emulator &emulator_;
  const PadInput &pad_;
  const ScreenOverlay &overlay_;
  std::optional<HudAtlas> atlas_;
  std::vector<Element> elements_;
  // <icons>/<item id>.png: the game's or the pack's icon of each item.
  std::filesystem::path icons_;
  unsigned width_ = 0;
  unsigned height_ = 0;
  std::vector<Settings::OverlayQuad> quads_;
};

} // namespace z3d::app
