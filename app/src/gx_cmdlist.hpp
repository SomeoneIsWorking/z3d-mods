// A draw section's output in nn::gx titles: the requests of the bound command list, which the
// frame end hands the GSP in order (decomp docs/frame.md), as GSP GX commands.
#pragma once

#include "core/hle/service/gsp/gsp_command.h"

#include <cstdint>
#include <string>
#include <vector>

namespace z3d::app {

class Emulator;

struct GxOutput {
  std::vector<Service::GSP::Command> commands;
  // Each SubmitCmdList's PICA commands, in command order.
  std::vector<std::vector<uint8_t>> lists;
};

// Where the first of two outputs differs, or empty when they are the same byte for byte.
[[nodiscard]] std::string compareGx(const GxOutput &real, const GxOutput &replayed);

class GxCmdlist {
public:
  // Guest addresses and offsets from the manifest's "interpolation" table.
  struct Addresses {
    uint32_t boundCmdlist; // the global holding the bound list's pointer
    uint32_t requests;     // offsets in the list
    uint32_t requestCount;
    uint32_t requestSize;
  };

  GxCmdlist(Emulator &emulator, const Addresses &addresses)
      : emulator_(emulator), addresses_(addresses) {
  }

  // The bound list's requests as memory holds them now.
  [[nodiscard]] GxOutput read() const;

private:
  [[nodiscard]] uint32_t u32(uint32_t address) const;

  Emulator &emulator_;
  Addresses addresses_;
};

} // namespace z3d::app
