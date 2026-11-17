// The blends of frame interpolation's motion tracks, one of each
// (docs/plans/replay-interpolation.md).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace z3d::app {

using Vec3 = std::array<float, 3>;
using Vec3s = std::array<int16_t, 3>;

[[nodiscard]] inline float lerp(float from, float to, float t) {
  return from + (to - from) * t;
}

[[nodiscard]] inline Vec3 lerp(const Vec3 &from, const Vec3 &to, float t) {
  Vec3 out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = lerp(from[i], to[i], t);
  }
  return out;
}

// A binary angle (0x10000 a turn) the short way round.
[[nodiscard]] inline int16_t lerpAngle(int16_t from, int16_t to, float t) {
  const auto delta = static_cast<int16_t>(static_cast<uint16_t>(to) - static_cast<uint16_t>(from));
  return static_cast<int16_t>(
      static_cast<uint16_t>(from) +
      static_cast<uint16_t>(static_cast<int32_t>(static_cast<float>(delta) * t)));
}

[[nodiscard]] inline Vec3s lerpAngles(const Vec3s &from, const Vec3s &to, float t) {
  Vec3s out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = lerpAngle(from[i], to[i], t);
  }
  return out;
}

} // namespace z3d::app
