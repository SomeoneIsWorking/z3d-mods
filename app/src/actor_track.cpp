#include "actor_track.hpp"

#include "section_memory.hpp"

namespace z3d::app {
namespace {

// More actors than the game can load: a list that runs longer is not walked further.
constexpr uint32_t kMaxActors = 0x400;

} // namespace

void ActorTrack::sample(Core::SectionReplay &replay) {
  const auto play = readStart<uint32_t>(replay, addresses_.play);
  previous_ = play == play_ ? std::move(current_) : decltype(previous_){};
  current_.clear();
  play_ = play;
  if (play == 0) {
    return;
  }
  uint32_t walked = 0;
  for (uint32_t list = 0; list < addresses_.listCount; ++list) {
    const uint32_t entry = play + addresses_.lists + list * addresses_.listSize;
    for (auto actor = readStart<uint32_t>(replay, entry + addresses_.listHead);
         actor != 0 && walked < kMaxActors;
         actor = readStart<uint32_t>(replay, actor + addresses_.next), ++walked) {
      current_[actor] = Sample{
          .id = readStart<int16_t>(replay, actor + addresses_.id),
          .position = readStart<Vec3>(replay, actor + addresses_.position),
          .shapeRotation = readStart<Vec3s>(replay, actor + addresses_.shapeRotation),
      };
    }
  }
}

ActorTrack::Counts ActorTrack::inputs(float t, std::vector<Core::SectionReplay::Input> &out) const {
  Counts counts;
  for (const auto &[actor, to] : current_) {
    const auto from = previous_.find(actor);
    if (from == previous_.end() || from->second.id != to.id) {
      ++counts.unpaired;
      continue;
    }
    ++counts.paired;
    out.push_back(
        replayInput(actor + addresses_.position, lerp(from->second.position, to.position, t)));
    out.push_back(replayInput(actor + addresses_.shapeRotation,
                              lerpAngles(from->second.shapeRotation, to.shapeRotation, t)));
  }
  return counts;
}

std::vector<uint32_t> ActorTrack::pairedWithId(int16_t id) const {
  std::vector<uint32_t> out;
  for (const auto &[actor, to] : current_) {
    const auto from = previous_.find(actor);
    if (to.id == id && from != previous_.end() && from->second.id == id) {
      out.push_back(actor);
    }
  }
  return out;
}

} // namespace z3d::app
