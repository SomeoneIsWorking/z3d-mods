// The input tests' gamepad: an SDL virtual Xbox 360 pad that only this process sees, made and
// driven over a loopback HTTP channel (docs/app.md, "Input tests"). PadInput opens it like any pad.
#pragma once

#include <lucent/http.h>

#include <mutex>

struct SDL_Joystick;

namespace z3d::app {

class TestPad {
public:
  // Listens on a free loopback port and logs it; SDL's gamepads are initialised (GameWindow).
  TestPad();
  ~TestPad();
  TestPad(const TestPad &) = delete;
  TestPad &operator=(const TestPad &) = delete;
  TestPad(TestPad &&) = delete;
  TestPad &operator=(TestPad &&) = delete;

private:
  // POST /attach, /button?name=<SDL button>&down=0|1, /axis?name=<SDL axis>&value=<int16>.
  lucent::http::Response serve(const lucent::http::Request &request);
  lucent::http::Response attach();

  std::mutex mutex_;
  SDL_Joystick *joystick_ = nullptr;
  // Last, so its threads stop before the pad goes.
  lucent::http::Server server_;
};

} // namespace z3d::app
