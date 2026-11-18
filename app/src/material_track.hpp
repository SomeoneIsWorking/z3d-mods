// The material track of frame interpolation (docs/plans/replay-interpolation.md): the material
// animations (texture scroll, colour and texture frames) the draw steps, each started a fraction
// of its step on, so that the draw's own step lands between two ticks. An animation pairs by its
// address and animation while the newer tick's frame is the older one's step.
#pragma once

#include "core/section_replay.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct MaterialAddresses {
  std::vector<uint32_t> stepCalls; // the branches that step an animation in the draw
  uint32_t stepArg;                // which of their arguments is the animation
  uint32_t anim;                   // offsets in an animation
  uint32_t frame;
  uint32_t speed;
  uint32_t loop;
  uint32_t paused;
  uint32_t animOffset; // in the animation's file: its block
  uint32_t frameCount; // in the block
};

class MaterialTrack {
public:
  explicit MaterialTrack(MaterialAddresses addresses);

  [[nodiscard]] const std::vector<uint32_t> &stepCalls() const {
    return addresses_.stepCalls;
  }
  // Takes the animations the real section stepped, as they stood at its start.
  void sample(Core::SectionReplay &replay);
  struct Counts {
    uint64_t paired = 0;
    uint64_t unpaired = 0;
  };
  // The paired animations' frames a fraction `t` of their step on from the previous tick's.
  Counts inputs(float t, std::vector<Core::SectionReplay::Input> &out) const;

private:
  struct Sample {
    uint32_t anim = 0;
    float frame = 0.0F;
    float speed = 0.0F;
    bool loop = false;
    bool paused = false;
    int32_t frameCount = 0;
  };
  // The frame AnimPlayer_Update leaves after one step from `from`.
  [[nodiscard]] static float stepped(const Sample &from);

  MaterialAddresses addresses_;
  std::unordered_set<uint32_t> calls_;
  std::unordered_map<uint32_t, Sample> previous_;
  std::unordered_map<uint32_t, Sample> current_;
};

} // namespace z3d::app
