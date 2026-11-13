// The patch's options: one block at the start of the payload (link.ld), holding the defaults.
// The app's options menu writes it; every feature reads its switches here.
#pragma once

#include <stdint.h>

namespace z3dmods::oot3d {

struct Options {
  uint8_t freeCamera = 1;
  uint8_t freeCameraInvertX = 0;
  uint8_t freeCameraInvertY = 0;
};

const volatile Options &options();

} // namespace z3dmods::oot3d
