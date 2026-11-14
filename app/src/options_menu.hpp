// The options screen of one game, drawn with the title picker's renderer: up and down pick an
// option, left, right or A change it, B, X or Escape go back. Changes are saved as they are made.
#pragma once

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
              const PadInput &pad, GameOptions &options, std::string title)
      : renderer_(renderer), engine_(engine), font_(font), window_(window), pad_(pad),
        options_(options), title_(std::move(title)) {
  }

  // Until the player goes back or the window closes.
  void run();

private:
  // Each logs the menu's new state, which the input tests wait on.
  void select(size_t index);
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
  size_t selected_ = 0;
};

} // namespace z3d::app
