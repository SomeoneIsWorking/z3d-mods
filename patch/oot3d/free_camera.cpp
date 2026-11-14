// Right-stick free camera: after the game's own camera update, the C-stick, and the mouse through
// the slot link, orbit the eye around the game's look-at point. The game keeps its at, quake,
// water and setting changes; the orbit holds only while the game runs Link's follow camera, so
// Z-targeting, first person, talking, climbing, horses, cutscenes and fixed or scripted settings
// hand control back.
// It replaces The One Screen's orbit camera, whose hook hooks.s takes back.
#include "../common/cstick.hpp"
#include "../common/guest.hpp"
#include "../common/mouse_look.hpp"
#include "options.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Angles {
  int16_t pitch;
  int16_t yaw;
  int16_t roll;
};

using CameraUpdateFn = void (*)(Angles *out, void *camera);
using BgCheckFn = int32_t (*)(void *camera, const Vec3 *from, void *colChk);
using LookAtFn = void (*)(void *view, Vec3 *eye, Vec3 *at, Vec3 *up);

constexpr float kTau = 6.2831853F;
constexpr float kBinangToRadians = kTau / 65536.0F;

// sin(x) for x in [-pi, pi], odd polynomial on [-pi/2, pi/2] with the rest folded in.
float sine(float x) {
  constexpr float kHalfPi = kTau / 4.0F;
  if (x > kHalfPi) {
    x = kTau / 2.0F - x;
  } else if (x < -kHalfPi) {
    x = -kTau / 2.0F - x;
  }
  const float x2 = x * x;
  return x * (1.0F + x2 * (-1.0F / 6.0F + x2 * (1.0F / 120.0F + x2 * (-1.0F / 5040.0F + x2 / 362880.0F))));
}

float sinS(int16_t angle) {
  return sine(static_cast<float>(angle) * kBinangToRadians);
}

float cosS(int16_t angle) {
  return sinS(static_cast<int16_t>(angle + 0x4000));
}

float squareRoot(float value) {
  float out = 0.0F;
  asm("vsqrt.f32 %0, %1" : "=t"(out) : "t"(value));
  return out;
}

Vec3 readVec(const void *base, uint32_t offset) {
  return {guest::read<float>(base, offset), guest::read<float>(base, offset + 4),
          guest::read<float>(base, offset + 8)};
}

void writeVec(void *base, uint32_t offset, const Vec3 &value) {
  guest::write<float>(base, offset, value.x);
  guest::write<float>(base, offset + 4, value.y);
  guest::write<float>(base, offset + 8, value.z);
}

Vec3 minus(const Vec3 &a, const Vec3 &b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 plus(const Vec3 &a, const Vec3 &b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scaled(const Vec3 &a, float factor) {
  return {a.x * factor, a.y * factor, a.z * factor};
}

float length(const Vec3 &a) {
  return squareRoot(a.x * a.x + a.y * a.y + a.z * a.z);
}

class FreeCamera {
public:
  // `step` is false when the game redoes the update for a draw: the stick moves the orbit once a frame.
  void update(Angles *out, void *camera, bool step) {
    guest::function<CameraUpdateFn>(kCameraUpdate)(out, camera);
    // Taken every frame, so a turn made while the orbit cannot follow it is dropped.
    const LookTurn look = step ? takeLook() : LookTurn{};
    StickPosition stick;
    if (!options().freeCamera || !eligible(camera) || !cstick_.read(&stick)) {
      engaged_ = false;
      return;
    }
    // Master Quest's world is mirrored.
    const bool mirrored = guest::read<uint8_t>(kSaveContext + kSaveMasterQuest) != 0;
    const int32_t x = turn(stick.x, options().freeCameraInvertX != mirrored);
    const int32_t y = turn(stick.y, options().freeCameraInvertY);
    const bool stickMoved = x * x + y * y > kDeadzone * kDeadzone;
    const bool moved = stickMoved || look.right != 0 || look.down != 0;
    const Vec3 at = readVec(camera, kCamAt);
    const Vec3 gameEye = readVec(camera, kCamEye);
    const float gameDistance = guest::read<float>(camera, kCamDist);
    if (!engaged_) {
      if (!moved || !step) {
        return;
      }
      engaged_ = true;
      pitch_ = clampPitch(guest::read<int16_t>(camera, kCamDir));
      yaw_ = guest::read<int16_t>(camera, kCamDir + 2);
      distance_ = gameDistance;
      reach_ = gameDistance;
    }
    if (step) {
      if (stickMoved) {
        yaw_ = static_cast<int16_t>(yaw_ - x * kYawSpeed);
        pitch_ = clampPitch(pitch_ + y * kPitchSpeed);
      }
      yaw_ = static_cast<int16_t>(yaw_ - turn(look.right, mirrored));
      pitch_ = clampPitch(pitch_ - look.down);
      // The game steers its own radius from our eye toward its setting's range: follow it.
      if (gameDistance >= kDistanceMin && gameDistance <= kDistanceMax) {
        distance_ += (gameDistance - distance_) * kDistanceFollow;
      }
    }
    place(camera, at, gameEye, step);
    out->pitch = pitch_;
    out->yaw = yaw_;
    out->roll = 0;
    guest::write<int16_t>(camera, kCamDir, pitch_);
    guest::write<int16_t>(camera, kCamDir + 2, yaw_);
    guest::write<int16_t>(camera, kCamDir + 4, 0);
    guest::write<int16_t>(camera, kCamInputDir, pitch_);
    guest::write<int16_t>(camera, kCamInputDir + 2, yaw_);
    guest::write<int16_t>(camera, kCamInputDir + 4, 0);
  }

private:
  static constexpr int32_t kDeadzone = 20;
  static constexpr int32_t kYawSpeed = 5;
  static constexpr int32_t kPitchSpeed = 4;
  // camDir.x: negative looks down at Link from above, positive up from below.
  static constexpr int32_t kPitchMin = -0x3000;
  static constexpr int32_t kPitchMax = 0x1400;
  static constexpr float kDistanceMin = 40.0F;
  static constexpr float kDistanceMax = 800.0F;
  static constexpr float kDistanceFollow = 0.12F;
  static constexpr float kReachEase = 0.25F;

  static int32_t turn(int32_t value, bool inverted) {
    return inverted ? -value : value;
  }

  static int16_t clampPitch(int32_t pitch) {
    return static_cast<int16_t>(pitch < kPitchMin ? kPitchMin : (pitch > kPitchMax ? kPitchMax : pitch));
  }

  // The camera function the game runs for this setting and mode, as Camera_Update picks it.
  static int16_t cameraFunction(void *camera) {
    const uint32_t setting = guest::read<uint16_t>(camera, kCamSetting) & 0xFFU;
    const uint32_t mode = guest::read<uint16_t>(camera, kCamMode) & 0xFFU;
    const uintptr_t entry = kCameraSettings + setting * kCameraSettingSize;
    if ((guest::read<uint32_t>(entry + kCameraSettingValidModes) & (1U << mode)) == 0) {
      return -1;
    }
    return guest::read<int16_t>(guest::read<uintptr_t>(entry + kCameraSettingModes) + mode * kCameraModeSize +
                                kCameraModeFunction);
  }

  // Link's own follow camera, on the ground or in a jump; every other function frames something.
  static bool follows(int16_t function) {
    return function == kCamFuncNorm1 || function == kCamFuncJump1;
  }

  static bool eligible(void *camera) {
    auto *play = reinterpret_cast<void *>(guest::read<uintptr_t>(camera, kCamPlay));
    if (play == nullptr || guest::read<uintptr_t>(camera, kCamPlayer) == 0) {
      return false;
    }
    if (guest::read<int16_t>(play, kPlayActiveCamera) != 0 ||
        guest::read<uintptr_t>(play, kPlayCameras) != reinterpret_cast<uintptr_t>(camera)) {
      return false;
    }
    if (guest::read<uint8_t>(play, kPlayCutscene + kCsState) != 0 || guest::read<int8_t>(play, kPlayTransitionTrigger) != 0) {
      return false;
    }
    return guest::read<int16_t>(camera, kCamStatus) == kCamStatActive && follows(cameraFunction(camera));
  }

  // The eye on the orbit, kept in front of walls; the view gets the game's quake offset on top.
  void place(void *camera, const Vec3 &at, const Vec3 &gameEye, bool step) {
    auto *play = reinterpret_cast<uint8_t *>(guest::read<uintptr_t>(camera, kCamPlay));
    auto *view = play + kPlayView;
    const Vec3 shakeEye = minus(readVec(view, kViewEye), gameEye);
    const Vec3 shakeAt = minus(readVec(view, kViewAt), at);
    const float horizontal = cosS(pitch_);
    const Vec3 back = {-horizontal * sinS(yaw_), -sinS(pitch_), -horizontal * cosS(yaw_)};
    // Camera_BGCheckInfo reads only pos; it writes the rest.
    alignas(4) uint8_t colChk[kColChkSize];
    const Vec3 orbit = plus(at, scaled(back, distance_));
    writeVec(colChk, kColChkPos, orbit);
    guest::function<BgCheckFn>(kCameraBgCheck)(camera, &at, colChk);
    // In at once when something is in the way, out again gradually.
    const float clear = length(minus(readVec(colChk, kColChkPos), at));
    if (step) {
      reach_ = clear < reach_ ? clear : reach_ + (clear - reach_) * kReachEase;
    }
    const Vec3 eye = plus(at, scaled(back, reach_ < clear ? reach_ : clear));
    // eyeNext is the eye before collision: the game measures its radius from it.
    writeVec(camera, kCamEye, eye);
    writeVec(camera, kCamEyeNext, orbit);
    Vec3 viewEye = plus(eye, shakeEye);
    Vec3 viewAt = plus(at, shakeAt);
    Vec3 up = readVec(view, kViewUp);
    guest::function<LookAtFn>(kViewLookAt)(view, &viewEye, &viewAt, &up);
  }

  CStick cstick_;
  bool engaged_ = false;
  int16_t pitch_ = 0;
  int16_t yaw_ = 0;
  // The orbit's radius, and how much of it is in use while something is in the way.
  float distance_ = 0.0F;
  float reach_ = 0.0F;
};

constinit FreeCamera freeCamera;

} // namespace

// Called instead of Camera_Update for the active camera: by Play_Update, and by Gameplay_Draw
// when it redoes the update (link.ld).
void updateActiveCamera(void *out, void *camera) {
  freeCamera.update(static_cast<Angles *>(out), camera, true);
}

void redrawActiveCamera(void *out, void *camera) {
  freeCamera.update(static_cast<Angles *>(out), camera, false);
}

} // namespace z3dmods::oot3d
