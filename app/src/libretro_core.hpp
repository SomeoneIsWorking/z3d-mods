#pragma once

#include "frame.hpp"
#include "guest_memory.hpp"
#include "input.hpp"
#include "vulkan/core_video.hpp"

#include <azahar_debug.h>
#include <libretro.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace z3d::app {

class AudioOutput;
class Window;

struct CoreConfig {
  std::filesystem::path core;
  std::filesystem::path rom;
  // The frontend save and system directory; the Azahar core keeps its data in <dir>/Azahar/.
  std::filesystem::path userDir;
  std::map<std::string, std::string> options;
};

// One libretro core loaded with dlopen and driven as its only frontend.
class LibretroCore {
public:
  // The core draws with Vulkan on `window`'s surface.
  LibretroCore(CoreConfig config, const Window &window);
  ~LibretroCore();
  LibretroCore(const LibretroCore &) = delete;
  LibretroCore &operator=(const LibretroCore &) = delete;

  void runFrames(unsigned count);
  // Held joypad buttons as a RETRO_DEVICE_ID_JOYPAD bitmask.
  void setButtons(uint16_t mask) {
    pad_.buttons = mask;
  }
  // Left analog stick, libretro's signed 16-bit range.
  void setStick(int16_t x, int16_t y) {
    pad_.left = {x, y};
  }
  void setPad(const PadState &pad) {
    pad_ = pad;
  }
  // Where the core's sound goes; none (the default) drops it.
  void setAudio(AudioOutput *audio) {
    audio_ = audio;
  }

  [[nodiscard]] std::optional<std::vector<uint8_t>> saveState();
  [[nodiscard]] bool loadState(const std::vector<uint8_t> &state);

  [[nodiscard]] const GuestMemory &memory() const {
    return memory_;
  }
  // The last presented frame, read back from the GPU for a hardware-rendered core.
  [[nodiscard]] const Frame &capture();
  [[nodiscard]] CoreVideo &video() {
    return video_;
  }
  [[nodiscard]] const retro_system_av_info &avInfo() const {
    return avInfo_;
  }
  [[nodiscard]] uint64_t framesRun() const {
    return framesRun_;
  }

  // Write watchpoints; false when the core has no debug interface (a stock core).
  [[nodiscard]] bool watchWrites(uint32_t address, uint32_t size);
  void clearWatches();
  // Hits since the last call, oldest first.
  [[nodiscard]] std::vector<azahar_write_hit> takeHits();

private:
  struct Api {
    void (*init)();
    void (*deinit)();
    void (*setEnvironment)(retro_environment_t);
    void (*setVideoRefresh)(retro_video_refresh_t);
    void (*setAudioSample)(retro_audio_sample_t);
    void (*setAudioSampleBatch)(retro_audio_sample_batch_t);
    void (*setInputPoll)(retro_input_poll_t);
    void (*setInputState)(retro_input_state_t);
    bool (*loadGame)(const retro_game_info *);
    void (*unloadGame)();
    void (*run)();
    size_t (*serializeSize)();
    bool (*serialize)(void *, size_t);
    bool (*unserialize)(const void *, size_t);
    void (*getAvInfo)(retro_system_av_info *);
  };

  static bool onEnvironment(unsigned command, void *data);
  static void onVideo(const void *data, unsigned width, unsigned height, size_t pitch);
  static void onAudioSample(int16_t left, int16_t right);
  static size_t onAudioBatch(const int16_t *data, size_t frames);
  static void onInputPoll();
  static int16_t onInputState(unsigned port, unsigned device, unsigned index, unsigned id);
  static void onLog(retro_log_level level, const char *format, ...);
  static void onWriteHit(const azahar_write_hit *hit);

  bool environment(unsigned command, void *data);
  struct InputQuery {
    unsigned port;
    unsigned device;
    unsigned index;
    unsigned id;
  };
  [[nodiscard]] int16_t inputState(const InputQuery &query) const;
  void resolve();
  // Options the caller did not set take the core's own default.
  void recordDefaults(const retro_core_option_v2_definition *definitions);

  static LibretroCore *active_;

  CoreConfig config_;
  std::string userDirText_;
  void *library_ = nullptr;
  Api api_{};
  bool loaded_ = false;
  bool hardwareFrame_ = false;
  PadState pad_;
  AudioOutput *audio_ = nullptr;
  uint64_t framesRun_ = 0;
  GuestMemory memory_;
  Frame frame_;
  retro_system_av_info avInfo_{};
  CoreVideo video_;
  const azahar_debug_interface *debug_ = nullptr;
  std::vector<azahar_write_hit> hits_;
};

} // namespace z3d::app
