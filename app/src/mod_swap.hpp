// A running game's mods changed without a reboot: our code patch (the set's code.ips) put in,
// taken out or read again from disk, and the texture pack turned on and off.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace z3d::app {

class Emulator;

class ModSwap {
public:
  // `folder` is the game's installed set (the options manifest's folder) and `payload` the guest
  // address where the patch's own pages start (the options block; 0 without a manifest).
  ModSwap(Emulator &emulator, std::filesystem::path folder, uint32_t payload);

  [[nodiscard]] bool patchOn() const {
    return !applied_.empty();
  }
  // On writes code.ips as it is on disk now, so a rebuilt patch replaces the running one; off
  // puts the game's own code back. The patch's memory starts over, options included: the caller
  // applies them again. False, with the game as it was, when the patch cannot be read or applied.
  [[nodiscard]] bool setPatch(bool on);

  [[nodiscard]] bool texturesOn() const;
  // The core drops its texture cache and loads the pack's textures, or the game's own, from the
  // next frame. A pack changed on disk needs a reboot: the core lists its files once.
  void setTextures(bool on);

private:
  // The code image with `patch` applied, as the loader builds it; empty when it does not apply.
  [[nodiscard]] std::vector<uint8_t> patched(const std::vector<uint8_t> &patch) const;

  Emulator &emulator_;
  std::filesystem::path patch_;
  uint32_t payload_;
  // The game's code without a patch, as long as the process image; and the patch now in it.
  std::vector<uint8_t> base_;
  std::vector<uint8_t> applied_;
};

} // namespace z3d::app
