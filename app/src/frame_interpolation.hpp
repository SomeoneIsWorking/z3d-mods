// Frame interpolation by replay (docs/plans/replay-interpolation.md): the game's draw section,
// named by the manifest's "interpolation" table, is hooked in the core (core/section_replay.h).
// After each real tick it runs again in the core's sandbox with the camera between the last two
// ticks, and the GX commands it leaves in the bound command list make the in-between the renderer
// shows on the vblank before the tick's own frame.
#pragma once

#include "camera_track.hpp"
#include "gx_cmdlist.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace z3d::app {

class Emulator;

class FrameInterpolation {
public:
  // Census: the real section's SVCs, GX commands and pages each tick. Probe: also a replay with
  // nothing blended, its stores and GX commands compared byte for byte with the real section's.
  // On: an in-between each tick.
  enum class Mode : uint8_t { Off, Census, Probe, On };

  // `manifest` is the game's z3d-options.json; without "interpolation" nothing is hooked.
  FrameInterpolation(Emulator &emulator, const std::filesystem::path &manifest);
  ~FrameInterpolation();
  FrameInterpolation(const FrameInterpolation &) = delete;
  FrameInterpolation &operator=(const FrameInterpolation &) = delete;
  FrameInterpolation(FrameInterpolation &&) = delete;
  FrameInterpolation &operator=(FrameInterpolation &&) = delete;

  [[nodiscard]] bool available() const {
    return section_.has_value();
  }
  bool setMode(Mode mode);
  // What the sections and replays did since the last call, one line.
  [[nodiscard]] std::string takeReport();

private:
  // The game's draw section as the manifest names it.
  struct Section {
    uint32_t open;
    uint32_t close;
    GxCmdlist cmdlist;
    CameraTrack camera;
  };

  void onSection(Section &section);
  void census(Core::SectionReplay &replay, Section &section);
  void probe(Core::SectionReplay &replay, Section &section);
  void interpolate(Core::SectionReplay &replay, Section &section);

  Emulator &emulator_;
  std::optional<Section> section_;
  // The real section's output this tick.
  GxOutput real_;
  Mode mode_ = Mode::Off;

  struct Totals {
    uint64_t sections = 0;
    uint64_t sectionPages = 0;
    int64_t sectionMicroseconds = 0;
    uint64_t sectionHeldReads = 0;
    uint64_t replayHeldReads = 0;
    int64_t replayMicroseconds = 0;
    // By number and site, with an IPC request's service and header.
    std::map<std::string, uint64_t> svcs;
    // By GX command id.
    std::map<uint32_t, uint64_t> gx;
    uint64_t replays = 0;
    uint64_t refused = 0;
    std::string lastRefusal;
    uint64_t instructions = 0;
    uint64_t replayPages = 0;
    uint64_t identical = 0;
    uint64_t differentPages = 0;
    // Pages only the real window stored to that differ: another writer while the section ran,
    // or a store the replay did not make.
    std::map<uint32_t, uint64_t> windowOnly;
    uint64_t differentBytes = 0;
    std::string firstDifference;
    // Differing bytes by page, the replays' first differing offset in each.
    std::map<uint32_t, std::pair<uint64_t, uint32_t>> differentByPage;
    std::map<uint32_t, uint64_t> pageRegions;
    // Replays whose GX commands, lists included, equal the real section's byte for byte.
    uint64_t gxIdentical = 0;
    uint64_t listBytes = 0;
    std::string firstGxDifference;
    uint64_t cuts = 0;
    uint64_t inBetweens = 0;
  };
  Totals totals_;
};

} // namespace z3d::app
