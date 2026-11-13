#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace z3d::app {

struct FrameSize {
  unsigned width = 0;
  unsigned height = 0;
};

// A captured frame, as XRGB8888 rows.
class Frame {
public:
  // Tightly packed XRGB8888 rows, top row first.
  void storeRows(std::vector<uint32_t> rows, FrameSize size);

  [[nodiscard]] unsigned width() const {
    return width_;
  }
  [[nodiscard]] unsigned height() const {
    return height_;
  }

  // False if nothing was captured yet or the file cannot be written.
  [[nodiscard]] bool writePng(const std::filesystem::path &path) const;

private:
  unsigned width_ = 0;
  unsigned height_ = 0;
  std::vector<uint32_t> pixels_;
};

} // namespace z3d::app
