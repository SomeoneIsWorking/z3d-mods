// The camera track of frame interpolation (docs/plans/replay-interpolation.md): View_LookAt's
// inputs in the play state's view, blended between two ticks without the quake, with the newer
// tick's quake added back.
#pragma once

#include "core/section_replay.h"
#include "motion_blend.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct CameraAddresses {
  uint32_t play;    // the global holding the play state's pointer
  uint32_t frames;  // offsets in the play state: its game state's frame count
  uint32_t viewEye; // offsets in the play state
  uint32_t viewAt;
  uint32_t viewUp;
  uint32_t viewFovy;
  uint32_t cameras;
  uint32_t activeCamera;
  uint32_t cameraEye; // offsets in a camera
  uint32_t cameraAt;
};

class CameraTrack {
public:
  explicit CameraTrack(const CameraAddresses &addresses) : addresses_(addresses) {
  }

  // Takes the tick's camera from the memory the section started from.
  void sample(Core::SectionReplay &replay);
  // The view's inputs a fraction `t` of the way from the previous tick to this one, or nothing at
  // a cut: no previous tick, another play state or camera, or a play state started again (its
  // frame count not one on from the previous tick's: a scene change, warp or respawn).
  [[nodiscard]] std::optional<std::vector<Core::SectionReplay::Input>> inputs(float t) const;

private:
  struct Sample {
    uint32_t play = 0;
    uint32_t frames = 0;
    uint32_t camera = 0;
    Vec3 viewEye{};
    Vec3 viewAt{};
    Vec3 viewUp{};
    float fovy = 0.0F;
    Vec3 cameraEye{};
    Vec3 cameraAt{};
  };

  CameraAddresses addresses_;
  std::optional<Sample> previous_;
  std::optional<Sample> current_;
};

} // namespace z3d::app
