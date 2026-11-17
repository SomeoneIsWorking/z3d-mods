#include "camera_track.hpp"

#include "motion_blend.hpp"
#include "section_memory.hpp"

#include <cmath>

namespace z3d::app {

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
  // The unshaken point between the ticks, shaken by the newer tick's quake offset.
  const auto shaken = [&](const Vec3 &fromCamera, const Vec3 &toCamera, const Vec3 &toView) {
    Vec3 out = lerp(fromCamera, toCamera, t);
    for (std::size_t i = 0; i < out.size(); ++i) {
      out[i] += toView[i] - toCamera[i];
    }
    return out;
  };
  Vec3 up = lerp(from.viewUp, to.viewUp, t);
  const float length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
  if (length > 0.0F) {
    for (float &component : up) {
      component /= length;
    }
  }
  return std::vector<Core::SectionReplay::Input>{
      replayInput(to.play + addresses_.viewEye, shaken(from.cameraEye, to.cameraEye, to.viewEye)),
      replayInput(to.play + addresses_.viewAt, shaken(from.cameraAt, to.cameraAt, to.viewAt)),
      replayInput(to.play + addresses_.viewUp, up),
      replayInput(to.play + addresses_.viewFovy, lerp(from.fovy, to.fovy, t)),
  };
}

} // namespace z3d::app
