#include "options_menu.hpp"

#include "game_options.hpp"
#include "game_window.hpp"
#include "pad_input.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <lucent/log.h>

#include <algorithm>

namespace z3d::app {
namespace {

constexpr const char *kHint = "Up/Down  Select    A  Change    B  Back";
constexpr const char *kFolders = "Games and mods folders";
constexpr const char *kFoldersChange = "Change";
constexpr const char *kFoldersRow = "folders";
// Text heights as shares of the window's height, capped by its width so a row fits a narrow window.
constexpr float kHeadingShare = 1.0F / 16.0F;
constexpr float kRowShare = 1.0F / 24.0F;
constexpr float kHeadingWidthShare = 1.0F / 16.0F;
constexpr float kRowWidthShare = 1.0F / 27.0F;
constexpr float kMargin = 0.06F;
constexpr float kRowPitch = 1.9F;

struct Held {
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool change = false;
  bool back = false;

  static Held from(const MenuPad &pad) {
    return {pad.up, pad.down, pad.left, pad.right, pad.a, pad.b || pad.x};
  }
};

} // namespace

size_t OptionsMenu::rows() const {
  return options_.items().size() + (folders_ ? 1 : 0);
}

bool OptionsMenu::onFolders() const {
  return selected_ == options_.items().size();
}

void OptionsMenu::select(size_t index) {
  selected_ = index;
  lucent::info("menu", "options: row {}",
               onFolders() ? kFoldersRow : options_.items()[selected_].id);
}

void OptionsMenu::step(int direction) {
  if (onFolders()) {
    return;
  }
  options_.step(selected_, direction);
  const Option &item = options_.items()[selected_];
  lucent::info("menu", "options: {} = {}", item.id, item.choices[item.value]);
}

OptionsMenu::Left OptionsMenu::run() {
  (void)window_.takeMenuRequest();
  Held before = Held::from(pad_.menu());
  lucent::info("menu", "options: open for {}", title_);
  select(selected_);
  while (!window_.closeRequested()) {
    (void)window_.pollMenu();
    const Held now = Held::from(pad_.menu());
    const size_t count = rows();
    if (count == 0) {
      break;
    }
    if (now.up && !before.up) {
      select((selected_ + count - 1) % count);
    }
    if (now.down && !before.down) {
      select((selected_ + 1) % count);
    }
    if (now.left && !before.left) {
      step(-1);
    }
    if (now.change && !before.change && onFolders()) {
      lucent::info("menu", "options: closed for the folders");
      return Left::ForFolders;
    }
    if ((now.right && !before.right) || (now.change && !before.change)) {
      step(1);
    }
    if ((now.back && !before.back) || window_.takeMenuRequest()) {
      break;
    }
    before = now;
    draw();
  }
  lucent::info("menu", "options: closed");
  return Left::Back;
}

void OptionsMenu::text(const std::string &line, float x, float y, bool selected,
                       bool rightAligned) {
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
  if (folders_) {
    text(kFolders, left, y, onFolders(), false);
    text(kFoldersChange, right, y, onFolders(), true);
  }
  text(kHint, left, height * 0.9F, false, false);
  SDL_RenderPresent(renderer_);
}

} // namespace z3d::app
