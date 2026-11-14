// What the picker and its screens draw with between games: an SDL renderer on the game window,
// the outline font and the hints that say what the host's inputs do there. The core's Vulkan
// renderer takes the window after it is gone.
#pragma once

#include "pad_input.hpp"

#include <SDL3/SDL_rect.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
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

// What some of the host's inputs do on a screen: each input, then the words.
struct Hint {
  std::vector<Prompt> by;
  std::string words;
};

class MenuCanvas {
public:
  // `assets` is a port-assets copy, whose pad glyphs and key caps the hints draw.
  MenuCanvas(GameWindow &window, const std::filesystem::path &font, std::filesystem::path assets);
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

  // A hint's art against its words' size.
  static constexpr float kHintArtShare = 1.35F;
  // One line of hints as `device` shows them: pad glyphs, or the caps of the keys. Its words are
  // `size` tall, or smaller when the line is wider than `width`; `at` is its top left corner,
  // or the middle of its top edge when `centred`. The font keeps its size.
  void hints(std::span<const Hint> line, HostDevice device, SDL_FPoint at, float size, float width,
             bool centred) const;

private:
  // Both draw only when `draw`, and give the width they take.
  float hintLine(std::span<const Hint> line, HostDevice device, SDL_FPoint at, float size, bool draw) const;
  float prompt(const Prompt &by, HostDevice device, SDL_FPoint at, float tall, bool draw) const;
  // A set's art `height` pixels tall, made once per height.
  [[nodiscard]] SDL_Texture *art(std::string_view set, std::string_view name, int width, int height) const;

  struct Art {
    SDL_Texture *texture;
    int height;
  };

  SDL_Renderer *renderer_ = nullptr;
  TTF_TextEngine *engine_ = nullptr;
  TTF_Font *font_ = nullptr;
  std::filesystem::path assets_;
  mutable std::map<std::string, Art> art_;
};

[[nodiscard]] SDL_FPoint textSize(TTF_Text *text);

} // namespace z3d::app
