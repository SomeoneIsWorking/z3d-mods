// The interactive frame loop: events, the player's pad, one core frame, present, and pacing.
#pragma once

#include "audio_output.hpp"
#include "input.hpp"

#include <chrono>
#include <memory>

namespace z3d::app {

class LibretroCore;
class Window;

class Player {
public:
  Player(LibretroCore &core, Window &window);

  // Until the window is closed.
  void run();

private:
  // Sleeps until the next frame is due: by the sound queue, or by the clock without a device.
  void pace();

  LibretroCore &core_;
  Window &window_;
  Input input_;
  std::unique_ptr<AudioOutput> audio_;
  std::chrono::steady_clock::duration frameTime_;
  std::chrono::steady_clock::time_point due_;
};

} // namespace z3d::app
