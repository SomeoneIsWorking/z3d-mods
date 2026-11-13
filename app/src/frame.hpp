#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace z3d::app {

struct FrameSize {
  unsigned width = 0;
  unsigned height = 0;
};

// The last frame the core presented, as XRGB8888 rows.
class Frame {
public:
  void store(const void *data, FrameSize size, size_t pitch);
  // Tightly packed XRGB8888 rows, bottom row first (an OpenGL read).
  void storeBottomUp(const std::vector<uint32_t> &rows, FrameSize size);

  [[nodiscard]] unsigned width() const {
    return width_;
  }
  [[nodiscard]] unsigned height() const {
    return height_;
  }

  // Writes an RGB PNG; false if nothing was presented yet or the file cannot be written.
  [[nodiscard]] bool writePng(const std::filesystem::path &path) const;

private:
  unsigned width_ = 0;
  unsigned height_ = 0;
  std::vector<uint32_t> pixels_;
};

} // namespace z3d::app
