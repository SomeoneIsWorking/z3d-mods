// The core's log backend (z3d_core.log in the user dir) for the app's lifetime, across games.
#pragma once

namespace z3d::app {

class CoreLog {
public:
  CoreLog();
  ~CoreLog();
  CoreLog(const CoreLog &) = delete;
  CoreLog &operator=(const CoreLog &) = delete;
  CoreLog(CoreLog &&) = delete;
  CoreLog &operator=(CoreLog &&) = delete;
};

} // namespace z3d::app
