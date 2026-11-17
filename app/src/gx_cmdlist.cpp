#include "gx_cmdlist.hpp"

#include "emulator.hpp"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace z3d::app {
namespace {

// A request is its command id and the GSP command's first six parameter words.
constexpr uint32_t kRequestArgs = 24;
// nngx keeps at most this many requests in a list.
constexpr uint32_t kMaxRequests = 0x200;

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
    const uint32_t request = requests + i * addresses_.requestSize;
    Service::GSP::Command command{};
    command.id.Assign(static_cast<Service::GSP::CommandId>(u32(request)));
    if (!emulator_.read(request + 4, std::span<uint8_t>(command.raw_data.data(), kRequestArgs))) {
      throw std::runtime_error("cannot read a GX request");
    }
    if (command.id == Service::GSP::CommandId::SubmitCmdList) {
      std::vector<uint8_t> bytes(command.submit_gpu_cmdlist.size);
      if (!emulator_.read(command.submit_gpu_cmdlist.address, bytes)) {
        throw std::runtime_error("cannot read a PICA command list");
      }
      output.lists.push_back(std::move(bytes));
    }
    output.commands.push_back(command);
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
