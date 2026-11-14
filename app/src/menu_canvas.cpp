#include "menu_canvas.hpp"

#include "game_window.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <stdexcept>

namespace z3d::app {
namespace {

std::runtime_error sdlError(const char *what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

} // namespace

MenuCanvas::MenuCanvas(GameWindow &window, const std::filesystem::path &font) {
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

SDL_FPoint textSize(TTF_Text *text) {
  int w = 0;
  int h = 0;
  TTF_GetTextSize(text, &w, &h);
  return {static_cast<float>(w), static_cast<float>(h)};
}

} // namespace z3d::app
