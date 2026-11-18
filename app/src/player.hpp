// The interactive loop: the core runs frames at the 3DS's rate until the window is closed or the
// player asks for the title picker; the mod keys swap the running game's mods (mod_swap.hpp).
// Frame interpolation is on while it plays when the game's option asks for it, and the pacing
// (ticks, vblanks and the window's flips) is logged every kPacingReport.
#pragma once

#include <chrono>

namespace z3d::app {

class Emulator;
class FrameInterpolation;
class GameOptions;
class GameWindow;
class ModSwap;
class PadInput;

inline constexpr std::chrono::seconds kPacingReport{10};

class Player {
public:
  Player(Emulator &emulator, GameWindow &window, ModSwap &mods, const GameOptions &options,
         PadInput &pad, FrameInterpolation &interpolation)
      : emulator_(emulator), window_(window), mods_(mods), options_(options), pad_(pad),
        interpolation_(interpolation) {
  }

  void run();

private:
  void swapMods();

  Emulator &emulator_;
  GameWindow &window_;
  ModSwap &mods_;
  const GameOptions &options_;
  PadInput &pad_;
  FrameInterpolation &interpolation_;
};

} // namespace z3d::app
