// The in-window title picker, a stage of the two games running side by side: a slanted edge
// splits the window, the selected game's side is the wider one and only that game runs; the other
// is held where it stopped and shown grey. Left and right (or a click on the other side) move the
// edge across; A, Start or a click on the selected side picks it, the edge slides out of the
// window, the picked game plays on and the other is unloaded. X opens the selected game's options,
// whose last row asks for the games and mods folders again when the picker can change them. A
// game whose set is missing shows its icon and is set up from the player's dump when picked
// (set_install.hpp). It draws on the canvas it is given (menu_canvas.hpp); the games render off
// the window meanwhile and are shown from their frames.
#pragma once

#include "app_config.hpp"
#include "emulator.hpp"
#include "game_library.hpp"
#include "game_options.hpp"
#include "menu_canvas.hpp"

#include <SDL3/SDL_rect.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct SDL_Texture;

namespace z3d::app {

class GameWindow;
class PadInput;
class SetInstall;

class TitlePicker {
public:
  // `options` holds one entry per game of the library, in its order; the library has one or two.
  TitlePicker(const AppConfig &config, const MenuCanvas &canvas, GameWindow &window, PadInput &pad,
              const GameLibrary &library, std::vector<GameOptions> &options, SetInstall &install,
              bool folders);
  ~TitlePicker();
  TitlePicker(const TitlePicker &) = delete;
  TitlePicker &operator=(const TitlePicker &) = delete;
  TitlePicker(TitlePicker &&) = delete;
  TitlePicker &operator=(TitlePicker &&) = delete;

  struct Picked {
    // The game to play, or nothing when the player asks to change the folders.
    std::optional<size_t> game;
    // The picked game as it ran on the stage; null when it was set up just now.
    std::unique_ptr<Emulator> emulator;
  };
  // What the player picked, or nothing when the window closes.
  [[nodiscard]] std::optional<Picked> run();

private:
  struct Side {
    std::unique_ptr<Emulator> emulator;
    SDL_Texture *icon = nullptr;
    // Its last frame as it runs, and grey while it is held.
    SDL_Texture *frame = nullptr;
    SDL_Texture *grey = nullptr;
    TTF_Text *title = nullptr;
  };
  // The window's size and where the edge crosses its top and bottom.
  struct Stage {
    float width;
    float height;
    float top;
    float bottom;
  };

  [[nodiscard]] Stage stage() const;
  // Loads a game that is set up into a system of its own, held before its first frame.
  void load(size_t index);
  // Runs the selected game a frame and takes the frame.
  void step();
  // Logs the game the picker is on, which the input tests wait on; the one left greys.
  void select(size_t index);
  // Where the edge belongs: the selected side wide, or past the window's side once picked.
  [[nodiscard]] float edgeTarget(const Stage &stage) const;
  // Moves the edge toward where it belongs; true once a picked game fills the window.
  bool slide(const Stage &stage);
  void fitText(float height);
  void draw(const Stage &stage);
  // A game's side: its picture, or its title over the band.
  void drawSide(const Stage &stage, size_t index, bool title);
  [[nodiscard]] size_t sideAt(const Stage &stage, SDL_FPoint at) const;
  // What the bottom line says about the game the picker is on.
  [[nodiscard]] std::vector<Hint> hints() const;
  // A line of hints at the bottom, for the device the player used last.
  void hintLine(const Stage &stage, std::span<const Hint> line) const;
  // Sets the game up on a screen of its own; false when the set is still missing afterwards.
  [[nodiscard]] bool setUp(size_t index);
  void drawSetUp(const std::string &heading, const std::string &line, std::span<const Hint> below);

  const AppConfig &config_;
  const MenuCanvas &canvas_;
  SDL_Renderer *renderer_;
  TTF_Font *font_;
  GameWindow &window_;
  PadInput &pad_;
  const GameLibrary &library_;
  std::vector<GameOptions> &options_;
  SetInstall &install_;
  bool folders_;
  std::vector<Side> sides_;
  float textHeight_ = 0.0F;
  size_t selected_ = 0;
  // The edge's middle as a share of the window's width, and when it last moved.
  float edge_ = 0.0F;
  uint64_t edgeTicks_ = 0;
  bool picking_ = false;
};

} // namespace z3d::app
