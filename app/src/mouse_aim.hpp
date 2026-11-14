// Mouse aim: while the game's look follows the gyroscope (first person, an aimed item), the mouse
// turns the 3DS for it, so a mouse count turns the view by kDegreesPerCount. The game adds the
// SDK gyroscope reader's direction to the look (decomp docs/motion.md); here the reader is set to
// integrate our samples as they are, and each sample is one turn around the 3DS's x (pitch) or
// around world up (yaw).
#pragma once

#include "pad_input.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace z3d::app {

class Emulator;
class GameWindow;

class MouseAim final : public GyroSource {
public:
  // View degrees per mouse count, both axes.
  static constexpr double kDegreesPerCount = 0.05;

  // `manifest` is the game's z3d-options.json; without its "aim" the mouse stays the stylus.
  MouseAim(GameWindow &window, Emulator &emulator, PadInput &pad, const std::filesystem::path &manifest);
  ~MouseAim() override;
  MouseAim(const MouseAim &) = delete;
  MouseAim &operator=(const MouseAim &) = delete;
  MouseAim(MouseAim &&) = delete;
  MouseAim &operator=(MouseAim &&) = delete;

  // The mouse moved by this many counts, x right and y down; dropped unless the look is active.
  void move(double dx, double dy);
  // The game's look follows the gyroscope as of the last frame.
  [[nodiscard]] bool active() const {
    return active_;
  }

  std::array<float, 3> takeSample() override;

private:
  // Guest addresses from the decomp (tools/z3dmods/aim.py).
  struct Layout {
    uint32_t reader = 0;
    // MotionState.pitch, then yaw.
    uint32_t angles = 0;
    uint32_t valid = 0;
    uint32_t yawScale = 0;
    uint32_t pitchScale = 0;
    uint32_t motionOff = 0;
    uint32_t play = 0;
    uint32_t player = 0;
    uint32_t lookFlags = 0;
    // The bits of the look's flags that are all set while the gyroscope sets a first-person view.
    uint32_t lookActive = 0;
    uint32_t lookBase = 0;
    uint32_t focusRot = 0;
    // Offsets in the reader.
    uint32_t direction = 0;
    uint32_t filters = 0;
    uint32_t driftRadius = 0;
    uint32_t driftBias = 0;
    uint32_t scale = 0;
  };

  // Once a frame: the reader's settings, whether the look is active, the window's mouse.
  void update();
  // The reader's address once it integrates samples as they are.
  [[nodiscard]] std::optional<uint32_t> exactReader();
  // The player actor while its look follows the gyroscope.
  [[nodiscard]] std::optional<uint32_t> lookingPlayer() const;
  // How far the 3DS has turned past each of the look's limits, in its own degrees: pitch, yaw.
  [[nodiscard]] std::array<double, 2> pastLimits(uint32_t player) const;
  template <typename T> [[nodiscard]] std::optional<T> read(uint32_t address) const;

  GameWindow &window_;
  Emulator &emulator_;
  PadInput &pad_;
  std::optional<Layout> layout_;
  bool active_ = false;
  // View degrees per degree of the 3DS, the game's own.
  double yawScale_ = 1.0;
  double pitchScale_ = 1.0;
  // Degrees the 3DS has yet to turn: around its x, and around world up.
  double pitch_ = 0.0;
  double yaw_ = 0.0;
  // pastLimits() as of the last frame, and the samples it has read the same for: a motion reset
  // leaves the game's angles a frame behind the look's base.
  std::array<double, 2> past_{};
  uint32_t pastFor_ = 0;
  // World up on the 3DS's axes.
  std::array<double, 3> up_{0.0, 1.0, 0.0};
  // What rounding to the gyroscope's unit left over, per axis, in that unit.
  std::array<double, 3> carry_{};
  bool yawNext_ = false;
  // Samples since the last one that turned the 3DS.
  uint32_t still_ = 0;
};

} // namespace z3d::app
