#include "repl.hpp"

#include "emulator.hpp"
#include "pad_input.hpp"

#include <lucent/log.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

namespace z3d::app {
namespace {

constexpr uint32_t kMaxRead = 1U << 24;
constexpr float kStickFull = 32767.0F;

std::string toHex(const std::vector<uint8_t> &bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  text.reserve(bytes.size() * 2);
  for (const uint8_t byte : bytes) {
    text.push_back(kDigits[byte >> 4]);
    text.push_back(kDigits[byte & 0xF]);
  }
  return text;
}

bool fromHex(const std::string &text, std::vector<uint8_t> &out) {
  if (text.size() % 2 != 0) {
    return false;
  }
  out.clear();
  for (size_t i = 0; i < text.size(); i += 2) {
    unsigned value = 0;
    std::istringstream(text.substr(i, 2)) >> std::hex >> value;
    out.push_back(static_cast<uint8_t>(value));
  }
  return true;
}

} // namespace

void Repl::serve(std::istream &in, std::ostream &out) {
  std::string line;
  while (!done_ && std::getline(in, line)) {
    out << execute(line) << '\n' << std::flush;
  }
}

std::string Repl::execute(const std::string &line) {
  std::istringstream args(line);
  std::string command;
  args >> command;
  if (command == "run") {
    unsigned frames = 0;
    if (!(args >> frames)) {
      return "err run <frames>";
    }
    emulator_.runFrames(frames, true);
    return "ok " + std::to_string(emulator_.framesRun());
  }
  if (command == "input") {
    unsigned mask = 0;
    if (!(args >> std::hex >> mask)) {
      return "err input <hex mask>";
    }
    pad_.setButtons(mask);
    return "ok";
  }
  if (command == "stick" || command == "cstick") {
    int x = 0;
    int y = 0;
    if (!(args >> x >> y) || x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) {
      return "err " + command + " <x> <y>";
    }
    const auto analog = command == "stick" ? Settings::NativeAnalog::CirclePad : Settings::NativeAnalog::CStick;
    pad_.setStick(analog, static_cast<float>(x) / kStickFull, -static_cast<float>(y) / kStickFull);
    return "ok";
  }
  if (command == "r") {
    uint32_t address = 0;
    uint32_t size = 0;
    if (!(args >> std::hex >> address >> size) || size > kMaxRead) {
      return "err r <hex va> <hex size>";
    }
    std::vector<uint8_t> bytes(size);
    if (!emulator_.read(address, bytes)) {
      return "err unmapped";
    }
    return "ok " + toHex(bytes);
  }
  if (command == "w") {
    uint32_t address = 0;
    std::string hex;
    std::vector<uint8_t> bytes;
    if (!(args >> std::hex >> address >> hex) || !fromHex(hex, bytes)) {
      return "err w <hex va> <hex bytes>";
    }
    return emulator_.write(address, bytes) ? "ok" : "err unmapped";
  }
  if (command == "savestate") {
    std::string path;
    args >> path;
    const auto state = emulator_.saveState();
    if (!state) {
      return "err core did not serialize";
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(state->data()),
               static_cast<std::streamsize>(state->size()));
    return file ? "ok " + std::to_string(state->size()) : "err cannot write " + path;
  }
  if (command == "loadstate") {
    std::string path;
    args >> path;
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> state((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (state.empty()) {
      return "err cannot read " + path;
    }
    return emulator_.loadState(std::move(state)) ? "ok" : "err core refused the state";
  }
  if (command == "shot") {
    std::string path;
    args >> path;
    const Frame &frame = emulator_.frame();
    if (!frame.writePng(path)) {
      return "err no frame or cannot write " + path;
    }
    return "ok " + std::to_string(frame.width()) + "x" + std::to_string(frame.height());
  }
  if (command == "watch") {
    uint32_t address = 0;
    uint32_t size = 0;
    if (!(args >> std::hex >> address >> size)) {
      return "err watch <hex va> <hex size>";
    }
    return emulator_.watchWrites(address, size) ? "ok" : "err no running process";
  }
  if (command == "unwatch") {
    emulator_.clearWatches();
    return "ok";
  }
  if (command == "hits") {
    std::ostringstream reply;
    reply << "ok" << std::hex;
    for (const WriteHit &hit : emulator_.takeHits()) {
      reply << ' ' << hit.address << ':' << hit.size << ':' << hit.value << ':' << hit.pc << ':'
            << hit.lr;
    }
    return reply.str();
  }
  if (command == "frames") {
    return "ok " + std::to_string(emulator_.framesRun());
  }
  if (command == "quit") {
    done_ = true;
    return "ok";
  }
  return "err unknown command " + command;
}

} // namespace z3d::app
