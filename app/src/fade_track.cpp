#include "fade_track.hpp"

#include "motion_blend.hpp"
#include "section_memory.hpp"

namespace z3d::app {

void FadeTrack::sample(Core::SectionReplay &replay) {
  const auto play = readStart<uint32_t>(replay, addresses_.play);
  previous_ = play == play_ ? std::move(current_) : decltype(previous_){};
  current_.clear();
  previousCard_ = play == play_ ? currentCard_ : std::nullopt;
  currentCard_.reset();
  play_ = play;
  if (play == 0) {
    return;
  }
  if (addresses_.titleCard) {
    const auto card = play + addresses_.titleCard->card;
    const auto fadeAddress = card + addresses_.titleCard->fade;
    currentCard_ = CardSample{
        .fadeAddress = fadeAddress,
        .texture = readStart<uint32_t>(replay, card + addresses_.titleCard->texture),
        .fade = readStart<std::array<int16_t, 2>>(replay, fadeAddress),
    };
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
  if (currentCard_ && previousCard_ && currentCard_->texture == previousCard_->texture &&
      currentCard_->fade != previousCard_->fade) {
    std::array<int16_t, 2> fade{};
    for (std::size_t i = 0; i < fade.size(); ++i) {
      fade[i] = lerpShort(previousCard_->fade[i], currentCard_->fade[i], t);
    }
    out.push_back(replayInput(currentCard_->fadeAddress, fade));
  }
  return counts;
}

} // namespace z3d::app
