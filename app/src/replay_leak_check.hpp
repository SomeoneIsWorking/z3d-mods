// The replay's leak check (docs/plans/replay-interpolation.md): the game's state a replay must
// leave alone, captured before and after it: the rasterizer cache's surfaces (contents read back),
// dirty regions, cached pages and texture cubes, and physical memory (FCRAM, VRAM).
#pragma once

#include "video_core/rasterizer_interface.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace z3d::app {

class Emulator;

class ReplayLeakCheck {
public:
  explicit ReplayLeakCheck(Emulator &emulator) : emulator_(emulator) {
  }

  void before();
  void after();
  // Replays checked, leaking replays and what leaked, one line; the totals start again.
  [[nodiscard]] std::string takeReport();

private:
  struct Region {
    uint32_t address;
    std::vector<uint8_t> bytes;
  };
  [[nodiscard]] std::vector<Region> captureMemory();

  Emulator &emulator_;
  VideoCore::CacheState cache_;
  std::vector<Region> memory_;

  uint64_t checked_ = 0;
  uint64_t leaking_ = 0;
  // Leaked items by kind (surface, dirty, pages, cube, memory page) over all replays.
  std::map<std::string, uint64_t> kinds_;
  // The first leak of each kind, spelled out.
  std::map<std::string, std::string> examples_;
};

} // namespace z3d::app
