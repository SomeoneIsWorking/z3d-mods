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
  sample.frames = readStart<uint32_t>(replay, sample.play + addresses_.frames);
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
  if (sample.camera != 0 && addresses_.door) {
    const DoorCutAddresses &door = *addresses_.door;
    const auto setting = readStart<uint16_t>(replay, sample.camera + door.cameraSetting);
    const auto mode = readStart<uint16_t>(replay, sample.camera + door.cameraMode);
    const auto modes = readStart<uint32_t>(
        replay, door.settings + (setting & 0xFFU) * door.settingSize + door.settingModes);
    sample.function =
        readStart<int16_t>(replay, modes + (mode & 0xFFU) * door.modeSize + door.modeFunction);
    sample.animState = readStart<int16_t>(replay, sample.camera + door.cameraAnimState);
  }
  if (addresses_.shot) {
    const ShotCutAddresses &shot = *addresses_.shot;
    sample.cutsceneEyeShot = readStart<uint16_t>(replay, sample.play + shot.eyeShot);
    sample.cutsceneAtShot = readStart<uint16_t>(replay, sample.play + shot.atShot);
    sample.cutsceneShot = readStart<int32_t>(replay, sample.play + shot.shot);
  }
  previous_ = current_;
  current_ = sample;
}

std::optional<std::vector<Core::SectionReplay::Input>> CameraTrack::inputs(float t) const {
  if (!previous_ || !current_) {
    return std::nullopt;
  }
  const Sample &from = *previous_;
  const Sample &to = *current_;
  if (from.play != to.play || from.frames + 1 != to.frames || from.camera != to.camera ||
      to.camera == 0 || passedDoor(from, to) || newShot(from, to)) {
    return std::nullopt;
  }
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

bool CameraTrack::passedDoor(const Sample &from, const Sample &to) const {
  if (!addresses_.door) {
    return false;
  }
  const DoorCutAddresses &door = *addresses_.door;
  const auto atFarSide = [&door](const Sample &sample) {
    return sample.function == static_cast<int16_t>(door.doorFunction) &&
           sample.animState == static_cast<int16_t>(door.doorFarSide);
  };
  return atFarSide(to) && !atFarSide(from);
}

bool CameraTrack::newShot(const Sample &from, const Sample &to) {
  return from.cutsceneEyeShot != to.cutsceneEyeShot || from.cutsceneAtShot != to.cutsceneAtShot ||
         from.cutsceneShot != to.cutsceneShot;
}

} // namespace z3d::app
