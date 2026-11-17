#include "skeleton_track.hpp"

#include "section_memory.hpp"

namespace z3d::app {

void SkeletonTrack::sample(Core::SectionReplay &replay) {
  previous_ = std::move(current_);
  current_.clear();
  for (const Core::SectionReplay::Call &call : replay.SectionCensus().calls) {
    if (call.site != addresses_.drawCall) {
      continue;
    }
    const uint32_t skeleton = call.args.at(addresses_.drawArg);
    if (current_.contains(skeleton)) {
      continue;
    }
    Sample sample;
    sample.model = readStart<uint32_t>(replay, skeleton + addresses_.model);
    sample.jointTable = readStart<uint32_t>(replay, skeleton + addresses_.jointTable);
    const auto limbs = readStart<uint8_t>(replay, skeleton + addresses_.limbCount);
    if (sample.jointTable == 0) {
      continue;
    }
    sample.joints.reserve(limbs);
    for (uint32_t limb = 0; limb < limbs; ++limb) {
      sample.joints.push_back(
          readStart<Mtx34>(replay, sample.jointTable + limb * addresses_.jointSize));
    }
    current_.emplace(skeleton, std::move(sample));
  }
}

SkeletonTrack::Counts SkeletonTrack::inputs(float t,
                                            std::vector<Core::SectionReplay::Input> &out) const {
  Counts counts;
  for (const auto &[skeleton, to] : current_) {
    const auto from = previous_.find(skeleton);
    if (from == previous_.end() || from->second.model != to.model ||
        from->second.jointTable != to.jointTable ||
        from->second.joints.size() != to.joints.size()) {
      ++counts.unpaired;
      continue;
    }
    ++counts.paired;
    for (std::size_t limb = 0; limb < to.joints.size(); ++limb) {
      if (from->second.joints[limb] == to.joints[limb]) {
        continue;
      }
      out.push_back(replayInput(to.jointTable + static_cast<uint32_t>(limb) * addresses_.jointSize,
                                blendRigid(from->second.joints[limb], to.joints[limb], t)));
    }
  }
  return counts;
}

} // namespace z3d::app
