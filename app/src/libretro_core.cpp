#include "libretro_core.hpp"

#include "audio_output.hpp"
#include "window.hpp"

#include <lucent/log.h>

#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace z3d::app {

LibretroCore *LibretroCore::active_ = nullptr;

namespace {

template <typename Fn> void bind(void *library, const char *name, Fn &out) {
  void *symbol = dlsym(library, name);
  if (symbol == nullptr) {
    throw std::runtime_error(std::string("core has no ") + name);
  }
  out = reinterpret_cast<Fn>(symbol);
}

} // namespace

LibretroCore::LibretroCore(CoreConfig config, const Window &window) : config_(std::move(config)) {
  if (active_ != nullptr) {
    throw std::logic_error("one libretro core per process");
  }
  std::filesystem::create_directories(config_.userDir);
  userDirText_ = config_.userDir.string();

  library_ = dlopen(config_.core.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library_ == nullptr) {
    throw std::runtime_error(std::string("dlopen: ") + dlerror());
  }
  resolve();
  active_ = this;

  api_.setEnvironment(&LibretroCore::onEnvironment);
  api_.setVideoRefresh(&LibretroCore::onVideo);
  api_.setAudioSample(&LibretroCore::onAudioSample);
  api_.setAudioSampleBatch(&LibretroCore::onAudioBatch);
  api_.setInputPoll(&LibretroCore::onInputPoll);
  api_.setInputState(&LibretroCore::onInputState);
  api_.init();

  const std::string rom = config_.rom.string();
  retro_game_info game{rom.c_str(), nullptr, 0, nullptr};
  if (!api_.loadGame(&game)) {
    api_.deinit();
    active_ = nullptr;
    dlclose(library_);
    throw std::runtime_error("core refused " + rom);
  }
  loaded_ = true;
  api_.getAvInfo(&avInfo_);
  video_.start(window);
  lucent::info("core", "loaded {}: {}x{} at {:.3f} fps, sound {} Hz", rom,
               avInfo_.geometry.base_width, avInfo_.geometry.base_height, avInfo_.timing.fps,
               avInfo_.timing.sample_rate);
}

LibretroCore::~LibretroCore() {
  if (loaded_) {
    video_.destroyContext();
    api_.unloadGame();
  }
  api_.deinit();
  active_ = nullptr;
  dlclose(library_);
}

void LibretroCore::resolve() {
  bind(library_, "retro_init", api_.init);
  bind(library_, "retro_deinit", api_.deinit);
  bind(library_, "retro_set_environment", api_.setEnvironment);
  bind(library_, "retro_set_video_refresh", api_.setVideoRefresh);
  bind(library_, "retro_set_audio_sample", api_.setAudioSample);
  bind(library_, "retro_set_audio_sample_batch", api_.setAudioSampleBatch);
  bind(library_, "retro_set_input_poll", api_.setInputPoll);
  bind(library_, "retro_set_input_state", api_.setInputState);
  bind(library_, "retro_load_game", api_.loadGame);
  bind(library_, "retro_unload_game", api_.unloadGame);
  bind(library_, "retro_run", api_.run);
  bind(library_, "retro_serialize_size", api_.serializeSize);
  bind(library_, "retro_serialize", api_.serialize);
  bind(library_, "retro_unserialize", api_.unserialize);
  bind(library_, "retro_get_system_av_info", api_.getAvInfo);
}

void LibretroCore::runFrames(unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    api_.run();
    ++framesRun_;
  }
}

std::optional<std::vector<uint8_t>> LibretroCore::saveState() {
  const size_t size = api_.serializeSize();
  if (size == 0) {
    return std::nullopt;
  }
  std::vector<uint8_t> state(size);
  if (!api_.serialize(state.data(), state.size())) {
    return std::nullopt;
  }
  return state;
}

bool LibretroCore::loadState(const std::vector<uint8_t> &state) {
  return api_.unserialize(state.data(), state.size());
}

bool LibretroCore::onEnvironment(unsigned command, void *data) {
  return active_->environment(command, data);
}

void LibretroCore::onVideo(const void *data, unsigned width, unsigned height, size_t pitch) {
  LibretroCore &core = *active_;
  if (data == RETRO_HW_FRAME_BUFFER_VALID) {
    core.video_.frameReady(width, height);
    core.hardwareFrame_ = true;
    return;
  }
  if (data != nullptr) {
    core.hardwareFrame_ = false;
  }
  core.frame_.store(data, {width, height}, pitch);
}

const Frame &LibretroCore::capture() {
  if (hardwareFrame_) {
    const VkExtent2D size = video_.frame();
    frame_.storeRows(video_.read(), {size.width, size.height});
  }
  return frame_;
}

void LibretroCore::onWriteHit(const azahar_write_hit *hit) {
  active_->hits_.push_back(*hit);
}

bool LibretroCore::watchWrites(uint32_t address, uint32_t size) {
  return debug_ != nullptr && debug_->watch_writes(address, size);
}

void LibretroCore::clearWatches() {
  if (debug_ != nullptr) {
    debug_->clear_watches();
  }
}

std::vector<azahar_write_hit> LibretroCore::takeHits() {
  return std::exchange(hits_, {});
}

void LibretroCore::onAudioSample(int16_t left, int16_t right) {
  if (active_->audio_ != nullptr) {
    const std::array<int16_t, 2> frame{left, right};
    active_->audio_->push(frame.data(), 1);
  }
}

size_t LibretroCore::onAudioBatch(const int16_t *data, size_t frames) {
  if (active_->audio_ != nullptr) {
    active_->audio_->push(data, frames);
  }
  return frames;
}

void LibretroCore::onInputPoll() {
}

int16_t LibretroCore::onInputState(unsigned port, unsigned device, unsigned index, unsigned id) {
  return active_->inputState({port, device, index, id});
}

void LibretroCore::onLog(retro_log_level level, const char *format, ...) {
  std::array<char, 2048> text{};
  va_list args;
  va_start(args, format);
  std::vsnprintf(text.data(), text.size(), format, args);
  va_end(args);
  std::string line(text.data());
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }
  if (level >= RETRO_LOG_ERROR) {
    lucent::error("core", "{}", line);
  } else if (level == RETRO_LOG_WARN) {
    lucent::warn("core", "{}", line);
  } else {
    lucent::debug("core", "{}", line);
  }
}

void LibretroCore::recordDefaults(const retro_core_option_v2_definition *definitions) {
  for (const auto *definition = definitions; definition != nullptr && definition->key != nullptr;
       ++definition) {
    if (definition->default_value != nullptr) {
      config_.options.try_emplace(definition->key, definition->default_value);
    }
  }
}

int16_t LibretroCore::inputState(const InputQuery &query) const {
  const auto [port, device, index, id] = query;
  if (port != 0) {
    return 0;
  }
  switch (device) {
  case RETRO_DEVICE_JOYPAD:
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
      return static_cast<int16_t>(pad_.buttons);
    }
    return static_cast<int16_t>((pad_.buttons >> id) & 1U);
  case RETRO_DEVICE_ANALOG:
    if (id >= 2) {
      return 0;
    }
    if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
      return pad_.left[id];
    }
    return index == RETRO_DEVICE_INDEX_ANALOG_RIGHT ? pad_.right[id] : 0;
  case RETRO_DEVICE_POINTER:
    if (id == RETRO_DEVICE_ID_POINTER_X || id == RETRO_DEVICE_ID_POINTER_Y) {
      return pad_.pointer[id == RETRO_DEVICE_ID_POINTER_Y ? 1 : 0];
    }
    return static_cast<int16_t>(id == RETRO_DEVICE_ID_POINTER_PRESSED && pad_.touching);
  case RETRO_DEVICE_MOUSE:
    return static_cast<int16_t>(id == RETRO_DEVICE_ID_MOUSE_LEFT && pad_.touching);
  default:
    return 0;
  }
}

bool LibretroCore::environment(unsigned command, void *data) {
  switch (command) {
  case AZAHAR_ENVIRONMENT_SET_DEBUG_INTERFACE:
    debug_ = static_cast<const azahar_debug_interface *>(data);
    debug_->set_write_hit(&LibretroCore::onWriteHit);
    memory_.useDebugInterface(debug_);
    return true;
  case RETRO_ENVIRONMENT_SET_HW_RENDER:
    return video_.setRender(*static_cast<const retro_hw_render_callback *>(data));
  case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
    *static_cast<unsigned *>(data) = RETRO_HW_CONTEXT_VULKAN;
    return true;
  case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
    return video_.setNegotiation(
        *static_cast<const retro_hw_render_context_negotiation_interface *>(data));
  case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
    auto *query = static_cast<retro_hw_render_context_negotiation_interface *>(data);
    query->interface_version =
        query->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN
            ? RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION
            : 0;
    return true;
  }
  case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
    return video_.renderInterface(static_cast<const retro_hw_render_interface **>(data));
  case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    *static_cast<const char **>(data) = userDirText_.c_str();
    return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: {
    auto *variable = static_cast<retro_variable *>(data);
    const auto found = config_.options.find(variable->key);
    if (found == config_.options.end()) {
      variable->value = nullptr;
      return false;
    }
    variable->value = found->second.c_str();
    return true;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    *static_cast<bool *>(data) = false;
    return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    return *static_cast<const retro_pixel_format *>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
  case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    memory_.assign(*static_cast<const retro_memory_map *>(data));
    return true;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    static_cast<retro_log_callback *>(data)->log = &LibretroCore::onLog;
    return true;
  case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    return true;
  case RETRO_ENVIRONMENT_GET_CAN_DUPE:
    *static_cast<bool *>(data) = true;
    return true;
  case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
    // Bit 0 video, bit 1 audio.
    *static_cast<int *>(data) = audio_ != nullptr ? 3 : 1;
    return true;
  case RETRO_ENVIRONMENT_SET_MESSAGE:
    lucent::info("core", "message: {}", static_cast<const retro_message *>(data)->msg);
    return true;
  case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
    lucent::info("core", "message: {}", static_cast<const retro_message_ext *>(data)->msg);
    return true;
  case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
    *static_cast<unsigned *>(data) = 2;
    return true;
  case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    recordDefaults(static_cast<const retro_core_options_v2 *>(data)->definitions);
    return true;
  case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    recordDefaults(static_cast<const retro_core_options_v2_intl *>(data)->us->definitions);
    return true;
  case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
  case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
  case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
  case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
  case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
  case RETRO_ENVIRONMENT_SET_GEOMETRY:
    avInfo_.geometry = *static_cast<const retro_game_geometry *>(data);
    return true;
  case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    avInfo_ = *static_cast<const retro_system_av_info *>(data);
    return true;
  case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    return true;
  default:
    lucent::debug("env", "unhandled environment command {}", command);
    return false;
  }
}

} // namespace z3d::app
