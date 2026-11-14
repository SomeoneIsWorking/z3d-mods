// What the picker and its screens draw with between games: an SDL renderer on the game window
// and the outline font. The core's Vulkan renderer takes the window after it is gone.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct SDL_FPoint;
struct SDL_FRect;
struct SDL_Renderer;
struct TTF_Font;
struct TTF_Text;
struct TTF_TextEngine;

namespace z3d::app {

class GameWindow;

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};
inline constexpr Rgb kMenuBackground{18, 20, 26};
inline constexpr Rgb kMenuTile{34, 38, 48};
inline constexpr Rgb kMenuSelected{224, 186, 72};
inline constexpr Rgb kMenuText{236, 238, 242};
inline constexpr Rgb kMenuDim{150, 156, 170};

class MenuCanvas {
public:
  MenuCanvas(GameWindow &window, const std::filesystem::path &font);
  ~MenuCanvas();
  MenuCanvas(const MenuCanvas &) = delete;
  MenuCanvas &operator=(const MenuCanvas &) = delete;
  MenuCanvas(MenuCanvas &&) = delete;
  MenuCanvas &operator=(MenuCanvas &&) = delete;

  [[nodiscard]] SDL_Renderer *renderer() const {
    return renderer_;
  }
  [[nodiscard]] TTF_TextEngine *engine() const {
    return engine_;
  }
  [[nodiscard]] TTF_Font *font() const {
    return font_;
  }

  // The output's size in pixels, each at least 1.
  [[nodiscard]] SDL_FPoint size() const;
  void clear() const;
  void fill(Rgb color, const SDL_FRect &rect) const;
  // Text at the font's size now; the caller destroys it.
  [[nodiscard]] TTF_Text *text(const std::string &line, Rgb color) const;

private:
  SDL_Renderer *renderer_ = nullptr;
  TTF_TextEngine *engine_ = nullptr;
  TTF_Font *font_ = nullptr;
};

[[nodiscard]] SDL_FPoint textSize(TTF_Text *text);

} // namespace z3d::app
