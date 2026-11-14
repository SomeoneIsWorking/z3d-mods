// The app's own single screen for a game that keeps its HUD and menus on the touch screen: the
// top screen fills the window and the touch screen lies over it, small in a corner while the
// game's main touch screen is up and large while one of its menus is. Outside play (title, file
// select) and for a screen that uses both (the map) the two sit side by side, and a blanked touch
// screen is left out.
#pragma once

#include "app_config.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace z3d::app {

class Emulator;
class GameWindow;

// TopOnly leaves the touch screen out: the game blanks or dims it (cutscenes).
enum class OverlayPlacement : uint8_t { Split, Inset, Menu, TopOnly };

// Writes the core's custom layout for a `width` x `height` window.
void placeOverlay(unsigned width, unsigned height, OverlayPlacement placement);

class ScreenOverlay {
public:
  // Follows the running game when `setup` asks for the overlay layout and `manifest`
  // (z3d-options.json, "screens") names where the game keeps its touch screen.
  ScreenOverlay(GameWindow &window, Emulator &emulator, const GameSetup &setup,
                const std::filesystem::path &manifest);

private:
  struct ScreenPlacement {
    OverlayPlacement placement = OverlayPlacement::Split;
    // The main screen: left out while the game's HUD visibility hides it.
    bool followsHud = false;
  };

  struct Screens {
    // Guest addresses of the pointer to the screen that is up and of the table of play's
    // screens, and where the overlay puts the touch screen for each of the table's entries.
    uint32_t active = 0;
    uint32_t table = 0;
    std::vector<ScreenPlacement> placements;
    // The game's 16-bit HUD visibility and its values that blank or dim the main screen.
    uint32_t hud = 0;
    std::vector<uint16_t> hudHidden;
  };

  [[nodiscard]] OverlayPlacement placement() const;
  [[nodiscard]] bool hudHidden() const;

  void update();

  GameWindow &window_;
  Emulator &emulator_;
  std::optional<Screens> screens_;
  OverlayPlacement placement_ = OverlayPlacement::Split;
};

} // namespace z3d::app
