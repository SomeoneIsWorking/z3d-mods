#include "term_signal.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <lucent/log.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

namespace z3d::app {
namespace {

// How long the loop has to end by itself after the signal.
constexpr std::chrono::seconds kGrace{3};
// How often the waiting thread looks whether the app is ending anyway, in milliseconds.
constexpr int kPoll = 200;
// The exit status of a process a signal ended.
constexpr int kSignalStatus = 128;
constexpr std::array kSignals{SIGTERM, SIGINT};

} // namespace

TermSignal::TermSignal() {
  if (pipe2(pipe_.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
    throw std::runtime_error("no pipe for the signals");
  }
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  for (const int signal : kSignals) {
    std::signal(signal, caught);
  }
  waiter_ = std::jthread([this](const std::stop_token &stop) {
    wait(stop);
  });
}

TermSignal::~TermSignal() {
  for (const int signal : kSignals) {
    std::signal(signal, SIG_DFL);
  }
  waiter_.request_stop();
  waiter_.join();
  close(pipe_[0]);
  close(pipe_[1]);
  pipe_ = {-1, -1};
}

void TermSignal::caught(int signal) {
  const char number = static_cast<char>(signal);
  // A full pipe already holds a signal.
  static_cast<void>(write(pipe_[1], &number, 1));
}

void TermSignal::wait(const std::stop_token &stop) const {
  pollfd ready{pipe_[0], POLLIN, 0};
  char number = 0;
  while (read(pipe_[0], &number, 1) != 1) {
    if (stop.stop_requested()) {
      return;
    }
    poll(&ready, 1, kPoll);
  }
  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
  const auto deadline = std::chrono::steady_clock::now() + kGrace;
  while (!stop.stop_requested()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      lucent::warn("z3d", "signal {}: the window's loop did not end, ending the process",
                   static_cast<int>(number));
      std::_Exit(kSignalStatus + number);
    }
    poll(&ready, 1, kPoll);
  }
}

} // namespace z3d::app
