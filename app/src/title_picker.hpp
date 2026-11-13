// The in-window title picker: each game's icon and title from the player's dump, chosen with the
// pad (left and right, A or Start) or a click. It owns an SDL renderer on the game window only
// while it runs; the core's Vulkan renderer takes the window after it is gone.
#pragma once

#include "game_library.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
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

class TitlePicker {
public:
  TitlePicker(GameWindow &window, const PadInput &pad, const GameLibrary &library,
              const std::filesystem::path &font);
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
  void fitText(float height);
  void draw(const Layout &layout);
  [[nodiscard]] std::optional<size_t> hit(const Layout &layout, float x, float y) const;

  GameWindow &window_;
  const PadInput &pad_;
  const GameLibrary &library_;
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
