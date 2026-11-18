#include "effect_pool_track.hpp"

#include "section_memory.hpp"

namespace z3d::app {

void EffectPoolTrack::sample(Core::SectionReplay &replay, const ActorTrack &actors) {
  previous_ = std::move(current_);
  current_.clear();
  const auto play = readStart<uint32_t>(replay, addresses_.play);
  if (play == 0) {
    return;
  }
  const auto count = readStart<uint8_t>(replay, play + addresses_.count);
  for (const uint32_t actor : actors.pairedWithId(static_cast<int16_t>(addresses_.id))) {
    if (readStart<int16_t>(replay, actor + addresses_.params) !=
        static_cast<int16_t>(addresses_.paramsValue)) {
      continue;
    }
    for (uint32_t index = 0; index < count; ++index) {
      const uint32_t effect = actor + addresses_.effects + index * addresses_.effectSize;
      const Sample sample{
          .state = readStart<uint8_t>(replay, effect + addresses_.state),
          .position = readStart<Vec3>(replay, effect + addresses_.position),
          .base = readStart<Vec3>(replay, effect + addresses_.base),
      };
      if (sample.state != 0) {
        current_.emplace(effect, sample);
      }
    }
  }
}

EffectPoolTrack::Counts
EffectPoolTrack::inputs(float t, std::vector<Core::SectionReplay::Input> &out) const {
  Counts counts;
  for (const auto &[effect, to] : current_) {
    const auto from = previous_.find(effect);
    if (from == previous_.end() || from->second.state != to.state || from->second.base != to.base) {
      ++counts.unpaired;
      continue;
    }
    ++counts.paired;
    out.push_back(
        replayInput(effect + addresses_.position, lerp(from->second.position, to.position, t)));
  }
  return counts;
}

} // namespace z3d::app
