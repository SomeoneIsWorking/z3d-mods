// The blends of frame interpolation's motion tracks, one of each
// (docs/plans/replay-interpolation.md).
#pragma once

#include <array>
#include <cmath>
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

// A colour channel or other 0-255 value, rounded.
[[nodiscard]] inline uint8_t lerpByte(uint8_t from, uint8_t to, float t) {
  return static_cast<uint8_t>(
      std::lround(lerp(static_cast<float>(from), static_cast<float>(to), t)));
}

// A signed 16-bit level, rounded.
[[nodiscard]] inline int16_t lerpShort(int16_t from, int16_t to, float t) {
  return static_cast<int16_t>(
      std::lround(lerp(static_cast<float>(from), static_cast<float>(to), t)));
}

// A binary angle (0x10000 a turn) the short way round.
[[nodiscard]] inline int16_t lerpAngle(int16_t from, int16_t to, float t) {
  const auto delta = static_cast<int16_t>(static_cast<uint16_t>(to) - static_cast<uint16_t>(from));
  return static_cast<int16_t>(
      static_cast<uint16_t>(from) +
      static_cast<uint16_t>(static_cast<int32_t>(static_cast<float>(delta) * t)));
}

// An 8-bit turn (0x100 a turn) the short way round, rounded.
[[nodiscard]] inline uint8_t lerpTurn8(uint8_t from, uint8_t to, float t) {
  const auto delta = static_cast<int8_t>(static_cast<uint8_t>(to - from));
  return static_cast<uint8_t>(from + std::lround(static_cast<float>(delta) * t));
}

[[nodiscard]] inline Vec3s lerpAngles(const Vec3s &from, const Vec3s &to, float t) {
  Vec3s out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = lerpAngle(from[i], to[i], t);
  }
  return out;
}

// A 3x4 matrix, rows of rotation with the translation last: the game's MTX34.
using Mtx34 = std::array<float, 12>;

// The rotation slerped the short way and the translation straight; the rotation comes back
// orthonormal.
[[nodiscard]] inline Mtx34 blendRigid(const Mtx34 &from, const Mtx34 &to, float t) {
  using Quat = std::array<float, 4>; // x, y, z, w
  const auto toQuat = [](const Mtx34 &m) {
    const float r00 = m[0], r01 = m[1], r02 = m[2];
    const float r10 = m[4], r11 = m[5], r12 = m[6];
    const float r20 = m[8], r21 = m[9], r22 = m[10];
    Quat q{};
    const float trace = r00 + r11 + r22;
    if (trace > 0.0F) {
      const float s = std::sqrt(trace + 1.0F) * 2.0F;
      q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25F * s};
    } else if (r00 > r11 && r00 > r22) {
      const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
      q = {0.25F * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
      const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
      q = {(r01 + r10) / s, 0.25F * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
      const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
      q = {(r02 + r20) / s, (r12 + r21) / s, 0.25F * s, (r10 - r01) / s};
    }
    return q;
  };
  const Quat a = toQuat(from);
  Quat b = toQuat(to);
  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0F) {
    for (float &c : b) {
      c = -c;
    }
    dot = -dot;
  }
  Quat q{};
  if (dot > 0.9995F) {
    for (std::size_t i = 0; i < q.size(); ++i) {
      q[i] = lerp(a[i], b[i], t);
    }
  } else {
    const float theta = std::acos(dot);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0F - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;
    for (std::size_t i = 0; i < q.size(); ++i) {
      q[i] = wa * a[i] + wb * b[i];
    }
  }
  const float norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (float &c : q) {
    c /= norm;
  }
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  return Mtx34{
      1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y - z * w),        2.0F * (x * z + y * w),
      lerp(from[3], to[3], t),       2.0F * (x * y + z * w),        1.0F - 2.0F * (x * x + z * z),
      2.0F * (y * z - x * w),        lerp(from[7], to[7], t),       2.0F * (x * z - y * w),
      2.0F * (y * z + x * w),        1.0F - 2.0F * (x * x + y * y), lerp(from[11], to[11], t)};
}

} // namespace z3d::app
