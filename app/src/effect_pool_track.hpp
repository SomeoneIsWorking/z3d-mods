// The effect pool track of frame interpolation (docs/plans/replay-interpolation.md): an actor's
// own array of effects (ObjectKankyo's fairies), each effect's position blended between two
// ticks. An effect pairs by its slot in an actor paired by the actor track while it is in use in
// both ticks and its base, set where it spawns, is unchanged.
#pragma once

#include "actor_track.hpp"
#include "core/section_replay.h"
#include "motion_blend.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct EffectPoolAddresses {
  uint32_t play;   // the global holding the play state's pointer
  uint32_t count;  // in the play state: how many effects are in use
  uint32_t id;     // the owning actor's id and params
  uint32_t params; // offset in an actor
  uint32_t paramsValue;
  uint32_t effects; // offset of the array in the actor
  uint32_t effectSize;
  uint32_t state; // offsets in an effect
  uint32_t position;
  uint32_t base;
  uint32_t alpha; // stepped by the draw itself
};

class EffectPoolTrack {
public:
  explicit EffectPoolTrack(const EffectPoolAddresses &addresses) : addresses_(addresses) {
  }

  // Takes the effects of the owning actors the actor track paired this tick.
  void sample(Core::SectionReplay &replay, const ActorTrack &actors);
  struct Counts {
    uint64_t paired = 0;
    uint64_t unpaired = 0;
  };
  // The paired effects' positions and alphas a fraction `t` of the way from the previous tick; the
  // draw then steps the blended alpha once, as it steps the real one.
  Counts inputs(float t, std::vector<Core::SectionReplay::Input> &out) const;

private:
  struct Sample {
    uint8_t state = 0;
    Vec3 position{};
    Vec3 base{};
    uint8_t alpha = 0;
  };

  EffectPoolAddresses addresses_;
  std::unordered_map<uint32_t, Sample> previous_;
  std::unordered_map<uint32_t, Sample> current_;
};

} // namespace z3d::app
