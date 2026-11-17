#include "camera_track.hpp"

#include <cmath>
#include <cstring>

namespace z3d::app {
namespace {

template <class T> T readStart(Core::SectionReplay &replay, uint32_t address) {
  T value{};
  std::array<uint8_t, sizeof(T)> bytes{};
  replay.ReadStart(address, bytes);
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

template <class T> Core::SectionReplay::Input input(uint32_t address, const T &value) {
  std::vector<uint8_t> bytes(sizeof(T));
  std::memcpy(bytes.data(), &value, sizeof(T));
  return {address, std::move(bytes)};
}

} // namespace

void CameraTrack::sample(Core::SectionReplay &replay) {
  Sample sample;
  sample.play = readStart<uint32_t>(replay, addresses_.play);
  if (sample.play == 0) {
    previous_.reset();
    current_.reset();
    return;
  }
  const auto active = readStart<int16_t>(replay, sample.play + addresses_.activeCamera);
  if (active < 0 || active >= 4) {
    previous_.reset();
    current_.reset();
    return;
  }
  sample.camera = readStart<uint32_t>(replay, sample.play + addresses_.cameras +
                                                  static_cast<uint32_t>(active) * 4U);
  sample.viewEye = readStart<Vec3>(replay, sample.play + addresses_.viewEye);
  sample.viewAt = readStart<Vec3>(replay, sample.play + addresses_.viewAt);
  sample.viewUp = readStart<Vec3>(replay, sample.play + addresses_.viewUp);
  sample.fovy = readStart<float>(replay, sample.play + addresses_.viewFovy);
  if (sample.camera != 0) {
    sample.cameraEye = readStart<Vec3>(replay, sample.camera + addresses_.cameraEye);
    sample.cameraAt = readStart<Vec3>(replay, sample.camera + addresses_.cameraAt);
  }
  previous_ = current_;
  current_ = sample;
}

std::optional<std::vector<Core::SectionReplay::Input>> CameraTrack::inputs(float t) const {
  if (!previous_ || !current_ || previous_->play != current_->play ||
      previous_->camera != current_->camera || current_->camera == 0) {
    return std::nullopt;
  }
  const Sample &from = *previous_;
  const Sample &to = *current_;
  const auto lerp = [t](float a, float b) {
    return a + (b - a) * t;
  };
  // The unshaken point between the ticks, shaken by the newer tick's quake offset.
  const auto shaken = [&](const Vec3 &fromCamera, const Vec3 &toCamera, const Vec3 &toView) {
    Vec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
      out[i] = lerp(fromCamera[i], toCamera[i]) + (toView[i] - toCamera[i]);
    }
    return out;
  };
  Vec3 up{};
  float length = 0.0F;
  for (std::size_t i = 0; i < 3; ++i) {
    up[i] = lerp(from.viewUp[i], to.viewUp[i]);
    length += up[i] * up[i];
  }
  length = std::sqrt(length);
  if (length > 0.0F) {
    for (float &component : up) {
      component /= length;
    }
  }
  return std::vector<Core::SectionReplay::Input>{
      input(to.play + addresses_.viewEye, shaken(from.cameraEye, to.cameraEye, to.viewEye)),
      input(to.play + addresses_.viewAt, shaken(from.cameraAt, to.cameraAt, to.viewAt)),
      input(to.play + addresses_.viewUp, up),
      input(to.play + addresses_.viewFovy, lerp(from.fovy, to.fovy)),
  };
}

} // namespace z3d::app
