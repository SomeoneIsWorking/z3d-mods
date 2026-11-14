// SIGTERM and SIGINT end the app: the window's loop gets SDL's quit event, and a loop that
// cannot take it (a present waiting in the driver for a compositor that is gone has no way
// out) is ended with the process.
#pragma once

#include <array>
#include <stop_token>
#include <thread>

namespace z3d::app {

class TermSignal {
public:
  // One at a time: the handlers are the process's.
  TermSignal();
  ~TermSignal();
  TermSignal(const TermSignal &) = delete;
  TermSignal &operator=(const TermSignal &) = delete;
  TermSignal(TermSignal &&) = delete;
  TermSignal &operator=(TermSignal &&) = delete;

private:
  static void caught(int signal);
  void wait(const std::stop_token &stop) const;

  // A pipe the handler writes the signal's number into: its read and write ends.
  static inline std::array<int, 2> pipe_{-1, -1};
  std::jthread waiter_;
};

} // namespace z3d::app
