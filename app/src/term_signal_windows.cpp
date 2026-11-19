#include "term_signal.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <lucent/log.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

#include <windows.h>

namespace z3d::app {
namespace {

// How long the loop has to end by itself after the event.
constexpr std::chrono::seconds kGrace{3};
// The exit status of a process the console ended.
constexpr int kEndedStatus = 130;

} // namespace

TermSignal::TermSignal() {
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  if (SetConsoleCtrlHandler(caught, TRUE) == 0) {
    throw std::runtime_error("no console handler for the app's end");
  }
}

TermSignal::~TermSignal() {
  SetConsoleCtrlHandler(caught, FALSE);
}

// Runs on a thread the console starts; returning ends the process, so it waits for the loop.
int __stdcall TermSignal::caught(unsigned long event) {
  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
  std::this_thread::sleep_for(kGrace);
  lucent::warn("z3d", "console event {}: the window's loop did not end, ending the process", event);
  std::_Exit(kEndedStatus);
}

} // namespace z3d::app
