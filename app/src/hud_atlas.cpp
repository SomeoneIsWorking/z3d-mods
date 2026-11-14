#include "hud_atlas.hpp"

#include "prompt_art.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <lucent/log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <sstream>
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
// The size the capital's height is measured at, and lettering's outline against its height.
constexpr float kMeasureSize = 200.0F;
constexpr float kOutlineShare = 0.14F;
constexpr uint8_t kInkAlpha = 8;
// The ring's circle in its 72-unit square (sets/hud/ring.svg).
constexpr double kArcCentre = 36.0;
constexpr double kArcRadius = 31.0;

// The rows and columns of `surface` (RGBA32) that hold ink; empty when it has none.
// Fills `surface` (RGBA32) with the outline's colour, its corners rounded by `radius`.
void plate(SDL_Surface *surface, int radius) {
  constexpr std::array<uint8_t, kBytesPerPixel> kPlate{16, 16, 20, 255};
  const int limit = std::min({radius, surface->w / 2, surface->h / 2});
  for (int y = 0; y < surface->h; ++y) {
    uint8_t *row = static_cast<uint8_t *>(surface->pixels) +
                   static_cast<size_t>(y) * static_cast<size_t>(surface->pitch);
    const int down = std::max({limit - y, y - (surface->h - 1 - limit), 0});
    for (int x = 0; x < surface->w; ++x) {
      const int across = std::max({limit - x, x - (surface->w - 1 - limit), 0});
      if (across * across + down * down <= limit * limit) {
        std::memcpy(row + static_cast<size_t>(x) * kBytesPerPixel, kPlate.data(), kBytesPerPixel);
      }
    }
  }
}

SDL_Rect inkBounds(const SDL_Surface *surface) {
  int left = surface->w;
  int top = surface->h;
  int right = -1;
  int bottom = -1;
  for (int y = 0; y < surface->h; ++y) {
    const uint8_t *row = static_cast<const uint8_t *>(surface->pixels) +
                         static_cast<size_t>(y) * static_cast<size_t>(surface->pitch);
    for (int x = 0; x < surface->w; ++x) {
      if (row[static_cast<size_t>(x) * kBytesPerPixel + 3] > kInkAlpha) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left) {
    return {0, 0, 0, 0};
  }
  return {left, top, right - left + 1, bottom - top + 1};
}

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
  TTF_SetFontSize(font_, kMeasureSize);
  SDL_Surface *capital = TTF_RenderText_Blended(font_, "H", 1, SDL_Color{255, 255, 255, 255});
  SDL_Surface *rgba =
      capital != nullptr ? SDL_ConvertSurface(capital, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(capital);
  capShare_ = rgba != nullptr ? static_cast<float>(inkBounds(rgba).h) / kMeasureSize : 0.0F;
  SDL_DestroySurface(rgba);
  if (capShare_ <= 0.0F) {
    TTF_CloseFont(font_);
    TTF_Quit();
    throw std::runtime_error((assets_ / kFont).string() + " draws no capital");
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

HudSprite HudAtlas::place(const std::string &key, SDL_Surface *surface, float base) {
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
                static_cast<const uint8_t *>(rgba->pixels) +
                    size_t{row} * static_cast<size_t>(rgba->pitch),
                size_t{width} * kBytesPerPixel);
  }
  const HudSprite sprite{static_cast<float>(shelfX_), static_cast<float>(shelfTop_),
                         static_cast<float>(width), static_cast<float>(height), base};
  shelfX_ += width + kGap;
  SDL_DestroySurface(rgba);
  image_.reset();
  return sprites_[key] = sprite;
}

HudSprite HudAtlas::svg(const std::string &set, const std::string &name, int width, int height,
                        uint32_t tint) {
  const std::string key = set + "/" + name + "@" + std::to_string(width) + "x" +
                          std::to_string(height) + "#" + std::to_string(tint);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  SDL_Surface *surface = loadSetArt(assets_, set, name, width, height);
  if (surface == nullptr || tint == kNoTint) {
    return place(key, surface);
  }
  SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surface);
  for (int y = 0; rgba != nullptr && y < rgba->h; ++y) {
    uint8_t *row = static_cast<uint8_t *>(rgba->pixels) +
                   static_cast<size_t>(y) * static_cast<size_t>(rgba->pitch);
    for (int x = 0; x < rgba->w; ++x) {
      uint8_t *pixel = row + static_cast<size_t>(x) * kBytesPerPixel;
      for (size_t channel = 0; channel < 3; ++channel) {
        const uint32_t share = (tint >> (16 - 8 * channel)) & 0xFFU;
        pixel[channel] = static_cast<uint8_t>(pixel[channel] * share / 0xFFU);
      }
    }
  }
  return place(key, rgba);
}

HudSprite HudAtlas::arc(int size, int step, int steps) {
  const std::string key =
      "arc@" + std::to_string(size) + ":" + std::to_string(step) + "/" + std::to_string(steps);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  // Over the ring's own rim, in gold.
  const double turn = 2.0 * std::numbers::pi * step / steps;
  const double endX = kArcCentre + kArcRadius * std::sin(turn);
  const double endY = kArcCentre - kArcRadius * std::cos(turn);
  std::ostringstream art;
  art << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 72 72\" width=\"72\" "
         "height=\"72\">";
  if (step >= steps) {
    art << "<circle cx=\"" << kArcCentre << "\" cy=\"" << kArcCentre << "\" r=\"" << kArcRadius
        << "\"";
  } else {
    art << "<path d=\"M" << kArcCentre << " " << kArcCentre - kArcRadius << " A" << kArcRadius
        << " " << kArcRadius << " 0 " << (step * 2 > steps ? 1 : 0) << " 1 " << endX << " " << endY
        << "\"";
  }
  art << " fill=\"none\" stroke=\"#FFD23C\" stroke-width=\"6\"/></svg>";
  const std::string text = art.str();
  SDL_IOStream *stream = SDL_IOFromConstMem(text.data(), text.size());
  SDL_Surface *surface = stream != nullptr ? IMG_LoadSizedSVG_IO(stream, size, size) : nullptr;
  if (stream != nullptr) {
    SDL_CloseIO(stream);
  }
  return place(key, surface);
}

int HudAtlas::outline(int height) {
  return std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * kOutlineShare)));
}

// The text drawn and cut to its ink, RGBA32; `base` is the baseline's row in it.
SDL_Surface *HudAtlas::lettering(const std::string &text, int height, HudInk ink, float &base) {
  constexpr SDL_Color kDark{32, 32, 36, 255};
  constexpr SDL_Color kOutline{16, 16, 20, 255};
  constexpr SDL_Color kLight{255, 255, 255, 255};
  constexpr SDL_Color kFull{120, 255, 80, 255};
  TTF_SetFontSize(font_, static_cast<float>(height) / capShare_);
  const int around = ink == HudInk::Dark ? 0 : outline(height);
  const int baseline = TTF_GetFontAscent(font_) + around;
  SDL_Surface *fill = TTF_RenderText_Blended(font_, text.c_str(), text.size(),
                                             ink == HudInk::Dark   ? kDark
                                             : ink == HudInk::Full ? kFull
                                                                   : kLight);
  SDL_Surface *drawn = fill;
  if (fill != nullptr && around > 0) {
    TTF_SetFontOutline(font_, around);
    drawn = TTF_RenderText_Blended(font_, text.c_str(), text.size(), kOutline);
    TTF_SetFontOutline(font_, 0);
    SDL_Rect inside{around, around, fill->w, fill->h};
    if (drawn != nullptr) {
      SDL_BlitSurface(fill, nullptr, drawn, &inside);
    }
    SDL_DestroySurface(fill);
  }
  SDL_Surface *rgba =
      drawn != nullptr ? SDL_ConvertSurface(drawn, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(drawn);
  if (rgba == nullptr) {
    return nullptr;
  }
  const SDL_Rect bounds = inkBounds(rgba);
  SDL_Surface *cut =
      bounds.w > 0 ? SDL_CreateSurface(bounds.w, bounds.h, SDL_PIXELFORMAT_RGBA32) : nullptr;
  if (cut != nullptr) {
    if (ink == HudInk::Plate) {
      plate(cut, around);
    } else {
      SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_NONE);
    }
    SDL_BlitSurface(rgba, &bounds, cut, nullptr);
    base = static_cast<float>(baseline - bounds.y);
  }
  SDL_DestroySurface(rgba);
  return cut;
}

HudSprite HudAtlas::text(const std::string &text, int height, HudInk ink) {
  constexpr std::array kNames{"text/", "full/", "dark/", "plate/"};
  const std::string key = kNames[static_cast<size_t>(ink)] + text + "@" + std::to_string(height);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  float base = 0.0F;
  SDL_Surface *surface = lettering(text, height, ink, base);
  return place(key, surface, base);
}

HudSprite HudAtlas::image(const std::filesystem::path &file, int width, int height) {
  const std::string key =
      file.string() + "@" + std::to_string(width) + "x" + std::to_string(height);
  if (const auto found = sprites_.find(key); found != sprites_.end()) {
    return found->second;
  }
  SDL_Surface *loaded = IMG_Load(file.c_str());
  if (loaded != nullptr && width <= 0 && loaded->h > 0) {
    width = std::max(
        1, static_cast<int>(std::lround(static_cast<float>(loaded->w) * static_cast<float>(height) /
                                        static_cast<float>(loaded->h))));
  }
  SDL_Surface *scaled =
      loaded != nullptr ? SDL_ScaleSurface(loaded, width, height, SDL_SCALEMODE_LINEAR) : nullptr;
  SDL_DestroySurface(loaded);
  return place(key, scaled);
}

void HudAtlas::requireSvg(const std::string &set, const std::string &name) const {
  constexpr int kProbe = 16;
  SDL_Surface *surface = loadSetArt(assets_, set, name, kProbe, kProbe);
  if (surface == nullptr) {
    throw std::runtime_error("HUD art is missing or unreadable: " +
                             setArtFile(assets_, set, name).string() + " (" + SDL_GetError() + ")");
  }
  SDL_DestroySurface(surface);
}

void HudAtlas::requireImage(const std::filesystem::path &file) {
  SDL_Surface *surface = IMG_Load(file.c_str());
  if (surface == nullptr) {
    throw std::runtime_error("HUD art is missing or unreadable: " + file.string() + " (" +
                             SDL_GetError() + "); install the game's set again");
  }
  SDL_DestroySurface(surface);
}

std::shared_ptr<const Settings::OverlayImage> HudAtlas::image() {
  if (!image_) {
    image_ = std::make_shared<const Settings::OverlayImage>(
        Settings::OverlayImage{kWidth, height_, ++generation_, pixels_});
  }
  return image_;
}

} // namespace z3d::app
