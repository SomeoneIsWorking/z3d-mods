// Frame interpolation by replay (docs/plans/replay-interpolation.md): the game's draw section,
// named by the manifest's "interpolation" table, is hooked in the core (core/section_replay.h) and
// run again in its sandbox after each real tick.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace z3d::app {

class Emulator;

class FrameInterpolation {
public:
  // Census: the real section's SVCs and pages each tick. Probe: also a replay with nothing
  // blended, compared byte for byte with what the real section left.
  enum class Mode : uint8_t { Off, Census, Probe };

  // `manifest` is the game's z3d-options.json; without "interpolation" nothing is hooked.
  FrameInterpolation(Emulator &emulator, const std::filesystem::path &manifest);
  ~FrameInterpolation();
  FrameInterpolation(const FrameInterpolation &) = delete;
  FrameInterpolation &operator=(const FrameInterpolation &) = delete;
  FrameInterpolation(FrameInterpolation &&) = delete;
  FrameInterpolation &operator=(FrameInterpolation &&) = delete;

  [[nodiscard]] bool available() const {
    return callSite_.has_value();
  }
  bool setMode(Mode mode);
  // Hooks `site`, a candidate section's call, in place of the manifest's while off.
  bool setCallSite(uint32_t site);
  // What the sections and replays did since the last call, one line.
  [[nodiscard]] std::string takeReport();

private:
  void onSection();

  Emulator &emulator_;
  std::optional<uint32_t> callSite_;
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
  };
  Totals totals_;
};

} // namespace z3d::app
