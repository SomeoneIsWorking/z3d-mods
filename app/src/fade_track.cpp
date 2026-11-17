#include "fade_track.hpp"

#include "motion_blend.hpp"
#include "section_memory.hpp"

namespace z3d::app {

void FadeTrack::sample(Core::SectionReplay &replay) {
  const auto play = readStart<uint32_t>(replay, addresses_.play);
  previous_ = play == play_ ? std::move(current_) : decltype(previous_){};
  current_.clear();
  play_ = play;
  if (play == 0) {
    return;
  }
  const auto take = [&](uint32_t fade) {
    current_[fade] = Sample{
        .type = readStart<uint8_t>(replay, fade + addresses_.type),
        .color = readStart<std::array<uint8_t, 4>>(replay, fade + addresses_.color),
    };
  };
  if (readStart<uint32_t>(replay, play + addresses_.transitionDraw) == addresses_.fadeDraw) {
    take(play + addresses_.transition);
  }
  take(play + addresses_.flash);
}

FadeTrack::Counts FadeTrack::inputs(float t, std::vector<Core::SectionReplay::Input> &out) const {
  Counts counts;
  for (const auto &[fade, to] : current_) {
    const auto from = previous_.find(fade);
    if (from == previous_.end() || from->second.type != to.type) {
      ++counts.unpaired;
      continue;
    }
    ++counts.paired;
    if (from->second.color == to.color) {
      continue;
    }
    std::array<uint8_t, 4> color{};
    for (std::size_t i = 0; i < color.size(); ++i) {
      color[i] = lerpByte(from->second.color[i], to.color[i], t);
    }
    out.push_back(replayInput(fade + addresses_.color, color));
  }
  return counts;
}

} // namespace z3d::app
