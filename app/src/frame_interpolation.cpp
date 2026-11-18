#include "frame_interpolation.hpp"

#include "emulator.hpp"
#include "section_memory.hpp"

#include "core/section_replay.h"
#include "video_core/gpu.h"

#include <lucent/json.h>
#include <lucent/log.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>
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

bool has(const lucent::json::Value &table, const char *name) {
  return table.find(name) != nullptr;
}

bool observeAll(Core::SectionReplay &replay, const std::vector<uint32_t> &sites,
                bool lead = false) {
  for (const uint32_t site : sites) {
    if (!(lead ? replay.ObserveLead(site) : replay.Observe(site))) {
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
  if (!document) {
    return;
  }
  const lucent::json::Value *table = document->find("interpolation");
  if (table == nullptr) {
    return;
  }
  Section section{
      .open = field(*table, "open_site"),
      .close = field(*table, "close_site"),
      .cmdlist = GxCmdlist(emulator_,
                           {
                               .boundCmdlist = field(*table, "bound_cmdlist"),
                               .requests = field(*table, "cmdlist_requests"),
                               .requestCount = field(*table, "cmdlist_request_count"),
                               .requestSize = field(*table, "request_size"),
                           }),
  };
  // Each track runs when the table names its first key, and then wants all of its keys.
  if (has(*table, "view_eye")) {
    CameraAddresses camera{
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
    };
    if (has(*table, "door_camera_function")) {
      camera.door = DoorCutAddresses{
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
      };
    }
    if (has(*table, "cutscene_shot")) {
      camera.shot = ShotCutAddresses{
          .eyeShot = field(*table, "cutscene_eye_shot"),
          .atShot = field(*table, "cutscene_at_shot"),
          .shot = field(*table, "cutscene_shot"),
      };
    }
    if (has(*table, "cutscene_group_pending")) {
      camera.group = GroupCutAddresses{
          .eyeType = field(*table, "cutscene_eye_group_type"),
          .atType = field(*table, "cutscene_at_group_type"),
          .pending = field(*table, "cutscene_group_pending"),
      };
    }
    section.camera.emplace(camera);
  }
  if (has(*table, "actor_lists")) {
    section.actors.emplace(ActorAddresses{
        .play = field(*table, "play"),
        .lists = field(*table, "actor_lists"),
        .listCount = field(*table, "actor_list_count"),
        .listSize = field(*table, "actor_list_size"),
        .listHead = field(*table, "actor_list_head"),
        .next = field(*table, "actor_next"),
        .id = field(*table, "actor_id"),
        .position = field(*table, "actor_position"),
        .shapeRotation = field(*table, "actor_shape_rotation"),
        .drawPhase = has(*table, "actor_draw_phase")
                         ? std::optional(ActorAddresses::DrawPhase{
                               .id = static_cast<int16_t>(field(*table, "actor_draw_phase_id")),
                               .offset = field(*table, "actor_draw_phase"),
                           })
                         : std::nullopt,
    });
  }
  if (has(*table, "skeleton_draw_calls")) {
    section.skeletons.emplace(SkeletonAddresses{
        .drawCalls = fields(*table, "skeleton_draw_calls"),
        .drawArg = field(*table, "skeleton_draw_arg"),
        .model = field(*table, "skeleton_model"),
        .limbCount = field(*table, "skeleton_limb_count"),
        .jointTable = field(*table, "skeleton_joint_table"),
        .jointSize = field(*table, "joint_size"),
    });
  }
  if (has(*table, "transition")) {
    section.fades.emplace(FadeAddresses{
        .play = field(*table, "play"),
        .transition = field(*table, "transition"),
        .transitionDraw = field(*table, "transition_draw"),
        .flash = field(*table, "fade_flash"),
        .fadeDraw = field(*table, "fade_draw"),
        .type = field(*table, "fade_type"),
        .color = field(*table, "fade_color"),
    });
  }
  // An animation is stepped in the draw (material_step_calls) or before it (material_lead_calls).
  const bool stepped = has(*table, "material_step_calls");
  const bool led = has(*table, "material_lead_calls");
  if (stepped || led) {
    section.materials.emplace(MaterialAddresses{
        .stepCalls = stepped ? fields(*table, "material_step_calls") : std::vector<uint32_t>{},
        .leadCalls = led ? fields(*table, "material_lead_calls") : std::vector<uint32_t>{},
        .stepArg = field(*table, "material_step_arg"),
        .update = has(*table, "material_update")
                      ? std::optional<uint32_t>(field(*table, "material_update"))
                      : std::nullopt,
        .anim = field(*table, "material_anim"),
        .frame = field(*table, "material_frame"),
        .speed = field(*table, "material_speed"),
        .loop = field(*table, "material_loop"),
        .paused = field(*table, "material_paused"),
        .animFile = field(*table, "material_anim_file"),
        .animOffset = field(*table, "cmab_anim_offset"),
        .frameCount = field(*table, "cmab_frame_count"),
    });
  }
  if (has(*table, "effects")) {
    section.effects.emplace(EffectPoolAddresses{
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
        .alpha = field(*table, "effect_alpha"),
    });
  }
  const lucent::json::Value *link = document->find("link");
  if (link != nullptr && has(*link, "draw_fraction")) {
    section.drawFraction = field(*link, "draw_fraction");
  }
  if (has(*table, "held_lock")) {
    section.heldLock.emplace(HeldLockAddresses{
        .lock = field(*table, "held_lock"),
        .event = field(*table, "lock_event"),
        .owner = field(*table, "lock_owner"),
        .count = field(*table, "lock_count"),
    });
  }
  // The camera track declares the cuts every blend obeys; the effects live in paired actors.
  const bool blends = section.actors || section.skeletons || section.fades || section.materials;
  if (blends && !section.camera) {
    throw std::runtime_error("\"interpolation\" blends tracks without the camera's cuts");
  }
  if (section.effects && !section.actors) {
    throw std::runtime_error("\"interpolation\" has effect pools without the actor track");
  }
  section_.emplace(std::move(section));
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
  } else if (mode_ == Mode::Off &&
             (!replay.Install(section.open, section.close,
                              [this, &section](Core::SectionReplay &) {
                                onSection(section);
                              }) ||
              (section.skeletons && !observeAll(replay, section.skeletons->drawCalls())) ||
              (section.materials && (!observeAll(replay, section.materials->stepCalls()) ||
                                     !observeAll(replay, section.materials->leadCalls(), true))))) {
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
    key << std::hex << (svc.other_thread ? "other:" : "") << svc.number << '@' << svc.pc << '<'
        << svc.lr;
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

std::vector<Core::SectionReplay::Input> FrameInterpolation::startInputs(Core::SectionReplay &replay,
                                                                        const Section &section) {
  std::vector<Core::SectionReplay::Input> inputs;
  if (section.heldLock && section.heldLock->release(replay, emulator_.runningThreadTls(), inputs)) {
    ++totals_.locksReleased;
  }
  return inputs;
}

void FrameInterpolation::interpolate(Core::SectionReplay &replay, Section &section) {
  if (section.camera) {
    section.camera->sample(replay);
  }
  if (section.actors) {
    section.actors->sample(replay);
  }
  if (section.skeletons) {
    section.skeletons->sample(replay);
  }
  if (section.fades) {
    section.fades->sample(replay);
  }
  if (section.materials) {
    section.materials->sample(replay);
  }
  if (section.effects && section.actors) {
    section.effects->sample(replay, *section.actors);
  }
  std::vector<Core::SectionReplay::Input> inputs;
  if (section.camera) {
    auto camera = section.camera->inputs(kHalfway);
    if (!camera) {
      ++totals_.cuts;
      return;
    }
    inputs = std::move(*camera);
  }
  for (Core::SectionReplay::Input &input : startInputs(replay, section)) {
    inputs.push_back(std::move(input));
  }
  const auto blend = [&inputs](const auto &track, Pairing &pairing) {
    if (track) {
      const auto counts = track->inputs(kHalfway, inputs);
      pairing.paired += counts.paired;
      pairing.unpaired += counts.unpaired;
    }
  };
  blend(section.actors, totals_.actors);
  blend(section.skeletons, totals_.skeletons);
  blend(section.fades, totals_.fades);
  blend(section.effects, totals_.effects);
  std::vector<Core::SectionReplay::Call> leads;
  if (section.materials) {
    const auto counts = section.materials->inputs(kHalfway, inputs, leads);
    totals_.materials.paired += counts.paired;
    totals_.materials.unpaired += counts.unpaired;
  }
  if (section.drawFraction) {
    inputs.push_back(replayInput(*section.drawFraction, kHalfway - 1.0F));
  }
  VideoCore::GPU &gpu = emulator_.gpu();
  const bool checking = mode_ == Mode::LeakCheck;
  if (checking) {
    leaks_.before();
  }
  const Core::SectionReplay::Result result = replay.Replay(inputs, leads, [&] {
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
  const Core::SectionReplay::Result result = replay.Replay(startInputs(replay, section), {}, [&] {
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

std::string FrameInterpolation::takeOutcomes() {
  std::ostringstream outcomes;
  outcomes << "replays=" << totals_.replays << " refused=" << totals_.refused;
  if (!totals_.lastRefusal.empty()) {
    outcomes << " (" << totals_.lastRefusal << ')';
  }
  outcomes << " cuts=" << totals_.cuts << " in_betweens=" << totals_.inBetweens;
  totals_ = {};
  return outcomes.str();
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
  report << " replays=" << totals_.replays << " refused=" << totals_.refused
         << " locks_released=" << totals_.locksReleased;
  if (!totals_.lastRefusal.empty()) {
    report << " (" << totals_.lastRefusal << ')';
  }
  report << " instructions=" << totals_.instructions << " replay_pages=" << totals_.replayPages
         << " identical=" << totals_.identical << " different_pages=" << totals_.differentPages
         << " different_bytes=" << totals_.differentBytes << " gx_identical=" << totals_.gxIdentical
         << " list_bytes=" << totals_.listBytes << " cuts=" << totals_.cuts
         << " in_betweens=" << totals_.inBetweens << " gpu_skipped=" << totals_.gpuSkipped;
  report << " actors_paired=" << totals_.actors.paired
         << " actors_unpaired=" << totals_.actors.unpaired
         << " skeletons_paired=" << totals_.skeletons.paired
         << " skeletons_unpaired=" << totals_.skeletons.unpaired
         << " fades_paired=" << totals_.fades.paired << " fades_unpaired=" << totals_.fades.unpaired
         << " materials_paired=" << totals_.materials.paired
         << " materials_unpaired=" << totals_.materials.unpaired
         << " effects_paired=" << totals_.effects.paired
         << " effects_unpaired=" << totals_.effects.unpaired;
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
