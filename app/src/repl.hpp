#pragma once

#include <istream>
#include <ostream>
#include <string>

namespace z3d::app {

class Emulator;
class GameOptions;
class PadInput;

// Line commands on stdin, one reply line each: "ok [value]" or "err <reason>".
//   run <frames> | input <hex NativeButton mask> | stick <x> <y> | r <va> <size> | w <va> <hex>
//   savestate <file> | loadstate <file> | shot <file> | frames | quit
//   watch <va> <size> | unwatch | hits (address:size:value:pc:lr per write, hex)
//   option <id> <value> (a game option, game_options.hpp)
class Repl {
public:
  // `stick` values are signed 16-bit, y growing downward.
  Repl(Emulator &emulator, PadInput &pad, GameOptions &options)
      : emulator_(emulator), pad_(pad), options_(options) {
  }

  void serve(std::istream &in, std::ostream &out);

private:
  // Returns the reply; sets done_ on quit.
  std::string execute(const std::string &line);

  Emulator &emulator_;
  PadInput &pad_;
  GameOptions &options_;
  bool done_ = false;
};

} // namespace z3d::app
