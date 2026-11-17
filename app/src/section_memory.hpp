// Reading the state a draw section started from and writing a replay's inputs, as typed values.
#pragma once

#include "core/section_replay.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace z3d::app {

template <class T> [[nodiscard]] T readStart(Core::SectionReplay &replay, uint32_t address) {
  std::array<uint8_t, sizeof(T)> bytes{};
  replay.ReadStart(address, bytes);
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

template <class T>
[[nodiscard]] Core::SectionReplay::Input replayInput(uint32_t address, const T &value) {
  std::vector<uint8_t> bytes(sizeof(T));
  std::memcpy(bytes.data(), &value, sizeof(T));
  return {address, std::move(bytes)};
}

} // namespace z3d::app
