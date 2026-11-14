// The app's own single screen for a game that keeps its HUD and menus on the touch screen: the
// window has the top screen's shape, and pieces of the touch screen are drawn over or beside the
// top screen where the game's layout table puts them for the touch screen that is up: HUD
// elements in play, the whole screen large in a menu, nothing while the game blanks it.
#pragma once

#include "app_config.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lucent::json {
class Value;
}

namespace z3d::app {

class Emulator;
class GameWindow;

// A pixel of the 320x240 touch screen.
struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
};

// A layout is laid out on the top screen's own 400x240 pixels, whatever the window's size.
inline constexpr float kOverlayWidth = 400.0F;
inline constexpr float kOverlayHeight = 240.0F;

struct OverlayRect {
  float x = 0.0F;
  float y = 0.0F;
  float w = 0.0F;
  float h = 0.0F;
};

// A part of the touch screen (`from`, in its pixels) drawn at `to`. `radius` rounds the corners,
// in the piece's own pixels.
struct OverlayPiece {
  OverlayRect from;
  OverlayRect to;
  float radius = 0.0F;
  float opacity = 1.0F;
};

struct OverlayLayout {
  OverlayRect top{0.0F, 0.0F, kOverlayWidth, kOverlayHeight};
  std::vector<OverlayPiece> pieces;
  // Play's view: our HUD (hud.hpp) is drawn over it.
  bool hud = false;
};

// The two screens side by side: the layout named "split", and the one before a game's table is read.
[[nodiscard]] OverlayLayout splitLayout();

// [x, y, width, height] of a manifest; `where` names it in the refusal.
[[nodiscard]] OverlayRect parseOverlayRect(const lucent::json::Value *value, const std::string &where);

// A rectangle of the layout's 400x240 in the pixels of a `width` x `height` window.
[[nodiscard]] OverlayRect overlayWindowRect(unsigned width, unsigned height, const OverlayRect &rect);

// Writes the core's custom layout for a `width` x `height` window.
void placeOverlay(unsigned width, unsigned height, const OverlayLayout &layout);

// The touch screen's pixel drawn at a window pixel, through the topmost piece there.
[[nodiscard]] std::optional<TouchPoint> overlayTouch(unsigned width, unsigned height,
                                                     const OverlayLayout &layout, float x, float y);

class ScreenOverlay {
public:
  // Follows the running game when `setup` asks for the overlay layout and `manifest`
  // (z3d-options.json, "screens") names the game's touch screens and a layout for each.
  ScreenOverlay(GameWindow &window, Emulator &emulator, const GameSetup &setup,
                const std::filesystem::path &manifest);

  // Whether the layout that is up is play's view.
  [[nodiscard]] bool hudShown() const {
    return screens_ && layout_ && screens_->layouts[*layout_].hud;
  }

private:
  struct ScreenPlacement {
    size_t layout = 0;
    // The layout while the game's HUD visibility hides this screen, if it has one.
    std::optional<size_t> hidden;
  };

  struct Screens {
    // Guest addresses of the pointer to the screen that is up and of the table of play's
    // screens, the layout for each of the table's entries and the one outside play.
    uint32_t active = 0;
    uint32_t table = 0;
    std::vector<OverlayLayout> layouts;
    std::vector<ScreenPlacement> placements;
    size_t outside = 0;
    // The game's 16-bit HUD visibility and its values that blank or dim a screen.
    uint32_t hud = 0;
    std::vector<uint16_t> hudHidden;
  };

  [[nodiscard]] size_t layout() const;
  [[nodiscard]] bool hudHidden() const;

  void update();

  GameWindow &window_;
  Emulator &emulator_;
  std::optional<Screens> screens_;
  std::optional<size_t> layout_;
};

} // namespace z3d::app
