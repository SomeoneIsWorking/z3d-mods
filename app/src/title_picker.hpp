// The in-window title picker: each game's icon and title from the player's dump, chosen with the
// pad (left and right, A or Start) or a click; X opens the selected game's options. A game whose
// set is missing is set up from the player's dump first (set_install.hpp). It owns an SDL
// renderer on the game window only while it runs; the core's Vulkan renderer takes the window
// after it is gone.
#pragma once

#include "game_library.hpp"
#include "game_options.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_FRect;
struct TTF_Font;
struct TTF_Text;
struct TTF_TextEngine;

namespace z3d::app {

class GameWindow;
class PadInput;
class SetInstall;

class TitlePicker {
public:
  // `options` holds one entry per game of the library, in its order.
  TitlePicker(GameWindow &window, const PadInput &pad, const GameLibrary &library,
              std::vector<GameOptions> &options, SetInstall &install, const std::filesystem::path &font);
  ~TitlePicker();
  TitlePicker(const TitlePicker &) = delete;
  TitlePicker &operator=(const TitlePicker &) = delete;
  TitlePicker(TitlePicker &&) = delete;
  TitlePicker &operator=(TitlePicker &&) = delete;

  // The chosen game's index, or nothing when the window closes.
  [[nodiscard]] std::optional<size_t> run();

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

  GameWindow &window_;
  const PadInput &pad_;
  const GameLibrary &library_;
  std::vector<GameOptions> &options_;
  SetInstall &install_;
  SDL_Renderer *renderer_ = nullptr;
  TTF_TextEngine *engine_ = nullptr;
  TTF_Font *font_ = nullptr;
  std::vector<SDL_Texture *> icons_;
  std::vector<TTF_Text *> titles_;
  TTF_Text *heading_ = nullptr;
  TTF_Text *hint_ = nullptr;
  float textHeight_ = 0.0F;
  size_t selected_ = 0;
};

} // namespace z3d::app
