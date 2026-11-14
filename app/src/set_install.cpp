#include "set_install.hpp"

#include <SDL3/SDL.h>
#include <lucent/log.h>

#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

// What a line of the installer's refusal starts with (tools/z3dmods/setup.py REFUSAL).
constexpr std::string_view kRefusal = "! ";

} // namespace

SetInstall::SetInstall(std::vector<std::string> command, std::filesystem::path userDir)
    : command_(std::move(command)), userDir_(std::move(userDir)) {
}

SetInstall::~SetInstall() {
  if (process_ != nullptr) {
    SDL_KillProcess(process_, false);
    SDL_DestroyProcess(process_);
  }
}

bool SetInstall::wanted(const GameSetup &game) const {
  return !command_.empty() && !game.options.empty() && !std::filesystem::exists(userDir_ / game.options);
}

void SetInstall::start(const GameSetup &game) {
  if (process_ != nullptr) {
    throw std::logic_error("an installer is already running");
  }
  std::vector<std::string> command = command_;
  command.push_back(game.rom.string());
  std::vector<const char *> args;
  for (const std::string &arg : command) {
    args.push_back(arg.c_str());
  }
  args.push_back(nullptr);
  const SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, static_cast<void *>(args.data()));
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  process_ = SDL_CreateProcessWithProperties(properties);
  SDL_DestroyProperties(properties);
  if (process_ == nullptr) {
    throw std::runtime_error(command.front() + ": " + SDL_GetError());
  }
  manifest_ = userDir_ / game.options;
  pending_.clear();
  lastLine_.clear();
  refusal_.clear();
  succeeded_ = false;
  lucent::info("install", "running {} on {}", command.front(), game.rom.string());
}

void SetInstall::read() {
  SDL_IOStream *output = SDL_GetProcessOutput(process_);
  std::array<char, 4096> buffer{};
  for (size_t got = SDL_ReadIO(output, buffer.data(), buffer.size()); got > 0;
       got = SDL_ReadIO(output, buffer.data(), buffer.size())) {
    pending_.append(buffer.data(), got);
  }
  for (size_t end = pending_.find('\n'); end != std::string::npos; end = pending_.find('\n')) {
    if (end > 0) {
      lastLine_ = pending_.substr(0, end);
      lucent::info("install", "{}", lastLine_);
      if (lastLine_.starts_with(kRefusal)) {
        refusal_ += (refusal_.empty() ? "" : "\n") + lastLine_.substr(kRefusal.size());
      }
    }
    pending_.erase(0, end + 1);
  }
}

bool SetInstall::running() {
  if (process_ == nullptr) {
    return false;
  }
  read();
  int code = 0;
  if (!SDL_WaitProcess(process_, false, &code)) {
    return true;
  }
  read();
  SDL_DestroyProcess(process_);
  process_ = nullptr;
  succeeded_ = code == 0 && std::filesystem::exists(manifest_);
  fresh_ = fresh_ || succeeded_;
  lucent::info("install", "installer ended with {}, the set is {}", code, succeeded_ ? "in place" : "missing");
  return false;
}

} // namespace z3d::app
