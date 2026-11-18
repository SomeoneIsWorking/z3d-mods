// The material track of frame interpolation (docs/plans/replay-interpolation.md): the material
// animations (texture scroll, colour and texture frames), each started a fraction of its step on
// so that its step lands between two ticks. The step is the draw's own (OoT3D, and MM3D's models
// stepped in the draw), or the update's call of it before the section (MM3D's rooms), which the
// replay makes again as a lead. An animation pairs by its address and animation while the newer
// tick's frame is the older one's step.
#pragma once

#include "core/section_replay.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct MaterialAddresses {
  std::vector<uint32_t> stepCalls; // the branches in the section that step an animation
  std::vector<uint32_t> leadCalls; // those before it, whose result the section only reads
  uint32_t stepArg;                // which of their arguments is the animation
  // A virtual call site steps an animation only when it reaches this function.
  std::optional<uint32_t> update;
  uint32_t anim; // offsets in an animation
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
  [[nodiscard]] const std::vector<uint32_t> &leadCalls() const {
    return addresses_.leadCalls;
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
    // Stepped before the section: the replay makes the call again.
    bool lead = false;
    uint32_t anim = 0;
    float frame = 0.0F;
    float speed = 0.0F;
    bool loop = false;
    bool paused = false;
    int32_t frameCount = 0;
  };
  // The frame AnimPlayer_Update leaves after one step from `from`.
  [[nodiscard]] static float stepped(const Sample &from);
  void take(Core::SectionReplay &replay, const Core::SectionReplay::Call &call, bool lead);

  MaterialAddresses addresses_;
  std::unordered_set<uint32_t> stepSites_;
  std::unordered_set<uint32_t> leadSites_;
  std::unordered_map<uint32_t, Sample> previous_;
  std::unordered_map<uint32_t, Sample> current_;
};

} // namespace z3d::app
