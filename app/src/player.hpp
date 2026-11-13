// The interactive loop: the core runs frames at the 3DS's rate until the window is closed or the
// player asks for the title picker.
#pragma once

namespace z3d::app {

class Emulator;
class GameWindow;

class Player {
public:
  Player(Emulator &emulator, GameWindow &window) : emulator_(emulator), window_(window) {
  }

  void run();

private:
  Emulator &emulator_;
  GameWindow &window_;
};

} // namespace z3d::app
