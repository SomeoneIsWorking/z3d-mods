// The actor track of frame interpolation (docs/plans/replay-interpolation.md): each live actor's
// world position and shape rotation, which Actor_Draw builds its model matrix from, blended
// between two ticks. An actor pairs by its address and id while it is in an actor list in both.
#pragma once

#include "core/section_replay.h"
#include "motion_blend.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace z3d::app {

// Guest offsets from the manifest's "interpolation" table.
struct ActorAddresses {
  uint32_t play;  // the global holding the play state's pointer
  uint32_t lists; // the actor lists in the play state
  uint32_t listCount;
  uint32_t listSize; // one list's entry
  uint32_t listHead; // in an entry
  uint32_t next;     // in an actor
  uint32_t id;
  uint32_t position;
  uint32_t shapeRotation;
};

class ActorTrack {
public:
  explicit ActorTrack(const ActorAddresses &addresses) : addresses_(addresses) {
  }

  // Takes the tick's live actors from the section's start; another play state pairs none.
  void sample(Core::SectionReplay &replay);
  struct Counts {
    uint64_t paired = 0;
    uint64_t unpaired = 0;
  };
  // The paired actors' positions and rotations a fraction `t` of the way from the previous tick.
  Counts inputs(float t, std::vector<Core::SectionReplay::Input> &out) const;

private:
  struct Sample {
    int16_t id = 0;
    Vec3 position{};
    Vec3s shapeRotation{};
  };

  ActorAddresses addresses_;
  uint32_t play_ = 0;
  std::unordered_map<uint32_t, Sample> previous_;
  std::unordered_map<uint32_t, Sample> current_;
};

} // namespace z3d::app
