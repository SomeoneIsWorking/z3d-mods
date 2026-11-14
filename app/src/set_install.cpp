#include "set_install.hpp"

#include <lucent/json.h>
#include <lucent/log.h>

#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

// The manifest's key for what the set was made from (tools/z3dmods/set_inputs.py KEY).
constexpr std::string_view kInputs = "inputs";

} // namespace

SetInstall::SetInstall(std::vector<std::string> tools, std::filesystem::path userDir)
    : tools_(std::move(tools)), userDir_(std::move(userDir)) {
}

SetInstall::State SetInstall::state(const GameSetup &game) const {
  if (tools_.empty() || game.options.empty()) {
    return State::Ready;
  }
  std::ifstream file(userDir_ / game.options);
  if (!file) {
    return State::Missing;
  }
  if (game.inputs.empty()) {
    return State::Ready;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto manifest = lucent::json::parse(text, error);
  const lucent::json::Value *made = manifest ? manifest->find(kInputs) : nullptr;
  return made != nullptr && made->string() != nullptr && *made->string() == game.inputs ? State::Ready
                                                                                        : State::Outdated;
}

void SetInstall::start(const GameSetup &game) {
  std::vector<std::string> command = tools_;
  command.insert(command.end(), {"setup", "--user", userDir_.string(), game.rom.string()});
  tool_.start(command);
  game_ = game;
  active_ = true;
  succeeded_ = false;
  lucent::info("install", "setting up {}", game.rom.string());
}

bool SetInstall::running() {
  if (!active_) {
    return false;
  }
  if (tool_.running()) {
    return true;
  }
  active_ = false;
  succeeded_ = tool_.exitCode() == 0 && state(game_) == State::Ready;
  fresh_ = fresh_ || succeeded_;
  lucent::info("install", "installer ended with {}, the set is {}", tool_.exitCode(),
               succeeded_ ? "in place" : "missing");
  return false;
}

} // namespace z3d::app
