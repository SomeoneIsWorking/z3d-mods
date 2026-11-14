// The in-window title picker: each game's icon and title from the player's dump, chosen with the
// pad (left and right, A or Start) or a click; X opens the selected game's options, whose last
// row asks for the games and mods folders again when the picker can change them. A game whose
// set is missing is set up from the player's dump first (set_install.hpp). It draws on the
// canvas it is given (menu_canvas.hpp).
#pragma once

#include "game_library.hpp"
#include "game_options.hpp"
#include "menu_canvas.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SDL_Texture;

namespace z3d::app {

class GameWindow;
class PadInput;
class SetInstall;

class TitlePicker {
public:
  // `options` holds one entry per game of the library, in its order.
  TitlePicker(const MenuCanvas &canvas, GameWindow &window, const PadInput &pad, const GameLibrary &library,
              std::vector<GameOptions> &options, SetInstall &install, bool folders);
  ~TitlePicker();
  TitlePicker(const TitlePicker &) = delete;
  TitlePicker &operator=(const TitlePicker &) = delete;
  TitlePicker(TitlePicker &&) = delete;
  TitlePicker &operator=(TitlePicker &&) = delete;

  struct Picked {
    // The game to play, or nothing when the player asks to change the folders.
    std::optional<size_t> game;
  };
  // What the player picked, or nothing when the window closes.
  [[nodiscard]] std::optional<Picked> run();

private:
  struct Layout;
  [[nodiscard]] Layout layout() const;
  // Logs the game the picker is on, which the input tests wait on.
  void select(size_t index);
  void fitText(float height);
  void draw(const Layout &layout);
  [[nodiscard]] std::optional<size_t> hit(const Layout &layout, float x, float y) const;
  // What the bottom line says about the game the picker is on.
  [[nodiscard]] std::string hint() const;
  // Sets the game up on a screen of its own; false when the set is still missing afterwards.
  [[nodiscard]] bool setUp(size_t index);
  void drawSetUp(const std::string &heading, const std::string &line);

  const MenuCanvas &canvas_;
  SDL_Renderer *renderer_;
  TTF_Font *font_;
  GameWindow &window_;
  const PadInput &pad_;
  const GameLibrary &library_;
  std::vector<GameOptions> &options_;
  SetInstall &install_;
  bool folders_;
  std::vector<SDL_Texture *> icons_;
  std::vector<TTF_Text *> titles_;
  TTF_Text *heading_ = nullptr;
  TTF_Text *hint_ = nullptr;
  float textHeight_ = 0.0F;
  size_t selected_ = 0;
};

} // namespace z3d::app
