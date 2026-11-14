#include "menu_canvas.hpp"

#include "game_window.hpp"
#include "prompt_art.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace z3d::app {
namespace {

std::runtime_error sdlError(const char *what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

// Shares of a hint's art: the gap after it, the space between two hints, a cap's lettering and
// the room beside a word on a cap.
constexpr float kHintGap = 0.35F;
constexpr float kHintSpace = 1.1F;
constexpr float kCapLabelShare = 0.5F;
constexpr float kCapWordRoom = 0.7F;
constexpr Rgb kCapInk{24, 24, 30};

} // namespace

MenuCanvas::MenuCanvas(GameWindow &window, const std::filesystem::path &font, std::filesystem::path assets)
    : assets_(std::move(assets)) {
  renderer_ = SDL_CreateRenderer(window.sdlWindow(), "vulkan");
  if (renderer_ == nullptr) {
    throw sdlError("SDL_CreateRenderer");
  }
  SDL_SetRenderVSync(renderer_, 1);
  if (!TTF_Init()) {
    throw sdlError("TTF_Init");
  }
  engine_ = TTF_CreateRendererTextEngine(renderer_);
  font_ = TTF_OpenFont(font.c_str(), 1.0F);
  if (engine_ == nullptr || font_ == nullptr) {
    throw sdlError("TTF_OpenFont");
  }
}

MenuCanvas::~MenuCanvas() {
  for (const auto &[name, made] : art_) {
    SDL_DestroyTexture(made.texture);
  }
  TTF_CloseFont(font_);
  if (engine_ != nullptr) {
    TTF_DestroyRendererTextEngine(engine_);
  }
  TTF_Quit();
  SDL_DestroyRenderer(renderer_);
}

SDL_FPoint MenuCanvas::size() const {
  int w = 0;
  int h = 0;
  SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
  return {static_cast<float>(std::max(w, 1)), static_cast<float>(std::max(h, 1))};
}

void MenuCanvas::clear() const {
  // The setup screen (folder_setup.hpp) draws through a clip of its own.
  SDL_SetRenderClipRect(renderer_, nullptr);
  SDL_SetRenderDrawColor(renderer_, kMenuBackground.r, kMenuBackground.g, kMenuBackground.b,
                         SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer_);
}

void MenuCanvas::fill(Rgb color, const SDL_FRect &rect) const {
  SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(renderer_, &rect);
}

TTF_Text *MenuCanvas::text(const std::string &line, Rgb color) const {
  TTF_Text *made = TTF_CreateText(engine_, font_, line.c_str(), line.size());
  if (made == nullptr) {
    throw sdlError("TTF_CreateText");
  }
  TTF_SetTextColor(made, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  return made;
}

SDL_Texture *MenuCanvas::art(std::string_view set, std::string_view name, int width, int height) const {
  const std::string key = std::string(set) + "/" + std::string(name);
  if (const auto found = art_.find(key); found != art_.end()) {
    if (found->second.height == height) {
      return found->second.texture;
    }
    SDL_DestroyTexture(found->second.texture);
    art_.erase(found);
  }
  SDL_Surface *surface = loadSetArt(assets_, set, name, width, height);
  SDL_Texture *texture = surface != nullptr ? SDL_CreateTextureFromSurface(renderer_, surface) : nullptr;
  SDL_DestroySurface(surface);
  if (texture == nullptr) {
    throw std::runtime_error("menu art is missing or unreadable: " + setArtFile(assets_, set, name).string() + " (" +
                             SDL_GetError() + ")");
  }
  art_[key] = {texture, height};
  return texture;
}

float MenuCanvas::prompt(const Prompt &by, HostDevice device, SDL_FPoint at, float tall, bool draw) const {
  const int side = static_cast<int>(tall);
  if (device == HostDevice::Pad) {
    const std::string_view glyph = by.pad ? glyphOf(*by.pad) : std::string_view();
    if (glyph.empty()) {
      return 0.0F;
    }
    if (draw) {
      const SDL_FRect box{at.x, at.y, tall, tall};
      SDL_RenderTexture(renderer_, art(kPadSet, glyph, side, side), nullptr, &box);
    }
    return tall;
  }
  if (!by.key) {
    return 0.0F;
  }
  const std::string name = SDL_GetScancodeName(*by.key);
  TTF_SetFontSize(font_, tall * kCapLabelShare);
  TTF_Text *label = text(name, kCapInk);
  const SDL_FPoint lettering = textSize(label);
  const bool word = name.size() > 1;
  const float wide = word ? std::max(std::round(tall * kWideCapAspect), lettering.x + tall * kCapWordRoom) : tall;
  if (draw) {
    const SDL_FRect box{at.x, at.y, wide, tall};
    if (word) {
      // The cap's ends as they are drawn, its middle as long as the word.
      const float end = tall / 2.0F;
      SDL_RenderTexture9Grid(renderer_, art(kKeySet, kWideCap, static_cast<int>(std::round(tall * kWideCapAspect)), side),
                             nullptr, end, end, 0.0F, 0.0F, 0.0F, &box);
    } else {
      SDL_RenderTexture(renderer_, art(kKeySet, kKeyCap, side, side), nullptr, &box);
    }
    TTF_DrawRendererText(label, at.x + std::round((wide - lettering.x) / 2.0F),
                         at.y + std::round((tall - lettering.y) / 2.0F));
  }
  TTF_DestroyText(label);
  return wide;
}

float MenuCanvas::hintLine(std::span<const Hint> line, HostDevice device, SDL_FPoint at, float size, bool draw) const {
  const float tall = std::round(size * kHintArtShare);
  float x = at.x;
  for (const Hint &hint : line) {
    if (&hint != &line.front()) {
      x += tall * kHintSpace;
    }
    for (const Prompt &by : hint.by) {
      const float took = prompt(by, device, {x, at.y}, tall, draw);
      x += took > 0.0F ? took + tall * kHintGap : 0.0F;
    }
    TTF_SetFontSize(font_, size);
    TTF_Text *words = text(hint.words, kMenuDim);
    const SDL_FPoint lettering = textSize(words);
    if (draw) {
      TTF_DrawRendererText(words, x, at.y + std::round((tall - lettering.y) / 2.0F));
    }
    TTF_DestroyText(words);
    x += lettering.x;
  }
  return x - at.x;
}

void MenuCanvas::hints(std::span<const Hint> line, HostDevice device, SDL_FPoint at, float size, float width,
                       bool centred) const {
  const float kept = TTF_GetFontSize(font_);
  float wide = hintLine(line, device, at, size, false);
  if (wide > width) {
    size *= width / wide;
    wide = hintLine(line, device, at, size, false);
  }
  (void)hintLine(line, device, {centred ? at.x - wide / 2.0F : at.x, at.y}, size, true);
  TTF_SetFontSize(font_, kept);
}

SDL_FPoint textSize(TTF_Text *text) {
  int w = 0;
  int h = 0;
  TTF_GetTextSize(text, &w, &h);
  return {static_cast<float>(w), static_cast<float>(h)};
}

} // namespace z3d::app
