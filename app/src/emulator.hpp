// Azahar's core as the app runs it (settings: core_settings.hpp): the game loaded on the window,
// frames run on this thread, and the tools' reach into guest memory, savestates and frames.
#pragma once

#include "app_config.hpp"
#include "frame.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Core {
class System;
}

namespace z3d::app {

class GameWindow;
class WriteWatches;

// A guest store to a watched range.
struct WriteHit {
  uint32_t address;
  uint32_t size;
  uint64_t value;
  uint32_t pc;
  uint32_t lr;
};

class Emulator {
public:
  // The core's log (core_log.hpp) outlives every Emulator.
  Emulator(const AppConfig &config, const std::filesystem::path &rom, GameWindow &window);
  ~Emulator();
  Emulator(const Emulator &) = delete;
  Emulator &operator=(const Emulator &) = delete;
  Emulator(Emulator &&) = delete;
  Emulator &operator=(Emulator &&) = delete;

  // Runs until the renderer has ended `count` more frames; `capture` keeps the last one in frame().
  void runFrames(uint64_t count, bool capture);
  [[nodiscard]] uint64_t framesRun() const;
  // Each is called as runFrames goes, at least once a frame, in the order they were added.
  void onFrame(std::function<void()> observer) {
    frameObservers_.push_back(std::move(observer));
  }

  // False when any page of the range is unmapped in the running process.
  [[nodiscard]] bool read(uint32_t address, std::span<uint8_t> out) const;
  [[nodiscard]] bool write(uint32_t address, std::span<const uint8_t> data);

  // Where the process image (code, data, .bss) starts.
  static constexpr uint32_t kImageBase = 0x00100000;
  // The game's code as its loader reads it (a set's code.bin included) before any code patch,
  // padded with zeros to the size of the running process image.
  [[nodiscard]] std::vector<uint8_t> unpatchedImage() const;
  // After code was written: the CPU drops what it translated.
  void codeChanged();

  [[nodiscard]] std::optional<std::vector<uint8_t>> saveState();
  [[nodiscard]] bool loadState(std::vector<uint8_t> state);

  [[nodiscard]] const Frame &frame() const {
    return frame_;
  }

  [[nodiscard]] bool watchWrites(uint32_t address, uint32_t size);
  void clearWatches();
  // Hits since the last call, oldest first.
  [[nodiscard]] std::vector<WriteHit> takeHits();

private:
  void step();
  // Savestates are unsafe while kernel async operations (file I/O) are in flight.
  [[nodiscard]] bool drainAsync();

  Core::System &system_;
  GameWindow &window_;
  uint32_t resolution_;
  Frame frame_;
  std::vector<uint32_t> captureRows_;
  std::unique_ptr<WriteWatches> watches_;
  std::vector<std::function<void()>> frameObservers_;
};

} // namespace z3d::app
