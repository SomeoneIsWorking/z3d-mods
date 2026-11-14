// The port-assets art that stands for a host input, for whoever draws a prompt: the play HUD
// (hud.hpp) and the menus' hints (menu_canvas.hpp).
#pragma once

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>

struct SDL_Surface;

namespace z3d::app {

// The file of a set's art in a port-assets copy, and the art drawn at a size; null when the
// file is missing or unreadable (SDL_GetError says why). The caller destroys the surface.
[[nodiscard]] std::filesystem::path setArtFile(const std::filesystem::path &assets, std::string_view set,
                                               std::string_view name);
[[nodiscard]] SDL_Surface *loadSetArt(const std::filesystem::path &assets, std::string_view set,
                                      std::string_view name, int width, int height);

// The sets (sets/<set>/<name>.svg): an Xbox pad's glyphs, and the key caps and mouse buttons.
inline constexpr const char *kPadSet = "gamepad-xbox360";
inline constexpr const char *kKeySet = "keyboard";
// A key's cap for one character, and the one for a word, this many times as wide as it is tall.
inline constexpr const char *kKeyCap = "cap";
inline constexpr const char *kWideCap = "cap_wide";
inline constexpr float kWideCapAspect = 1.5F;

// The glyph of each pad button a prompt can name.
inline constexpr std::array kPadGlyphs{
    std::pair{SDL_GAMEPAD_BUTTON_SOUTH, std::string_view("face_a")},
    std::pair{SDL_GAMEPAD_BUTTON_EAST, std::string_view("face_b")},
    std::pair{SDL_GAMEPAD_BUTTON_WEST, std::string_view("face_x")},
    std::pair{SDL_GAMEPAD_BUTTON_NORTH, std::string_view("face_y")},
    std::pair{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, std::string_view("lb")},
    std::pair{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, std::string_view("rb")},
    std::pair{SDL_GAMEPAD_BUTTON_RIGHT_STICK, std::string_view("rs")},
    std::pair{SDL_GAMEPAD_BUTTON_DPAD_UP, std::string_view("dpad_up")},
    std::pair{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, std::string_view("dpad_right")},
    std::pair{SDL_GAMEPAD_BUTTON_DPAD_DOWN, std::string_view("dpad_down")},
    std::pair{SDL_GAMEPAD_BUTTON_DPAD_LEFT, std::string_view("dpad_left")},
    std::pair{SDL_GAMEPAD_BUTTON_START, std::string_view("start")},
    std::pair{SDL_GAMEPAD_BUTTON_BACK, std::string_view("back")},
    std::pair{SDL_GAMEPAD_BUTTON_GUIDE, std::string_view("guide")}};

// Empty for a button without one.
[[nodiscard]] constexpr std::string_view glyphOf(SDL_GamepadButton button) {
  for (const auto &[pad, glyph] : kPadGlyphs) {
    if (pad == button) {
      return glyph;
    }
  }
  return {};
}

// The cap of each mouse button a prompt can name (SDL_BUTTON_*), in the key set.
inline constexpr std::array kMouseCaps{std::pair{uint8_t{SDL_BUTTON_LEFT}, std::string_view("mouse_left")},
                                       std::pair{uint8_t{SDL_BUTTON_RIGHT}, std::string_view("mouse_right")},
                                       std::pair{uint8_t{SDL_BUTTON_MIDDLE}, std::string_view("mouse_middle")}};

} // namespace z3d::app
