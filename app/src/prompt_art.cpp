#include "prompt_art.hpp"

#include <SDL3/SDL_iostream.h>
#include <SDL3_image/SDL_image.h>

#include <string>

namespace z3d::app {

std::filesystem::path setArtFile(const std::filesystem::path &assets, std::string_view set, std::string_view name) {
  return assets / "sets" / set / (std::string(name) + ".svg");
}

SDL_Surface *loadSetArt(const std::filesystem::path &assets, std::string_view set, std::string_view name, int width,
                        int height) {
  SDL_IOStream *stream = SDL_IOFromFile(setArtFile(assets, set, name).c_str(), "rb");
  if (stream == nullptr) {
    return nullptr;
  }
  SDL_Surface *surface = IMG_LoadSizedSVG_IO(stream, width, height);
  SDL_CloseIO(stream);
  return surface;
}

} // namespace z3d::app
