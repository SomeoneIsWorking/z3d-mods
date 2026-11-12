#include "guest_memory.hpp"

#include <azahar_debug.h>
#include <libretro.h>
#include <lucent/log.h>

#include <cstring>

namespace z3d::harness {

void GuestMemory::assign(const retro_memory_map &map) {
  regions_.clear();
  for (unsigned i = 0; i < map.num_descriptors; ++i) {
    const retro_memory_descriptor &desc = map.descriptors[i];
    if (desc.ptr == nullptr || desc.len == 0) {
      continue;
    }
    regions_.push_back({static_cast<uint32_t>(desc.start), static_cast<uint32_t>(desc.len),
                        static_cast<uint8_t *>(desc.ptr) + desc.offset});
    lucent::info("memory", "region 0x{:08X} size 0x{:X}", desc.start, desc.len);
  }
}

uint8_t *GuestMemory::locate(uint32_t address, size_t size) const {
  for (const Region &region : regions_) {
    if (address >= region.start && size <= region.size &&
        address - region.start <= region.size - size) {
      return region.host + (address - region.start);
    }
  }
  return nullptr;
}

bool GuestMemory::read(uint32_t address, std::span<uint8_t> out) const {
  if (debug_ != nullptr) {
    return debug_->read_memory(address, out.data(), static_cast<uint32_t>(out.size()));
  }
  const uint8_t *host = locate(address, out.size());
  if (host == nullptr) {
    return false;
  }
  std::memcpy(out.data(), host, out.size());
  return true;
}

bool GuestMemory::write(uint32_t address, std::span<const uint8_t> data) const {
  if (debug_ != nullptr) {
    return debug_->write_memory(address, data.data(), static_cast<uint32_t>(data.size()));
  }
  uint8_t *host = locate(address, data.size());
  if (host == nullptr) {
    return false;
  }
  std::memcpy(host, data.data(), data.size());
  return true;
}

} // namespace z3d::harness
