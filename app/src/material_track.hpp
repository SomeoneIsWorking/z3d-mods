// The material track of frame interpolation (docs/plans/replay-interpolation.md): the material
// animations (texture scroll, colour and texture frames), each started a fraction of its step on
// so that its step lands between two ticks. The step is the draw's own (OoT3D), or the update's
// call of it before the section (MM3D), which the replay makes again as a lead. An animation pairs
// by its address and animation while the newer tick's frame is the older one's step.
#pragma once

#include "core/section_replay.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct MaterialAddresses {
  std::vector<uint32_t> stepCalls; // the branches that step an animation
  bool lead;                       // they come before the section, which only reads the result
  uint32_t stepArg;                // which of their arguments is the animation
  uint32_t anim;                   // offsets in an animation
  uint32_t frame;
  uint32_t speed;
  uint32_t loop;
  uint32_t paused;
  uint32_t animFile;   // in what `anim` points to: the file
  uint32_t animOffset; // in the animation's file: its block
  uint32_t frameCount; // in the block
};

class MaterialTrack {
public:
  explicit MaterialTrack(MaterialAddresses addresses);

  [[nodiscard]] const std::vector<uint32_t> &stepCalls() const {
    return addresses_.stepCalls;
  }
  [[nodiscard]] bool lead() const {
    return addresses_.lead;
  }
  // Takes the animations the real section stepped, as they stood at its start.
  void sample(Core::SectionReplay &replay);
  struct Counts {
    uint64_t paired = 0;
    uint64_t unpaired = 0;
  };
  // The paired animations' frames a fraction `t` of their step on from the previous tick's, and
  // for lead steps the calls that step and apply them again.
  Counts inputs(float t, std::vector<Core::SectionReplay::Input> &out,
                std::vector<Core::SectionReplay::Call> &leads) const;

private:
  struct Sample {
    Core::SectionReplay::Call call{};
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
