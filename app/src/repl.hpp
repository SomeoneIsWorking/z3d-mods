#pragma once

#include "libretro_core.hpp"

#include <istream>
#include <ostream>
#include <string>

namespace z3d::app {

// Line commands on stdin, one reply line each: "ok [value]" or "err <reason>".
//   run <frames> | input <hex joypad mask> | stick <x> <y> | r <va> <size> | w <va> <hex>
//   savestate <file> | loadstate <file> | shot <file> | frames | quit
//   watch <va> <size> | unwatch | hits (address:size:value:pc:lr per write, hex)
class Repl {
public:
  explicit Repl(LibretroCore &core) : core_(core) {
  }

  void serve(std::istream &in, std::ostream &out);

private:
  // Returns the reply; sets done_ on quit.
  std::string execute(const std::string &line);

  LibretroCore &core_;
  bool done_ = false;
};

} // namespace z3d::app
