#include "frame.hpp"

#include <zlib.h>

#include <array>
#include <cstring>
#include <fstream>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

void putBe32(std::vector<uint8_t> &out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value >> 24));
  out.push_back(static_cast<uint8_t>(value >> 16));
  out.push_back(static_cast<uint8_t>(value >> 8));
  out.push_back(static_cast<uint8_t>(value));
}

void putChunk(std::vector<uint8_t> &out, std::string_view type, const std::vector<uint8_t> &body) {
  putBe32(out, static_cast<uint32_t>(body.size()));
  const size_t typeAt = out.size();
  out.insert(out.end(), type.begin(), type.end());
  out.insert(out.end(), body.begin(), body.end());
  const uLong crc = crc32(0, out.data() + typeAt, static_cast<uInt>(out.size() - typeAt));
  putBe32(out, static_cast<uint32_t>(crc));
}

} // namespace

void Frame::store(const void *data, FrameSize size, size_t pitch) {
  const auto [width, height] = size;
  if (data == nullptr) {
    return; // a duplicated frame: the previous one stands
  }
  width_ = width;
  height_ = height;
  pixels_.resize(static_cast<size_t>(width) * height);
  const auto *rows = static_cast<const uint8_t *>(data);
  for (unsigned y = 0; y < height; ++y) {
    std::memcpy(&pixels_[static_cast<size_t>(y) * width], rows + y * pitch,
                width * sizeof(uint32_t));
  }
}

void Frame::storeRows(std::vector<uint32_t> rows, FrameSize size) {
  width_ = size.width;
  height_ = size.height;
  pixels_ = std::move(rows);
}

bool Frame::writePng(const std::filesystem::path &path) const {
  if (pixels_.empty()) {
    return false;
  }
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height_) * (1 + width_ * 3));
  for (unsigned y = 0; y < height_; ++y) {
    raw.push_back(0);
    for (unsigned x = 0; x < width_; ++x) {
      const uint32_t pixel = pixels_[static_cast<size_t>(y) * width_ + x];
      raw.push_back(static_cast<uint8_t>(pixel >> 16));
      raw.push_back(static_cast<uint8_t>(pixel >> 8));
      raw.push_back(static_cast<uint8_t>(pixel));
    }
  }
  uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> packed(packedSize);
  if (compress2(packed.data(), &packedSize, raw.data(), static_cast<uLong>(raw.size()), 6) !=
      Z_OK) {
    return false;
  }
  packed.resize(packedSize);

  std::vector<uint8_t> header;
  putBe32(header, width_);
  putBe32(header, height_);
  header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  putChunk(png, "IHDR", header);
  putChunk(png, "IDAT", packed);
  putChunk(png, "IEND", {});

  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
  return static_cast<bool>(file);
}

} // namespace z3d::app
