#include "test_pad.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_stdinc.h>

#include <lucent/log.h>

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace z3d::app {
namespace {

// An Xbox 360 pad's IDs, so the pad reads as the common one it stands for.
constexpr uint16_t kVendor = 0x045E;
constexpr uint16_t kProduct = 0x028E;
// SDL's gamepad buttons up to the D-pad: the virtual pad's mapping numbers its buttons in this
// order, so a button's index is its SDL_GamepadButton.
constexpr int kButtons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;

using lucent::http::Response;

Response refused(std::string reason) {
  return Response::text(400, "Bad Request", std::move(reason) + "\n");
}

std::optional<std::string> field(const std::vector<lucent::http::FormField> &fields,
                                 std::string_view name) {
  for (const lucent::http::FormField &each : fields) {
    if (each.name == name) {
      return each.value;
    }
  }
  return std::nullopt;
}

std::optional<int> integer(const std::optional<std::string> &text) {
  if (!text) {
    return std::nullopt;
  }
  int value = 0;
  const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
  if (error != std::errc() || end != text->data() + text->size()) {
    return std::nullopt;
  }
  return value;
}

} // namespace

TestPad::TestPad()
    : server_({}, [this](const lucent::http::Request &request) {
        return serve(request);
      }) {
  if (!server_.start()) {
    throw std::runtime_error("test pad: no loopback port to listen on");
  }
  lucent::info("pad", "test pad: listening on port {}", server_.port());
}

TestPad::~TestPad() {
  server_.stop();
  if (joystick_ != nullptr) {
    const SDL_JoystickID id = SDL_GetJoystickID(joystick_);
    SDL_CloseJoystick(joystick_);
    SDL_DetachVirtualJoystick(id);
  }
}

Response TestPad::attach() {
  if (joystick_ != nullptr) {
    return Response::text(409, "Conflict", "attached already\n");
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.vendor_id = kVendor;
  desc.product_id = kProduct;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = kButtons;
  desc.name = "z3d input test pad";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  if (id == 0) {
    return Response::text(500, "Internal Server Error", std::string(SDL_GetError()) + "\n");
  }
  joystick_ = SDL_OpenJoystick(id);
  if (joystick_ == nullptr) {
    const std::string error = SDL_GetError();
    SDL_DetachVirtualJoystick(id);
    return Response::text(500, "Internal Server Error", error + "\n");
  }
  lucent::info("pad", "test pad: attached as joystick {}", id);
  return Response::text(200, "OK", "ok\n");
}

Response TestPad::serve(const lucent::http::Request &request) {
  if (request.method != "POST") {
    return Response::text(405, "Method Not Allowed", "POST only\n");
  }
  std::vector<lucent::http::FormField> fields;
  std::string error;
  if (!lucent::http::parse_form_urlencoded(request.query(), fields, error)) {
    return refused(error);
  }
  const std::lock_guard lock(mutex_);
  const std::string_view path = request.path();
  if (path == "/attach") {
    return attach();
  }
  if (joystick_ == nullptr) {
    return Response::text(409, "Conflict", "not attached\n");
  }
  const std::string name = field(fields, "name").value_or("");
  if (path == "/button") {
    const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
    const std::optional<int> down = integer(field(fields, "down"));
    if (button == SDL_GAMEPAD_BUTTON_INVALID || button >= kButtons || !down ||
        (*down != 0 && *down != 1)) {
      return refused("button wants name=<SDL button> and down=0|1");
    }
    SDL_SetJoystickVirtualButton(joystick_, button, *down == 1);
    return Response::text(200, "OK", "ok\n");
  }
  if (path == "/axis") {
    const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.c_str());
    const std::optional<int> value = integer(field(fields, "value"));
    if (axis == SDL_GAMEPAD_AXIS_INVALID || !value ||
        *value < std::numeric_limits<int16_t>::min() ||
        *value > std::numeric_limits<int16_t>::max()) {
      return refused("axis wants name=<SDL axis> and value=-32768..32767");
    }
    SDL_SetJoystickVirtualAxis(joystick_, axis, static_cast<int16_t>(*value));
    return Response::text(200, "OK", "ok\n");
  }
  return Response::text(404, "Not Found", "no such command\n");
}

} // namespace z3d::app
