// The mouse turns the game's own C-stick camera. While a turn is pending the stick does not read
// as neutral, so the player's camera goes to the stick's mode as for the stick; before
// Camera_Follow runs, its eyeNext is turned around at by the turn, and the camera takes its eye
// back from there, through its own limits and collision.
#include "../common/guest.hpp"
#include "../common/mouse_look.hpp"
#include "play_update.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

using DiffToGeoFn = void (*)(void *geo, const void *from, const void *to);
using AddGeoFn = void (*)(void *out, const void *from, const void *geo);

class MouseCamera {
public:
  // Once a frame: a turn that was already pending a frame ago and that no camera took since is
  // dropped (Z-targeting, first person, a cutscene), so it does not land later.
  void frame() {
    if (!ran_ && pending_) {
      (void)takeLook();
    }
    ran_ = false;
    pending_ = lookPending();
  }

  void turn(void *camera) {
    ran_ = true;
    const LookTurn turn = takeLook();
    if (turn.right == 0 && turn.down == 0) {
      return;
    }
    auto *at = static_cast<uint8_t *>(camera) + kCamAt;
    auto *eyeNext = static_cast<uint8_t *>(camera) + kCamEyeNext;
    alignas(4) uint8_t geo[kVecGeoSize];
    guest::function<DiffToGeoFn>(kVecDiffToGeo)(geo, at, eyeNext);
    // The eye goes up for a view turned down; right turns the view right, as the C-stick's right.
    const int32_t pitch = guest::read<int16_t>(geo, kGeoPitch) + turn.down;
    guest::write<int16_t>(geo, kGeoPitch, static_cast<int16_t>(pitch < -kPitchMax ? -kPitchMax : (pitch > kPitchMax ? kPitchMax : pitch)));
    guest::write<int16_t>(geo, kGeoYaw, static_cast<int16_t>(guest::read<int16_t>(geo, kGeoYaw) - turn.right));
    guest::function<AddGeoFn>(kVecAddGeo)(eyeNext, at, geo);
  }

private:
  // Short of straight above or below at, where the yaw has no meaning.
  static constexpr int32_t kPitchMax = 0x3C00;

  // Camera_Follow ran since the last frame().
  bool ran_ = false;
  // A turn was pending at the last frame().
  bool pending_ = false;
};

constinit MouseCamera mouseCamera;

} // namespace

void dropUntakenLook() {
  mouseCamera.frame();
}

// Asked by Input_CStickNeutral (hooks.s): a mouse turn is the stick out of neutral.
bool mouseTurning() {
  return lookPending();
}

// Called at the start of Camera_Follow (hooks.s).
void turnCameraByMouse(void *camera) {
  mouseCamera.turn(camera);
}

} // namespace z3dmods::mm3d
