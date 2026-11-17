// The skeleton track of frame interpolation (docs/plans/replay-interpolation.md): each skeleton
// the section draws has its joint table, posed in the update, blended joint by joint between two
// ticks. A skeleton pairs by its address, model and limb count while it is drawn in both.
#pragma once

#include "core/section_replay.h"
#include "motion_blend.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace z3d::app {

// Guest addresses and offsets from the manifest's "interpolation" table.
struct SkeletonAddresses {
  uint32_t drawCall; // the branch that queues a skeleton's draw
  uint32_t drawArg;  // which of its arguments is the skeleton
  uint32_t model;    // offsets in a skeleton
  uint32_t limbCount;
  uint32_t jointTable;
  uint32_t jointSize; // one joint's entry, its matrix first
};

class SkeletonTrack {
public:
  explicit SkeletonTrack(const SkeletonAddresses &addresses) : addresses_(addresses) {
  }

  [[nodiscard]] uint32_t drawCall() const {
    return addresses_.drawCall;
  }
  // Takes the joint tables of the skeletons the real section drew, as they stood at its start.
  void sample(Core::SectionReplay &replay);

  struct Counts {
    uint64_t paired = 0;
    uint64_t unpaired = 0;
  };
  // The paired skeletons' joints a fraction `t` of the way from the previous tick.
  Counts inputs(float t, std::vector<Core::SectionReplay::Input> &out) const;

private:
  struct Sample {
    uint32_t model = 0;
    uint32_t jointTable = 0;
    std::vector<Mtx34> joints;
  };

  SkeletonAddresses addresses_;
  std::unordered_map<uint32_t, Sample> previous_;
  std::unordered_map<uint32_t, Sample> current_;
};

} // namespace z3d::app
