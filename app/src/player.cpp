#include "player.hpp"

#include "libretro_core.hpp"
#include "window.hpp"

#include <SDL3/SDL.h>

#include <lucent/log.h>

#include <exception>
#include <thread>

namespace z3d::app {
namespace {

// Sound kept queued ahead of the device: enough to ride out a slow frame, little enough to feel.
constexpr double kQueuedSeconds = 0.05;

} // namespace

Player::Player(LibretroCore &core, Window &window)
    : core_(core), window_(window),
      frameTime_(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(1.0 / core.avInfo().timing.fps))),
      due_(std::chrono::steady_clock::now()) {
  try {
    audio_ = std::make_unique<AudioOutput>(core.avInfo().timing.sample_rate);
    core_.setAudio(audio_.get());
  } catch (const std::exception &error) {
    lucent::warn("player", "no sound ({}); pacing by the clock", error.what());
  }
  const retro_game_geometry &geometry = core.avInfo().geometry;
  window_.fit({geometry.base_width, geometry.base_height});
}

void Player::run() {
  for (;;) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return;
      }
      input_.handle(event);
    }
    core_.setPad(input_.read(core_.video().placement(), window_.pixelDensity()));
    core_.runFrames(1);
    core_.video().show(window_.pixels());
    pace();
  }
}

void Player::pace() {
  if (audio_) {
    while (audio_->queued() > kQueuedSeconds) {
      SDL_Delay(1);
    }
    return;
  }
  due_ += frameTime_;
  const auto now = std::chrono::steady_clock::now();
  if (due_ < now) {
    due_ = now;
  }
  std::this_thread::sleep_until(due_);
}

} // namespace z3d::app
