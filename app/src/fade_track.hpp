// The fade track of frame interpolation (docs/plans/replay-interpolation.md): the colour of each
// screen fill the draw fades (the scene transition while it is a fade, and the fade flash),
// blended between two ticks. A fill pairs by its address and type while it is a fade in both.
#pragma once

#include "core/section_replay.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct FadeAddresses {
  uint32_t play;           // the global holding the play state's pointer
  uint32_t transition;     // offsets in the play state: the transition's instance
  uint32_t transitionDraw; // the transition's draw method
  uint32_t flash;          // the fade flash, always a fade
  uint32_t fadeDraw;       // the fade's draw function
  uint32_t type;           // offsets in a fade
  uint32_t color;
};

class FadeTrack {
public:
  explicit FadeTrack(const FadeAddresses &addresses) : addresses_(addresses) {
  }

  // Takes the tick's fades from the section's start; another play state pairs none.
  void sample(Core::SectionReplay &replay);
  struct Counts {
    uint64_t paired = 0;
    uint64_t unpaired = 0;
  };
  // The paired fades' colours a fraction `t` of the way from the previous tick.
  Counts inputs(float t, std::vector<Core::SectionReplay::Input> &out) const;

private:
  struct Sample {
    uint8_t type = 0;
    std::array<uint8_t, 4> color{};
  };

  FadeAddresses addresses_;
  uint32_t play_ = 0;
  std::unordered_map<uint32_t, Sample> previous_;
  std::unordered_map<uint32_t, Sample> current_;
};

} // namespace z3d::app
