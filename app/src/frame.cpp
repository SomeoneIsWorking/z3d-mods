#include "frame.hpp"

#include "core/frontend/image_interface.h"

#include <utility>

namespace z3d::app {

void Frame::storeRows(std::vector<uint32_t> rows, FrameSize size) {
  width_ = size.width;
  height_ = size.height;
  pixels_ = std::move(rows);
}

bool Frame::writePng(const std::filesystem::path &path) const {
  if (pixels_.empty()) {
    return false;
  }
  std::vector<uint8_t> rgba;
  rgba.reserve(pixels_.size() * 4);
  for (const uint32_t pixel : pixels_) {
    rgba.push_back(static_cast<uint8_t>(pixel >> 16));
    rgba.push_back(static_cast<uint8_t>(pixel >> 8));
    rgba.push_back(static_cast<uint8_t>(pixel));
    rgba.push_back(0xFF);
  }
  Frontend::ImageInterface encoder;
  return encoder.EncodePNG(path.string(), width_, height_, rgba);
}

} // namespace z3d::app
