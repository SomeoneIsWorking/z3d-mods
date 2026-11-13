#pragma once

#include <cstdint>
#include <span>
#include <vector>

struct retro_memory_map;
struct azahar_debug_interface;

namespace z3d::app {

// Guest virtual memory: through the fork's debug interface when the core offers it (every mapped
// page, live), else the regions a stock core publishes with RETRO_ENVIRONMENT_SET_MEMORY_MAPS.
class GuestMemory {
public:
  void assign(const retro_memory_map &map);
  void useDebugInterface(const azahar_debug_interface *debug) {
    debug_ = debug;
  }

  // False when [address, address + out.size()) is not inside one published region.
  [[nodiscard]] bool read(uint32_t address, std::span<uint8_t> out) const;
  [[nodiscard]] bool write(uint32_t address, std::span<const uint8_t> data) const;

private:
  struct Region {
    uint32_t start;
    uint32_t size;
    uint8_t *host;
  };

  [[nodiscard]] uint8_t *locate(uint32_t address, size_t size) const;

  std::vector<Region> regions_;
  const azahar_debug_interface *debug_ = nullptr;
};

} // namespace z3d::app
