// The HUD's art at the window's size: port-assets SVGs, lettering and item icons rasterized into
// the one image the core draws quads of (Settings::values.overlay_image).
#pragma once

#include "common/settings.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct TTF_Font;
struct SDL_Surface;

namespace z3d::app {

// A rectangle of the atlas, in its pixels; empty when the art could not be read.
struct HudSprite {
  float x = 0.0F;
  float y = 0.0F;
  float w = 0.0F;
  float h = 0.0F;
};

class HudAtlas {
public:
  // `assets` is a port-assets checkout or copy: sets/<set>/<name>.svg and fonts/.
  explicit HudAtlas(std::filesystem::path assets);
  ~HudAtlas();
  HudAtlas(const HudAtlas &) = delete;
  HudAtlas &operator=(const HudAtlas &) = delete;
  HudAtlas(HudAtlas &&) = delete;
  HudAtlas &operator=(HudAtlas &&) = delete;

  // Each draws its art once per size and returns it again after that.
  [[nodiscard]] HudSprite svg(const std::string &set, const std::string &name, int width, int height);
  // Lettering `height` pixels tall in port-assets' key typeface: white, or `dark` for a key cap.
  [[nodiscard]] HudSprite text(const std::string &text, int height, bool dark);
  // A raster image (an item icon) scaled to the size.
  [[nodiscard]] HudSprite image(const std::filesystem::path &file, int width, int height);

  // Drops every sprite: the window changed size.
  void clear();
  // The atlas as the core takes it; a new object whenever a sprite was added.
  [[nodiscard]] std::shared_ptr<const Settings::OverlayImage> image();

private:
  [[nodiscard]] HudSprite place(const std::string &key, SDL_Surface *surface);

  std::filesystem::path assets_;
  TTF_Font *font_ = nullptr;
  std::map<std::string, HudSprite> sprites_;
  std::vector<uint8_t> pixels_;
  uint32_t height_ = 0;
  // The shelf being filled: its top, its height and how far along it is.
  uint32_t shelfTop_ = 0;
  uint32_t shelfHeight_ = 0;
  uint32_t shelfX_ = 0;
  uint64_t generation_ = 0;
  std::shared_ptr<const Settings::OverlayImage> image_;
};

} // namespace z3d::app
