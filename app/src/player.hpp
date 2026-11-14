// The interactive loop: the core runs frames at the 3DS's rate until the window is closed or the
// player asks for the title picker; the mod keys swap the running game's mods (mod_swap.hpp).
#pragma once

namespace z3d::app {

class Emulator;
class GameOptions;
class GameWindow;
class ModSwap;
class PadInput;

class Player {
public:
  Player(Emulator &emulator, GameWindow &window, ModSwap &mods, const GameOptions &options,
         PadInput &pad)
      : emulator_(emulator), window_(window), mods_(mods), options_(options), pad_(pad) {
  }

  void run();

private:
  void swapMods();

  Emulator &emulator_;
  GameWindow &window_;
  ModSwap &mods_;
  const GameOptions &options_;
  PadInput &pad_;
};

} // namespace z3d::app
