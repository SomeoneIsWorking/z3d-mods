#include "replay_leak_check.hpp"

#include "emulator.hpp"

#include "core/memory.h"
#include "video_core/gpu.h"

#include <cstring>
#include <sstream>

namespace z3d::app {
namespace {

constexpr uint32_t kPage = 0x1000;

std::string kindOf(const std::string &key) {
  return key.substr(0, key.find(' '));
}

// One line of the cache's state before and after a replay.
struct LineChange {
  const std::string &was;
  const std::string &now;
};

// The `name=value` fields that differ, by name: what changed in a surface.
std::string changedFields(const LineChange &change) {
  std::istringstream before(change.was);
  std::istringstream after(change.now);
  std::string field;
  std::string other;
  std::string names;
  while (before >> field && after >> other) {
    if (field != other) {
      names += '.' + field.substr(0, field.find('='));
    }
  }
  return names;
}

} // namespace

std::vector<ReplayLeakCheck::Region> ReplayLeakCheck::captureMemory() {
  std::vector<Region> regions;
  const auto take = [&](uint32_t address, uint32_t size) {
    const std::span<const uint8_t> bytes = emulator_.physical(address, size);
    if (!bytes.empty()) {
      regions.push_back({address, {bytes.begin(), bytes.end()}});
    }
  };
  take(Memory::VRAM_PADDR, Memory::VRAM_SIZE);
  if (emulator_.physical(Memory::FCRAM_PADDR, Memory::FCRAM_N3DS_SIZE).empty()) {
    take(Memory::FCRAM_PADDR, Memory::FCRAM_SIZE);
  } else {
    take(Memory::FCRAM_PADDR, Memory::FCRAM_N3DS_SIZE);
  }
  return regions;
}

void ReplayLeakCheck::before() {
  cache_ = emulator_.gpu().CaptureCacheState();
  memory_ = captureMemory();
}

void ReplayLeakCheck::after() {
  const VideoCore::CacheState cache = emulator_.gpu().CaptureCacheState();
  bool leaked = false;
  const auto leak = [&](const std::string &kind, const std::ostringstream &text) {
    ++kinds_[kind];
    examples_.emplace(kind, text.str());
    leaked = true;
  };
  for (const auto &[key, was] : cache_) {
    const auto now = cache.find(key);
    std::ostringstream text;
    if (now == cache.end()) {
      text << "gone " << key << " (" << was << ')';
      leak(kindOf(key).append(".gone"), text);
    } else if (now->second != was) {
      text << "changed " << key << " (" << was << " -> " << now->second << ')';
      leak(kindOf(key).append(changedFields({was, now->second})), text);
    }
  }
  for (const auto &[key, now] : cache) {
    if (!cache_.contains(key)) {
      std::ostringstream text;
      text << "new " << key << " (" << now << ')';
      leak(kindOf(key).append(".new"), text);
    }
  }
  for (const Region &region : memory_) {
    const std::span<const uint8_t> now =
        emulator_.physical(region.address, static_cast<uint32_t>(region.bytes.size()));
    for (uint32_t offset = 0; offset < region.bytes.size(); offset += kPage) {
      if (std::memcmp(region.bytes.data() + offset, now.data() + offset, kPage) != 0) {
        std::ostringstream text;
        text << "memory " << std::hex << region.address + offset;
        leak("memory", text);
      }
    }
  }
  ++checked_;
  if (!leaked) {
    return;
  }
  ++leaking_;
}

std::string ReplayLeakCheck::takeReport() {
  std::ostringstream report;
  report << "leak_checked=" << checked_ << " leaking=" << leaking_ << " leaked=";
  for (const auto &[kind, count] : kinds_) {
    report << kind << ':' << count << ',';
  }
  for (const auto &[kind, text] : examples_) {
    report << " [" << text << ']';
  }
  checked_ = 0;
  leaking_ = 0;
  kinds_.clear();
  examples_.clear();
  return report.str();
}

} // namespace z3d::app
