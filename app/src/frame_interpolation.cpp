#include "frame_interpolation.hpp"

#include "emulator.hpp"

#include "core/section_replay.h"
#include "video_core/gpu.h"

#include <lucent/json.h>
#include <lucent/log.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace z3d::app {
namespace {

// The in-between's place between the previous tick and this one.
constexpr float kHalfway = 0.5F;

uint32_t field(const lucent::json::Value &table, const char *name) {
  const lucent::json::Value *value = table.find(name);
  const auto integer = value != nullptr ? value->integer() : std::nullopt;
  if (!integer) {
    throw std::runtime_error(std::string("\"interpolation\" wants \"") + name + '"');
  }
  return static_cast<uint32_t>(*integer);
}

} // namespace

FrameInterpolation::FrameInterpolation(Emulator &emulator, const std::filesystem::path &manifest)
    : emulator_(emulator) {
  std::ifstream file(manifest);
  const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(json, error);
  const lucent::json::Value *table = document ? document->find("interpolation") : nullptr;
  if (table == nullptr) {
    return;
  }
  section_.emplace(Section{
      .open = field(*table, "open_site"),
      .close = field(*table, "close_site"),
      .cmdlist = GxCmdlist(emulator_,
                           {
                               .boundCmdlist = field(*table, "bound_cmdlist"),
                               .requests = field(*table, "cmdlist_requests"),
                               .requestCount = field(*table, "cmdlist_request_count"),
                               .requestSize = field(*table, "request_size"),
                           }),
      .camera = CameraTrack(CameraAddresses{
          .play = field(*table, "play"),
          .frames = field(*table, "play_frames"),
          .viewEye = field(*table, "view_eye"),
          .viewAt = field(*table, "view_at"),
          .viewUp = field(*table, "view_up"),
          .viewFovy = field(*table, "view_fovy"),
          .cameras = field(*table, "cameras"),
          .activeCamera = field(*table, "active_camera"),
          .cameraEye = field(*table, "camera_eye"),
          .cameraAt = field(*table, "camera_at"),
      }),
      .actors = ActorTrack(ActorAddresses{
          .play = field(*table, "play"),
          .lists = field(*table, "actor_lists"),
          .listCount = field(*table, "actor_list_count"),
          .listSize = field(*table, "actor_list_size"),
          .listHead = field(*table, "actor_list_head"),
          .next = field(*table, "actor_next"),
          .id = field(*table, "actor_id"),
          .position = field(*table, "actor_position"),
          .shapeRotation = field(*table, "actor_shape_rotation"),
      }),
      .skeletons = SkeletonTrack(SkeletonAddresses{
          .drawCall = field(*table, "skeleton_draw_call"),
          .drawArg = field(*table, "skeleton_draw_arg"),
          .model = field(*table, "skeleton_model"),
          .limbCount = field(*table, "skeleton_limb_count"),
          .jointTable = field(*table, "skeleton_joint_table"),
          .jointSize = field(*table, "joint_size"),
      }),
  });
}

FrameInterpolation::~FrameInterpolation() {
  setMode(Mode::Off);
}

bool FrameInterpolation::setMode(Mode mode) {
  if (!section_) {
    return mode == Mode::Off;
  }
  Section &section = *section_;
  Core::SectionReplay &replay = emulator_.sectionReplay();
  if (mode == Mode::Off) {
    replay.Uninstall();
  } else if (mode_ == Mode::Off && (!replay.Install(section.open, section.close,
                                                    [this, &section](Core::SectionReplay &) {
                                                      onSection(section);
                                                    }) ||
                                    !replay.Observe(section.skeletons.drawCall()))) {
    replay.Uninstall();
    return false;
  }
  mode_ = mode;
  return true;
}

void FrameInterpolation::onSection(Section &section) {
  Core::SectionReplay &replay = emulator_.sectionReplay();
  census(replay, section);
  if (mode_ == Mode::Probe) {
    probe(replay, section);
  } else if (mode_ == Mode::On) {
    interpolate(replay, section);
  }
}

void FrameInterpolation::census(Core::SectionReplay &replay, Section &section) {
  const Core::SectionReplay::Census &census = replay.SectionCensus();
  ++totals_.sections;
  totals_.sectionPages += census.pages;
  totals_.sectionMicroseconds += census.host_microseconds;
  totals_.sectionHeldReads += census.held_reads;
  for (const Core::SectionReplay::Svc &svc : census.svcs) {
    std::ostringstream key;
    key << std::hex << (svc.other_thread ? "other:" : "") << svc.number << '@' << svc.pc;
    if (!svc.service.empty()) {
      key << ':' << svc.service << ':' << svc.detail;
    } else if (svc.address != 0) {
      key << ':' << svc.address << ':' << svc.detail;
    }
    ++totals_.svcs[key.str()];
  }
  real_ = section.cmdlist.read();
  for (const Service::GSP::Command &command : real_.commands) {
    ++totals_.gx[static_cast<uint32_t>(command.id.Value())];
  }
  for (const VAddr page : replay.SectionPages()) {
    ++totals_.pageRegions[page >> 20];
  }
}

void FrameInterpolation::interpolate(Core::SectionReplay &replay, Section &section) {
  section.camera.sample(replay);
  section.actors.sample(replay);
  section.skeletons.sample(replay);
  auto inputs = section.camera.inputs(kHalfway);
  if (!inputs) {
    ++totals_.cuts;
    return;
  }
  const ActorTrack::Counts actors = section.actors.inputs(kHalfway, *inputs);
  totals_.actorsPaired += actors.paired;
  totals_.actorsUnpaired += actors.unpaired;
  const SkeletonTrack::Counts skeletons = section.skeletons.inputs(kHalfway, *inputs);
  totals_.skeletonsPaired += skeletons.paired;
  totals_.skeletonsUnpaired += skeletons.unpaired;
  VideoCore::GPU &gpu = emulator_.gpu();
  const Core::SectionReplay::Result result = replay.Replay(*inputs, [&] {
    totals_.inBetweens += gpu.RunDetached(section.cmdlist.read().commands);
  });
  ++totals_.replays;
  totals_.replayMicroseconds += result.host_microseconds;
  if (!result.done) {
    ++totals_.refused;
    totals_.lastRefusal = result.refusal;
    return;
  }
  totals_.instructions += result.instructions;
  totals_.replayPages += result.pages;
}

void FrameInterpolation::probe(Core::SectionReplay &replay, Section &section) {
  std::vector<Core::SectionReplay::Difference> differences;
  std::string gxDifference;
  const Core::SectionReplay::Result result = replay.Replay({}, [&] {
    differences = replay.DiffWithSection();
    const GxOutput replayed = section.cmdlist.read();
    gxDifference = compareGx(real_, replayed);
    for (const std::vector<uint8_t> &list : replayed.lists) {
      totals_.listBytes += list.size();
    }
  });
  ++totals_.replays;
  totals_.replayMicroseconds += result.host_microseconds;
  if (!result.done) {
    ++totals_.refused;
    totals_.lastRefusal = result.refusal;
    return;
  }
  totals_.instructions += result.instructions;
  totals_.replayPages += result.pages;
  totals_.replayHeldReads += result.held_reads;
  if (gxDifference.empty()) {
    ++totals_.gxIdentical;
  } else if (totals_.firstGxDifference.empty()) {
    totals_.firstGxDifference = gxDifference;
  }
  bool identical = true;
  for (const Core::SectionReplay::Difference &difference : differences) {
    if (!difference.replayed) {
      totals_.windowOnly[difference.page] += difference.bytes;
      continue;
    }
    identical = false;
    ++totals_.differentPages;
    totals_.differentBytes += difference.bytes;
    auto &page = totals_.differentByPage[difference.page];
    page.first += difference.bytes;
    page.second = difference.first;
    if (totals_.firstDifference.empty()) {
      std::ostringstream text;
      text << std::hex << difference.page + difference.first << '+' << std::dec << difference.bytes;
      totals_.firstDifference = text.str();
    }
  }
  if (identical) {
    ++totals_.identical;
  }
}

std::string FrameInterpolation::takeReport() {
  std::ostringstream report;
  report << "sections=" << totals_.sections << " section_pages=" << totals_.sectionPages
         << " section_us=" << totals_.sectionMicroseconds
         << " replay_us=" << totals_.replayMicroseconds
         << " section_held_reads=" << totals_.sectionHeldReads
         << " replay_held_reads=" << totals_.replayHeldReads << " svcs=";
  for (const auto &[svc, count] : totals_.svcs) {
    report << svc << '=' << count << ',';
  }
  report << " gx=";
  for (const auto &[id, count] : totals_.gx) {
    report << id << ':' << count << ',';
  }
  report << " regions=";
  for (const auto &[megabyte, count] : totals_.pageRegions) {
    report << std::hex << (megabyte << 20) << std::dec << ':' << count << ',';
  }
  report << " window_only=";
  for (const auto &[page, bytes] : totals_.windowOnly) {
    report << std::hex << page << std::dec << ':' << bytes << ',';
  }
  report << " replays=" << totals_.replays << " refused=" << totals_.refused;
  if (!totals_.lastRefusal.empty()) {
    report << " (" << totals_.lastRefusal << ')';
  }
  report << " instructions=" << totals_.instructions << " replay_pages=" << totals_.replayPages
         << " identical=" << totals_.identical << " different_pages=" << totals_.differentPages
         << " different_bytes=" << totals_.differentBytes << " gx_identical=" << totals_.gxIdentical
         << " list_bytes=" << totals_.listBytes << " cuts=" << totals_.cuts
         << " in_betweens=" << totals_.inBetweens;
  report << " actors_paired=" << totals_.actorsPaired
         << " actors_unpaired=" << totals_.actorsUnpaired
         << " skeletons_paired=" << totals_.skeletonsPaired
         << " skeletons_unpaired=" << totals_.skeletonsUnpaired;
  if (!totals_.firstGxDifference.empty()) {
    report << " gx_first=" << totals_.firstGxDifference;
  }
  if (!totals_.firstDifference.empty()) {
    report << " first=" << totals_.firstDifference << " pages=";
    for (const auto &[page, bytes] : totals_.differentByPage) {
      report << std::hex << page + bytes.second << std::dec << ':' << bytes.first << ',';
    }
  }
  totals_ = {};
  return report.str();
}

} // namespace z3d::app
