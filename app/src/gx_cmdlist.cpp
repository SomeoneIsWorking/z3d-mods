#include "gx_cmdlist.hpp"

#include "emulator.hpp"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace z3d::app {
namespace {

// A request is its kind and six words in nngx's own encoding (decomp docs/frame.md "Requests").
constexpr uint32_t kRequestWords = 7;
// nngx keeps at most this many requests in a list.
constexpr uint32_t kMaxRequests = 0x200;

using Request = std::array<uint32_t, kRequestWords>;
using Service::GSP::Command;
using Service::GSP::CommandId;

Command commandOf(CommandId id) {
  Command command{};
  command.id.Assign(id);
  return command;
}

uint32_t low(uint32_t word) {
  return word & 0xFFFF;
}

uint32_t high(uint32_t word) {
  return word >> 16;
}

// A fill's size word: the byte count in its low 23 bits, the fill width in bits 23 and 24.
uint32_t fillEnd(uint32_t start, uint32_t size) {
  return start + (size & 0x7FFFFF);
}

uint16_t fillControl(uint32_t size) {
  return static_cast<uint16_t>(((size >> 15) & 0x300) | 1);
}

// nngx's display transfer flags as the GSP's (Pica::DisplayTransferConfig).
uint32_t transferFlags(uint32_t flags, uint32_t inSize, uint32_t outSize) {
  const bool crop = low(outSize) < low(inSize) || high(outSize) < high(inSize);
  return ((flags >> 6) & 1) | ((flags >> 9) & 2) | (crop ? 4U : 0U) | ((flags >> 6) & 0x20) |
         ((flags & 7) << 8) | ((flags & 0x38) << 9) | ((flags & 0x200) << 7) |
         ((flags & 0x180) << 17);
}

// The GSP commands nngx's dispatcher (MM3D 0x1490F4, OoT3D 0x3027DC) sends for one request.
std::vector<Command> commandsOf(const Request &r) {
  switch (static_cast<CommandId>(r[0] & 0xFF)) {
  case CommandId::RequestDma: {
    Command command = commandOf(CommandId::RequestDma);
    command.dma_request = {.source_address = r[1], .dest_address = r[2], .size = r[3]};
    return {command};
  }
  case CommandId::SubmitCmdList: {
    std::vector<Command> commands;
    // Bit 2 of the flags word skips the cache flush before the list.
    if ((r[3] & 4) == 0) {
      Command flush = commandOf(CommandId::CacheFlush);
      flush.cache_flush.regions[0] = {.address = r[1], .size = r[2]};
      commands.push_back(flush);
    }
    Command command = commandOf(CommandId::SubmitCmdList);
    command.submit_gpu_cmdlist.address = r[1];
    command.submit_gpu_cmdlist.size = r[2];
    command.submit_gpu_cmdlist.flags = r[3] & 1;
    commands.push_back(command);
    return commands;
  }
  case CommandId::MemoryFill: {
    Command command = commandOf(CommandId::MemoryFill);
    command.memory_fill = {.start1 = r[1],
                           .value1 = r[2],
                           .end1 = fillEnd(r[1], r[3]),
                           .start2 = r[4],
                           .value2 = r[5],
                           .end2 = fillEnd(r[4], r[6]),
                           .control1 = fillControl(r[3]),
                           .control2 = fillControl(r[6])};
    return {command};
  }
  case CommandId::DisplayTransfer: {
    Command command = commandOf(CommandId::DisplayTransfer);
    command.display_transfer = {.in_buffer_address = r[1],
                                .out_buffer_address = r[2],
                                .in_buffer_size = r[3],
                                .out_buffer_size = r[4],
                                .flags = transferFlags(r[5], r[3], r[4])};
    return {command};
  }
  case CommandId::TextureCopy: {
    Command command = commandOf(CommandId::TextureCopy);
    const bool gaps = low(r[4]) != 0 || low(r[5]) != 0;
    command.texture_copy = {.in_buffer_address = r[1],
                            .out_buffer_address = r[2],
                            .size = r[3],
                            .in_width_gap = high(r[4]) | (low(r[4]) << 16),
                            .out_width_gap = high(r[5]) | (low(r[5]) << 16),
                            .flags = (gaps ? 4U : 0U) | 8U};
    return {command};
  }
  default:
    throw std::runtime_error("the GX command list holds a request nngx does not send");
  }
}

} // namespace

uint32_t GxCmdlist::u32(uint32_t address) const {
  std::array<uint8_t, 4> bytes{};
  if (!emulator_.read(address, bytes)) {
    throw std::runtime_error("cannot read the GX command list");
  }
  uint32_t value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
}

GxOutput GxCmdlist::read() const {
  GxOutput output;
  const uint32_t list = u32(addresses_.boundCmdlist);
  if (list == 0) {
    return output;
  }
  const uint32_t requests = u32(list + addresses_.requests);
  const uint32_t count = u32(list + addresses_.requestCount);
  if (count > kMaxRequests) {
    throw std::runtime_error("the GX command list holds more requests than nngx allows");
  }
  for (uint32_t i = 0; i < count; ++i) {
    std::array<uint8_t, sizeof(Request)> bytes{};
    if (!emulator_.read(requests + i * addresses_.requestSize, bytes)) {
      throw std::runtime_error("cannot read a GX request");
    }
    Request request{};
    std::memcpy(request.data(), bytes.data(), bytes.size());
    for (const Command &command : commandsOf(request)) {
      if (command.id == CommandId::SubmitCmdList) {
        std::vector<uint8_t> bytes(command.submit_gpu_cmdlist.size);
        if (!emulator_.read(command.submit_gpu_cmdlist.address, bytes)) {
          throw std::runtime_error("cannot read a PICA command list");
        }
        output.lists.push_back(std::move(bytes));
      }
      output.commands.push_back(command);
    }
  }
  return output;
}

std::string compareGx(const GxOutput &real, const GxOutput &replayed) {
  std::ostringstream text;
  if (real.commands.size() != replayed.commands.size()) {
    text << "count " << real.commands.size() << '/' << replayed.commands.size();
    return text.str();
  }
  for (std::size_t i = 0; i < real.commands.size(); ++i) {
    const Service::GSP::Command &a = real.commands[i];
    const Service::GSP::Command &b = replayed.commands[i];
    if (a.id.Value() != b.id.Value() || a.raw_data != b.raw_data) {
      text << "command " << i;
      return text.str();
    }
  }
  for (std::size_t i = 0; i < real.lists.size(); ++i) {
    if (real.lists[i] != replayed.lists[i]) {
      std::size_t at = 0;
      while (at < real.lists[i].size() && real.lists[i][at] == replayed.lists[i][at]) {
        ++at;
      }
      text << "list " << i << " at 0x" << std::hex << at;
      return text.str();
    }
  }
  return {};
}

} // namespace z3d::app
