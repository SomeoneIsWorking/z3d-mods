#pragma once

#include <istream>
#include <ostream>
#include <string>

namespace z3d::app {

class Emulator;
class GameWindow;
class GameOptions;
class ModSwap;
class PadInput;

// Line commands on stdin, one reply line each: "ok [value]" or "err <reason>".
//   run <frames> | input <hex NativeButton mask> | stick <x> <y> | r <va> <size> | w <va> <hex>
//   savestate <file> | loadstate <file> | shot <file> | frames | quit
//   watch <va> <size> | unwatch | hits (address:size:value:pc:lr per write, hex)
//   option <id> <value> (a game option, game_options.hpp)
//   touch <x> <y> | touch off (the stylus on the 320x240 touch screen)
//   patch on|off | textures on|off (mod_swap.hpp)
class Repl {
public:
  // `stick` values are signed 16-bit, y growing downward.
  Repl(Emulator &emulator, GameWindow &window, PadInput &pad, GameOptions &options, ModSwap &mods)
      : emulator_(emulator), window_(window), pad_(pad), options_(options), mods_(mods) {
  }

  void serve(std::istream &in, std::ostream &out);

private:
  // Returns the reply; sets done_ on quit.
  std::string execute(const std::string &line);

  Emulator &emulator_;
  GameWindow &window_;
  PadInput &pad_;
  GameOptions &options_;
  ModSwap &mods_;
  bool done_ = false;
};

} // namespace z3d::app
