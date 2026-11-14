// The options screen of one game, drawn with the title picker's renderer: up and down pick an
// option, left, right or A change it, B, X, F10 or Guide go back. Changes are saved as they are made.
// With `folders`, a last row leaves to change the games and mods folders (folder_setup.hpp).
#pragma once

#include <cstddef>
#include <string>

struct SDL_Renderer;
struct TTF_Font;
struct TTF_TextEngine;

namespace z3d::app {

class GameOptions;
class GameWindow;
class PadInput;

class OptionsMenu {
public:
  OptionsMenu(SDL_Renderer *renderer, TTF_TextEngine *engine, TTF_Font *font, GameWindow &window,
              const PadInput &pad, GameOptions &options, std::string title, bool folders)
      : renderer_(renderer), engine_(engine), font_(font), window_(window), pad_(pad),
        options_(options), title_(std::move(title)), folders_(folders) {
  }

  enum class Left { Back, ForFolders };
  // Until the player goes back, asks for the folders, or the window closes.
  [[nodiscard]] Left run();

private:
  // Each logs the menu's new state, which the input tests wait on.
  void select(size_t index);
  [[nodiscard]] size_t rows() const;
  [[nodiscard]] bool onFolders() const;
  void step(int direction);
  void draw();
  void text(const std::string &line, float x, float y, bool selected, bool rightAligned);

  SDL_Renderer *renderer_;
  TTF_TextEngine *engine_;
  TTF_Font *font_;
  GameWindow &window_;
  const PadInput &pad_;
  GameOptions &options_;
  std::string title_;
  bool folders_;
  size_t selected_ = 0;
};

} // namespace z3d::app
