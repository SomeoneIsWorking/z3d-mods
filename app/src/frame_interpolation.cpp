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

std::vector<uint32_t> fields(const lucent::json::Value &table, const char *name) {
  const lucent::json::Value *value = table.find(name);
  const lucent::json::Array *items = value != nullptr ? value->array() : nullptr;
  if (items == nullptr) {
    throw std::runtime_error(std::string("\"interpolation\" wants the list \"") + name + '"');
  }
  std::vector<uint32_t> out;
  for (const lucent::json::Value &item : *items) {
    const auto integer = item.integer();
    if (!integer) {
      throw std::runtime_error(std::string("\"interpolation\" wants numbers in \"") + name + '"');
    }
    out.push_back(static_cast<uint32_t>(*integer));
  }
  return out;
}

bool observeAll(Core::SectionReplay &replay, const std::vector<uint32_t> &sites) {
  for (const uint32_t site : sites) {
    if (!replay.Observe(site)) {
      return false;
    }
  }
  return true;
}

} // namespace

FrameInterpolation::FrameInterpolation(Emulator &emulator, const std::filesystem::path &manifest)
    : emulator_(emulator), leaks_(emulator) {
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
          .cameraSetting = field(*table, "camera_setting"),
          .cameraMode = field(*table, "camera_mode"),
          .cameraAnimState = field(*table, "camera_anim_state"),
          .settings = field(*table, "camera_settings"),
          .settingSize = field(*table, "camera_setting_size"),
          .settingModes = field(*table, "camera_setting_modes"),
          .modeSize = field(*table, "camera_mode_size"),
          .modeFunction = field(*table, "camera_mode_function"),
          .doorFunction = field(*table, "door_camera_function"),
          .doorFarSide = field(*table, "door_camera_far_side"),
          .cutsceneEyeShot = field(*table, "cutscene_eye_shot"),
          .cutsceneAtShot = field(*table, "cutscene_at_shot"),
          .cutsceneShot = field(*table, "cutscene_shot"),
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
      .fades = FadeTrack(FadeAddresses{
          .play = field(*table, "play"),
          .transition = field(*table, "transition"),
          .transitionDraw = field(*table, "transition_draw"),
          .flash = field(*table, "fade_flash"),
          .fadeDraw = field(*table, "fade_draw"),
          .type = field(*table, "fade_type"),
          .color = field(*table, "fade_color"),
      }),
      .materials = MaterialTrack(MaterialAddresses{
          .stepCalls = fields(*table, "material_step_calls"),
          .stepArg = field(*table, "material_step_arg"),
          .anim = field(*table, "material_anim"),
          .frame = field(*table, "material_frame"),
          .speed = field(*table, "material_speed"),
          .loop = field(*table, "material_loop"),
          .paused = field(*table, "material_paused"),
          .animOffset = field(*table, "cmab_anim_offset"),
          .frameCount = field(*table, "cmab_frame_count"),
      }),
      .effects = EffectPoolTrack(EffectPoolAddresses{
          .play = field(*table, "play"),
          .count = field(*table, "effect_count"),
          .id = field(*table, "effect_actor"),
          .params = field(*table, "actor_params"),
          .paramsValue = field(*table, "effect_actor_params"),
          .effects = field(*table, "effects"),
          .effectSize = field(*table, "effect_size"),
          .state = field(*table, "effect_state"),
          .position = field(*table, "effect_position"),
          .base = field(*table, "effect_base"),
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
                                    !replay.Observe(section.skeletons.drawCall()) ||
                                    !observeAll(replay, section.materials.stepCalls()))) {
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
  } else if (mode_ == Mode::On || mode_ == Mode::LeakCheck) {
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
  section.fades.sample(replay);
  section.materials.sample(replay);
  section.effects.sample(replay, section.actors);
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
  const FadeTrack::Counts fades = section.fades.inputs(kHalfway, *inputs);
  totals_.fadesPaired += fades.paired;
  totals_.fadesUnpaired += fades.unpaired;
  const MaterialTrack::Counts materials = section.materials.inputs(kHalfway, *inputs);
  totals_.materialsPaired += materials.paired;
  totals_.materialsUnpaired += materials.unpaired;
  const EffectPoolTrack::Counts effects = section.effects.inputs(kHalfway, *inputs);
  totals_.effectsPaired += effects.paired;
  totals_.effectsUnpaired += effects.unpaired;
  VideoCore::GPU &gpu = emulator_.gpu();
  const bool checking = mode_ == Mode::LeakCheck;
  if (checking) {
    leaks_.before();
  }
  const Core::SectionReplay::Result result = replay.Replay(*inputs, [&] {
    const VideoCore::GPU::DetachedRun run = gpu.RunDetached(section.cmdlist.read().commands);
    totals_.inBetweens += run.in_betweens;
    totals_.gpuSkipped += run.skipped;
  });
  if (checking) {
    leaks_.after();
  }
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
         << " in_betweens=" << totals_.inBetweens << " gpu_skipped=" << totals_.gpuSkipped;
  report << " actors_paired=" << totals_.actorsPaired
         << " actors_unpaired=" << totals_.actorsUnpaired
         << " skeletons_paired=" << totals_.skeletonsPaired
         << " skeletons_unpaired=" << totals_.skeletonsUnpaired
         << " fades_paired=" << totals_.fadesPaired << " fades_unpaired=" << totals_.fadesUnpaired
         << " materials_paired=" << totals_.materialsPaired
         << " materials_unpaired=" << totals_.materialsUnpaired
         << " effects_paired=" << totals_.effectsPaired
         << " effects_unpaired=" << totals_.effectsUnpaired;
  if (!totals_.firstGxDifference.empty()) {
    report << " gx_first=" << totals_.firstGxDifference;
  }
  if (!totals_.firstDifference.empty()) {
    report << " first=" << totals_.firstDifference << " pages=";
    for (const auto &[page, bytes] : totals_.differentByPage) {
      report << std::hex << page + bytes.second << std::dec << ':' << bytes.first << ',';
    }
  }
  report << ' ' << leaks_.takeReport();
  totals_ = {};
  return report.str();
}

} // namespace z3d::app
