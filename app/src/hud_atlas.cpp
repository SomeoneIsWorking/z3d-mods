#include "hud_atlas.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <lucent/log.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace z3d::app {
namespace {

constexpr uint32_t kWidth = 2048;
constexpr uint32_t kFirstHeight = 512;
constexpr uint32_t kBytesPerPixel = 4;
// Clear pixels between sprites, so a quad's edge never samples its neighbour.
constexpr uint32_t kGap = 2;
constexpr const char *kFont = "fonts/NotoSans-Bold-keys.ttf";

} // namespace

HudAtlas::HudAtlas(std::filesystem::path assets) : assets_(std::move(assets)) {
  if (!TTF_Init()) {
    throw std::runtime_error(std::string("SDL_ttf: ") + SDL_GetError());
  }
  font_ = TTF_OpenFont((assets_ / kFont).c_str(), 1.0F);
  if (font_ == nullptr) {
    TTF_Quit();
    throw std::runtime_error((assets_ / kFont).string() + ": " + SDL_GetError());
  }
  clear();
}

HudAtlas::~HudAtlas() {
  TTF_CloseFont(font_);
  TTF_Quit();
}

void HudAtlas::clear() {
  sprites_.clear();
  height_ = kFirstHeight;
  pixels_.assign(size_t{kWidth} * height_ * kBytesPerPixel, 0);
  shelfTop_ = 0;
  shelfHeight_ = 0;
  shelfX_ = 0;
  image_.reset();
}

HudSprite HudAtlas::place(const std::string &key, SDL_Surface *surface) {
  if (surface == nullptr) {
    lucent::warn("hud", "no art for {}: {}", key, SDL_GetError());
    return sprites_[key] = {};
  }
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  if (rgba == nullptr || static_cast<uint32_t>(rgba->w) + kGap > kWidth) {
    SDL_DestroySurface(rgba);
    lucent::warn("hud", "{} does not fit the atlas", key);
    return sprites_[key] = {};
  }
  const auto width = static_cast<uint32_t>(rgba->w);
  const auto height = static_cast<uint32_t>(rgba->h);
  if (shelfX_ + width + kGap > kWidth) {
    shelfTop_ += shelfHeight_ + kGap;
    shelfHeight_ = 0;
    shelfX_ = 0;
  }
  shelfHeight_ = std::max(shelfHeight_, height);
  while (shelfTop_ + shelfHeight_ > height_) {
    height_ *= 2;
    pixels_.resize(size_t{kWidth} * height_ * kBytesPerPixel, 0);
  }
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(&pixels_[(size_t{shelfTop_ + row} * kWidth + shelfX_) * kBytesPerPixel],
                static_cast<const uint8_t *>(rgba->pixels) + size_t{row} * static_cast<size_t>(rgba->pitch),
                size_t{width} * kBytesPerPixel);
  }
  const HudSprite sprite{static_cast<float>(shelfX_), static_cast<float>(shelfTop_),
                         static_cast<float>(width), static_cast<float>(height)};
  shelfX_ += width + kGap;
  SDL_DestroySurface(rgba);
  image_.reset();
  return sprites_[key] = sprite;
}

HudSprite HudAtlas::svg(const std::string &set, const std::string &name, int width, int height) {
  const std::string key = set + "/" + name + "@" + std::to_string(width) + "x" + std::to_string(height);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  const std::filesystem::path file = assets_ / "sets" / set / (name + ".svg");
  SDL_IOStream *stream = SDL_IOFromFile(file.c_str(), "rb");
  SDL_Surface *surface = stream != nullptr ? IMG_LoadSizedSVG_IO(stream, width, height) : nullptr;
  if (stream != nullptr) {
    SDL_CloseIO(stream);
  }
  return place(key, surface);
}

HudSprite HudAtlas::text(const std::string &text, int height, bool dark) {
  const std::string key = (dark ? "dark/" : "text/") + text + "@" + std::to_string(height);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  TTF_SetFontSize(font_, static_cast<float>(height));
  return place(key, TTF_RenderText_Blended(font_, text.c_str(), text.size(), dark ? SDL_Color{32, 32, 36, 255} : SDL_Color{255, 255, 255, 255}));
}

HudSprite HudAtlas::image(const std::filesystem::path &file, int width, int height) {
  const std::string key = file.string() + "@" + std::to_string(width) + "x" + std::to_string(height);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  SDL_Surface *loaded = IMG_Load(file.c_str());
  SDL_Surface *scaled = loaded != nullptr ? SDL_ScaleSurface(loaded, width, height, SDL_SCALEMODE_LINEAR) : nullptr;
  SDL_DestroySurface(loaded);
  return place(key, scaled);
}

std::shared_ptr<const Settings::OverlayImage> HudAtlas::image() {
  if (!image_) {
    image_ = std::make_shared<const Settings::OverlayImage>(
        Settings::OverlayImage{kWidth, height_, ++generation_, pixels_});
  }
  return image_;
}

} // namespace z3d::app
