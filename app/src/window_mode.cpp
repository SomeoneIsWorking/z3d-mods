#include "window_mode.hpp"

#include <lucent/json.h>
#include <lucent/platform.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace z3d::app {
namespace {

constexpr const char *kFullscreen = "fullscreen";

} // namespace

WindowMode::WindowMode(std::filesystem::path stored) : stored_(std::move(stored)) {
  if (stored_.empty()) {
    return;
  }
  std::ifstream file(stored_);
  if (!file) {
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::string error;
  const auto document = lucent::json::parse(text, error);
  if (!document) {
    throw std::runtime_error(stored_.string() + " is not JSON " + error);
  }
  if (const lucent::json::Value *value = document->find(kFullscreen)) {
    if (value->boolean() == nullptr) {
      throw std::runtime_error(stored_.string() + ": fullscreen must be true or false");
    }
    fullscreen_ = *value->boolean();
  }
}

void WindowMode::setFullscreen(bool fullscreen) {
  fullscreen_ = fullscreen;
  if (stored_.empty()) {
    return;
  }
  std::string error;
  if (!lucent::platform::write_file_atomically(stored_,
                                               std::string("{\"") + kFullscreen +
                                                   "\": " + (fullscreen ? "true" : "false") + "}\n",
                                               error)) {
    throw std::runtime_error("cannot write " + stored_.string() + ": " + error);
  }
}

} // namespace z3d::app
