#include "material_track.hpp"

#include "section_memory.hpp"

#include <utility>

namespace z3d::app {

MaterialTrack::MaterialTrack(MaterialAddresses addresses)
    : addresses_(std::move(addresses)),
      stepSites_(addresses_.stepCalls.begin(), addresses_.stepCalls.end()),
      leadSites_(addresses_.leadCalls.begin(), addresses_.leadCalls.end()) {
}

void MaterialTrack::sample(Core::SectionReplay &replay) {
  previous_ = std::move(current_);
  current_.clear();
  const Core::SectionReplay::Census &census = replay.SectionCensus();
  for (const Core::SectionReplay::Call &call : census.calls) {
    if (stepSites_.contains(call.site)) {
      take(replay, call, false);
    }
  }
  for (const Core::SectionReplay::Call &call : census.lead_calls) {
    if (leadSites_.contains(call.site)) {
      take(replay, call, true);
    }
  }
}

void MaterialTrack::take(Core::SectionReplay &replay, const Core::SectionReplay::Call &call,
                         bool lead) {
  if (addresses_.update && call.target != *addresses_.update) {
    return;
  }
  const uint32_t player = call.args.at(addresses_.stepArg);
  if (current_.contains(player)) {
    return;
  }
  Sample sample;
  sample.call = call;
  sample.lead = lead;
  sample.anim = readStart<uint32_t>(replay, player + addresses_.anim);
  if (sample.anim == 0) {
    return;
  }
  const auto file = readStart<uint32_t>(replay, sample.anim + addresses_.animFile);
  const auto block = file + readStart<uint32_t>(replay, file + addresses_.animOffset);
  sample.frameCount = readStart<int32_t>(replay, block + addresses_.frameCount);
  sample.frame = readStart<float>(replay, player + addresses_.frame);
  sample.speed = readStart<float>(replay, player + addresses_.speed);
  sample.loop = readStart<uint8_t>(replay, player + addresses_.loop) != 0;
  sample.paused = readStart<uint8_t>(replay, player + addresses_.paused) != 0;
  current_.emplace(player, sample);
}

float MaterialTrack::stepped(const Sample &from) {
  const float frame = from.frame + from.speed;
  const auto count = static_cast<float>(from.frameCount);
  if (frame >= count) {
    return from.loop ? frame - count : count;
  }
  if (frame < 0.0F) {
    return from.loop ? frame + count : 0.0F;
  }
  return frame;
}

MaterialTrack::Counts MaterialTrack::inputs(float t, std::vector<Core::SectionReplay::Input> &out,
                                            std::vector<Core::SectionReplay::Call> &leads) const {
  Counts counts;
  for (const auto &[player, to] : current_) {
    const auto from = previous_.find(player);
    if (from == previous_.end() || from->second.anim != to.anim || from->second.paused ||
        from->second.speed != to.speed || stepped(from->second) != to.frame) {
      ++counts.unpaired;
      continue;
    }
    ++counts.paired;
    if (to.speed == 0.0F) {
      continue;
    }
    // The replay steps it once more and wraps it: in the draw from the frame before its step, or
    // in the lead call from the frame after it.
    const float step = to.lead ? to.speed : 0.0F;
    out.push_back(replayInput(player + addresses_.frame, from->second.frame + to.speed * t - step));
    if (to.lead) {
      leads.push_back(to.call);
    }
  }
  return counts;
}

} // namespace z3d::app
