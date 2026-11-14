#include "mod_swap.hpp"

#include "emulator.hpp"

#include "common/settings.h"
#include "core/file_sys/patch.h"
#include "core/loader/loader.h"

#include <lucent/log.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>
#include <utility>

namespace z3d::app {
namespace {

constexpr const char *kPatchFile = "code.ips";

std::vector<uint8_t> readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

ModSwap::ModSwap(Emulator &emulator, std::filesystem::path folder, uint32_t payload)
    : emulator_(emulator), patch_(std::move(folder) / kPatchFile), payload_(payload),
      base_(emulator.unpatchedImage()), applied_(readFile(patch_)) {
}

std::vector<uint8_t> ModSwap::patched(const std::vector<uint8_t> &patch) const {
  std::vector<uint8_t> image = base_;
  if (!patch.empty() &&
      FileSys::Patch::ApplyIpsPatch(patch, image) != Loader::ResultStatus::Success) {
    return {};
  }
  return image;
}

bool ModSwap::setPatch(bool on) {
  const std::vector<uint8_t> wanted = on ? readFile(patch_) : std::vector<uint8_t>();
  if (on && wanted.empty()) {
    lucent::warn("mods", "no patch at {}", patch_.string());
    return false;
  }
  const std::vector<uint8_t> before = patched(applied_);
  const std::vector<uint8_t> after = patched(wanted);
  if (before.empty() || after.empty()) {
    lucent::warn("mods", "{} does not apply to this game's code", patch_.string());
    return false;
  }
  // The patch's own pages start over whole; below them, only bytes either patch changed are
  // written, so the game's own data stays as it is.
  const size_t own = payload_ >= Emulator::kImageBase
                         ? std::min<size_t>(payload_ - Emulator::kImageBase, after.size())
                         : after.size();
  size_t at = 0;
  size_t written = 0;
  while (at < after.size()) {
    const auto changed = [&](size_t i) {
      return i >= own || before[i] != base_[i] || after[i] != base_[i];
    };
    if (!changed(at)) {
      ++at;
      continue;
    }
    size_t end = at;
    while (end < after.size() && changed(end)) {
      ++end;
    }
    if (!emulator_.write(Emulator::kImageBase + static_cast<uint32_t>(at),
                         std::span<const uint8_t>(after).subspan(at, end - at))) {
      lucent::error("mods", "the code image is not mapped at {:#x}", Emulator::kImageBase + at);
      return false;
    }
    written += end - at;
    at = end;
  }
  emulator_.codeChanged();
  applied_ = wanted;
  lucent::info("mods", "patch {}: {} bytes written", on ? "in" : "out", written);
  return true;
}

bool ModSwap::texturesOn() const {
  return Settings::values.custom_textures.GetValue();
}

void ModSwap::setTextures(bool on) {
  Settings::values.custom_textures = on;
}

} // namespace z3d::app
