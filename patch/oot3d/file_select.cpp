// File select on the top screen: The One Screen draws the touch screen's file select there through
// a viewport of the top screen's middle 320 pixels, so nothing can reach its corners. While file
// select is up its viewport takes the whole top screen (hooks.s base_file_select_*) and the touch
// screen's projection spans x -40 to 360 to match: everything stays where it was, and what is
// moved past the touch screen's edges (BACK, hooks.s back_*) shows (decomp docs/file_select.md).
#include "../common/guest.hpp"
#include "game.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using SetMatricesFn = void (*)(void *camera, float *projection, const float *view);

// The touch screen's width and the top screen's, in pixels.
inline constexpr float kTouchWidth = 320.0F;
inline constexpr float kTopWidth = 400.0F;
// The projection's row for clip y, of a 4x4 row-major matrix; x is the touch screen's across.
inline constexpr int kClipYScale = 4;
inline constexpr int kClipYOffset = 7;

bool fileSelectUp() {
  return guest::read<int32_t>(kSaveContext + kSaveGameMode) == kGameModeFileSelect &&
         guest::read<uint32_t>(kFileChoose + kFileChooseActive) != 0;
}

} // namespace

// The touch screen layers' calls of Camera2D_SetMatrices (TouchScreen2D_BeginLayer0 to 3).
void fileSelectProjection(void *camera, float *projection, const float *view) {
  if (fileSelectUp()) {
    // Clip y of x is the game's of x' = (x + margin) * 320 / 400, so x -40 to 360 spans what
    // 0 to 320 did.
    constexpr float squeeze = kTouchWidth / kTopWidth;
    constexpr float margin = (kTopWidth - kTouchWidth) / 2;
    projection[kClipYOffset] += projection[kClipYScale] * margin * squeeze;
    projection[kClipYScale] *= squeeze;
  }
  guest::function<SetMatricesFn>(kCamera2DSetMatrices)(camera, projection, view);
}

} // namespace z3dmods::oot3d
