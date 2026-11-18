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
  uint32_t cameraSetting;
  uint32_t cameraMode;
  uint32_t cameraAnimState;
  // sCameraSettings, by which Camera_Update picks a camera's function from its setting and mode.
  uint32_t settings;
  uint32_t settingSize;
  uint32_t settingModes;
  uint32_t modeSize;
  uint32_t modeFunction;
  // Camera_Special9, and its animState once it has put the eye beyond the door.
  uint32_t doorFunction;
  uint32_t doorFarSide;
  // Offsets in the play state: the cutscene's shot (the frames its eye and at point lists were
  // handed to the camera, and command 0x97's segment), each changed only as a shot starts.
  uint32_t cutsceneEyeShot;
  uint32_t cutsceneAtShot;
  uint32_t cutsceneShot;
};

class CameraTrack {
public:
  explicit CameraTrack(const CameraAddresses &addresses) : addresses_(addresses) {
  }

  // Takes the tick's camera from the memory the section started from.
  void sample(Core::SectionReplay &replay);
  // The view's inputs a fraction `t` of the way from the previous tick to this one, or nothing at
  // a cut: no previous tick, another play state or camera, or a play state started again (its
  // frame count not one on from the previous tick's: a scene change, warp or respawn), or the
  // door camera's step to the door's far side, or a new cutscene shot.
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
    // The active camera's function index, and its animState.
    int16_t function = -1;
    int16_t animState = 0;
    uint16_t cutsceneEyeShot = 0;
    uint16_t cutsceneAtShot = 0;
    int32_t cutsceneShot = 0;
  };

  [[nodiscard]] bool passedDoor() const;
  [[nodiscard]] bool newShot() const;

  CameraAddresses addresses_;
  std::optional<Sample> previous_;
  std::optional<Sample> current_;
};

} // namespace z3d::app
