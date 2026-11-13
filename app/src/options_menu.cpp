#include "options_menu.hpp"

#include "game_options.hpp"
#include "game_window.hpp"
#include "pad_input.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>

namespace z3d::app {
namespace {

constexpr const char *kHint = "Up/Down  Select    A  Change    B  Back";
// Text heights as shares of the window's height, capped by its width so a row fits a narrow window.
constexpr float kHeadingShare = 1.0F / 16.0F;
constexpr float kRowShare = 1.0F / 24.0F;
constexpr float kHeadingWidthShare = 1.0F / 16.0F;
constexpr float kRowWidthShare = 1.0F / 27.0F;
constexpr float kMargin = 0.06F;
constexpr float kRowPitch = 1.9F;
constexpr float kStickPress = 0.5F;

struct Held {
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool change = false;
  bool back = false;

  static Held from(const PadState &pad) {
    const auto &circle = pad.sticks[Settings::NativeAnalog::CirclePad];
    const auto &buttons = pad.buttons;
    return {buttons[Settings::NativeButton::Up] || circle[1] > kStickPress,
            buttons[Settings::NativeButton::Down] || circle[1] < -kStickPress,
            buttons[Settings::NativeButton::Left] || circle[0] < -kStickPress,
            buttons[Settings::NativeButton::Right] || circle[0] > kStickPress,
            static_cast<bool>(buttons[Settings::NativeButton::A]),
            buttons[Settings::NativeButton::B] || buttons[Settings::NativeButton::X]};
  }
};

} // namespace

void OptionsMenu::run() {
  (void)window_.takeMenuRequest();
  Held before = Held::from(pad_.state());
  while (!window_.closeRequested()) {
    (void)window_.pollMenu();
    const Held now = Held::from(pad_.state());
    const size_t count = options_.items().size();
    if (now.up && !before.up) {
      selected_ = (selected_ + count - 1) % count;
    }
    if (now.down && !before.down) {
      selected_ = (selected_ + 1) % count;
    }
    if (now.left && !before.left) {
      options_.step(selected_, -1);
    }
    if ((now.right && !before.right) || (now.change && !before.change)) {
      options_.step(selected_, 1);
    }
    if ((now.back && !before.back) || window_.takeMenuRequest()) {
      return;
    }
    before = now;
    draw();
  }
}

void OptionsMenu::text(const std::string &line, float x, float y, bool selected, bool rightAligned) {
  TTF_Text *made = TTF_CreateText(engine_, font_, line.c_str(), line.size());
  if (made == nullptr) {
    return;
  }
  if (selected) {
    TTF_SetTextColor(made, 224, 186, 72, SDL_ALPHA_OPAQUE);
  } else {
    TTF_SetTextColor(made, 236, 238, 242, SDL_ALPHA_OPAQUE);
  }
  int w = 0;
  int h = 0;
  TTF_GetTextSize(made, &w, &h);
  TTF_DrawRendererText(made, rightAligned ? x - static_cast<float>(w) : x, y);
  TTF_DestroyText(made);
}

void OptionsMenu::draw() {
  int w = 0;
  int h = 0;
  SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
  const auto width = static_cast<float>(std::max(w, 1));
  const auto height = static_cast<float>(std::max(h, 1));
  SDL_SetRenderDrawColor(renderer_, 18, 20, 26, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer_);
  const float left = width * kMargin;
  const float right = width * (1.0F - kMargin);
  TTF_SetFontSize(font_, std::min(height * kHeadingShare, width * kHeadingWidthShare));
  text(title_, left, height * 0.08F, false, false);
  const float row = std::min(height * kRowShare, width * kRowWidthShare);
  TTF_SetFontSize(font_, row);
  const float pitch = row * kRowPitch;
  float y = height * 0.24F;
  for (size_t i = 0; i < options_.items().size(); ++i) {
    const Option &item = options_.items()[i];
    text(item.label, left, y, i == selected_, false);
    text(item.choices[item.value], right, y, i == selected_, true);
    y += pitch;
  }
  text(kHint, left, height * 0.9F, false, false);
  SDL_RenderPresent(renderer_);
}

} // namespace z3d::app
