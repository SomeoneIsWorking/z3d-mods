#include "tool_process.hpp"

#include <SDL3/SDL.h>
#include <lucent/log.h>

#include <array>
#include <stdexcept>
#include <string_view>

namespace z3d::app {
namespace {

// What a line of a tool's refusal starts with (tools/z3dmods/setup.py REFUSAL).
constexpr std::string_view kRefusal = "! ";
constexpr uint32_t kWaitStepMs = 5;

} // namespace

ToolProcess::~ToolProcess() {
  if (process_ != nullptr) {
    SDL_KillProcess(process_, false);
    SDL_DestroyProcess(process_);
  }
}

void ToolProcess::start(const std::vector<std::string> &command) {
  if (process_ != nullptr) {
    throw std::logic_error("a tool is already running");
  }
  std::vector<const char *> args;
  args.reserve(command.size() + 1);
  for (const std::string &arg : command) {
    args.push_back(arg.c_str());
  }
  args.push_back(nullptr);
  const SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                         static_cast<void *>(args.data()));
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  process_ = SDL_CreateProcessWithProperties(properties);
  SDL_DestroyProperties(properties);
  if (process_ == nullptr) {
    throw std::runtime_error(command.front() + ": " + SDL_GetError());
  }
  pending_.clear();
  lastLine_.clear();
  refusal_.clear();
  exitCode_ = 0;
}

void ToolProcess::read() {
  SDL_IOStream *output = SDL_GetProcessOutput(process_);
  std::array<char, 4096> buffer{};
  for (size_t got = SDL_ReadIO(output, buffer.data(), buffer.size()); got > 0;
       got = SDL_ReadIO(output, buffer.data(), buffer.size())) {
    pending_.append(buffer.data(), got);
  }
  for (size_t end = pending_.find('\n'); end != std::string::npos; end = pending_.find('\n')) {
    if (end > 0) {
      lastLine_ = pending_.substr(0, end);
      lucent::info("tools", "{}", lastLine_);
      if (lastLine_.starts_with(kRefusal)) {
        refusal_ += (refusal_.empty() ? "" : "\n") + lastLine_.substr(kRefusal.size());
      }
    }
    pending_.erase(0, end + 1);
  }
}

bool ToolProcess::running() {
  if (process_ == nullptr) {
    return false;
  }
  read();
  if (!SDL_WaitProcess(process_, false, &exitCode_)) {
    return true;
  }
  read();
  SDL_DestroyProcess(process_);
  process_ = nullptr;
  return false;
}

void ToolProcess::wait() {
  while (running()) {
    SDL_Delay(kWaitStepMs);
  }
}

} // namespace z3d::app
