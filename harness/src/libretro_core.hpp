#pragma once

#include "frame.hpp"
#include "gl_context.hpp"
#include "guest_memory.hpp"

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

namespace z3d::harness {

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
  explicit LibretroCore(CoreConfig config);
  ~LibretroCore();
  LibretroCore(const LibretroCore &) = delete;
  LibretroCore &operator=(const LibretroCore &) = delete;

  void runFrames(unsigned count);
  // Held joypad buttons as a RETRO_DEVICE_ID_JOYPAD bitmask.
  void setButtons(uint16_t mask) {
    buttons_ = mask;
  }
  // Left analog stick, libretro's signed 16-bit range.
  void setStick(int16_t x, int16_t y) {
    stick_ = {x, y};
  }

  [[nodiscard]] std::optional<std::vector<uint8_t>> saveState();
  [[nodiscard]] bool loadState(const std::vector<uint8_t> &state);

  [[nodiscard]] const GuestMemory &memory() const {
    return memory_;
  }
  [[nodiscard]] const Frame &frame() const {
    return frame_;
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
  static uintptr_t onCurrentFramebuffer();
  static void onWriteHit(const azahar_write_hit *hit);

  bool environment(unsigned command, void *data);
  bool setHwRender(retro_hw_render_callback &render);
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
  uint16_t buttons_ = 0;
  std::array<int16_t, 2> stick_{};
  uint64_t framesRun_ = 0;
  GuestMemory memory_;
  Frame frame_;
  std::vector<uint32_t> readback_;
  retro_system_av_info avInfo_{};
  retro_hw_render_callback hwRender_{};
  std::unique_ptr<GlContext> gl_;
  const azahar_debug_interface *debug_ = nullptr;
  std::vector<azahar_write_hit> hits_;
};

} // namespace z3d::harness
