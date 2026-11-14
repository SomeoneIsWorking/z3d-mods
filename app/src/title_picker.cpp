#include "title_picker.hpp"

#include "game_window.hpp"
#include "options_menu.hpp"
#include "pad_input.hpp"
#include "set_install.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <lucent/log.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace z3d::app {
namespace {

std::runtime_error sdlError(const char *what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

constexpr const char *kHeading = "z3d";
constexpr const char *kHint = "A  Play    X  Options    Esc or Guide  Back";
constexpr const char *kSetUpHint = "A  Set up from your game    X  Options";
constexpr const char *kSetUpHeading = "Setting up ";
constexpr const char *kSetUpStarting = "Reading your game";
constexpr const char *kSetUpFailed = "Setup failed    A  Back";
// Text size as a share of the window's height.
constexpr float kTitleShare = 1.0F / 26.0F;
constexpr float kHeadingShare = 1.0F / 14.0F;
// Tile size and lift as shares of the window's height.
constexpr float kTileShare = 0.42F;
constexpr float kTileLift = 0.06F;
// A line of text wraps at this share of the window's width.
constexpr float kTextWidth = 0.9F;
// The setup screen's text fills at most this share of the window's height: its lower part
// shrinks by kShrink a step, down to kSmallestShare, until it does.
constexpr float kTextHeight = 0.9F;
constexpr float kShrink = 0.9F;
constexpr float kSmallestShare = 1.0F / 60.0F;

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};
constexpr Rgb kBackground{18, 20, 26};
constexpr Rgb kTile{34, 38, 48};
constexpr Rgb kSelected{224, 186, 72};
constexpr Rgb kText{236, 238, 242};
constexpr Rgb kDim{150, 156, 170};

void fill(SDL_Renderer *renderer, Rgb color, const SDL_FRect &rect) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(renderer, &rect);
}

TTF_Text *makeText(TTF_TextEngine *engine, TTF_Font *font, const std::string &text, Rgb color) {
  TTF_Text *made = TTF_CreateText(engine, font, text.c_str(), text.size());
  if (made == nullptr) {
    throw sdlError("TTF_CreateText");
  }
  TTF_SetTextColor(made, color.r, color.g, color.b, SDL_ALPHA_OPAQUE);
  return made;
}

SDL_FPoint textSize(TTF_Text *text) {
  int w = 0;
  int h = 0;
  TTF_GetTextSize(text, &w, &h);
  return {static_cast<float>(w), static_cast<float>(h)};
}

// What moves or chooses, as held now.
struct Held {
  bool left = false;
  bool right = false;
  bool choose = false;
  bool options = false;

  static Held from(const MenuPad &pad) {
    return {pad.left, pad.right, pad.a || pad.start, pad.x};
  }
};

} // namespace

struct TitlePicker::Layout {
  float width;
  float height;
  std::vector<SDL_FRect> tiles;
  float iconInset;
};

TitlePicker::TitlePicker(GameWindow &window, const PadInput &pad, const GameLibrary &library,
                         std::vector<GameOptions> &options, SetInstall &install,
                         const std::filesystem::path &font)
    : window_(window), pad_(pad), library_(library), options_(options), install_(install) {
  renderer_ = SDL_CreateRenderer(window_.sdlWindow(), "vulkan");
  if (renderer_ == nullptr) {
    throw sdlError("SDL_CreateRenderer");
  }
  SDL_SetRenderVSync(renderer_, 1);
  if (!TTF_Init()) {
    throw sdlError("TTF_Init");
  }
  engine_ = TTF_CreateRendererTextEngine(renderer_);
  font_ = TTF_OpenFont(font.c_str(), 1.0F);
  if (engine_ == nullptr || font_ == nullptr) {
    throw sdlError("TTF_OpenFont");
  }
  TTF_SetFontWrapAlignment(font_, TTF_HORIZONTAL_ALIGN_CENTER);
  for (const Game &game : library_.games()) {
    SDL_Texture *icon = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                          SDL_TEXTUREACCESS_STATIC, game.iconSize, game.iconSize);
    if (icon == nullptr) {
      throw sdlError("SDL_CreateTexture");
    }
    SDL_UpdateTexture(icon, nullptr, game.icon.data(),
                      game.iconSize * static_cast<int>(sizeof(uint32_t)));
    SDL_SetTextureScaleMode(icon, SDL_SCALEMODE_PIXELART);
    icons_.push_back(icon);
    titles_.push_back(makeText(engine_, font_, game.title, kText));
  }
  heading_ = makeText(engine_, font_, kHeading, kText);
  hint_ = makeText(engine_, font_, hint(), kDim);
}

TitlePicker::~TitlePicker() {
  for (TTF_Text *text : titles_) {
    TTF_DestroyText(text);
  }
  TTF_DestroyText(heading_);
  TTF_DestroyText(hint_);
  for (SDL_Texture *icon : icons_) {
    SDL_DestroyTexture(icon);
  }
  TTF_CloseFont(font_);
  if (engine_ != nullptr) {
    TTF_DestroyRendererTextEngine(engine_);
  }
  TTF_Quit();
  SDL_DestroyRenderer(renderer_);
}

std::string TitlePicker::hint() const {
  return install_.wanted(library_.games()[selected_].setup) ? kSetUpHint : kHint;
}

void TitlePicker::select(size_t index) {
  selected_ = index;
  // The bottom line is the selected game's.
  textHeight_ = 0.0F;
  lucent::info("menu", "picker: on {}", library_.games()[selected_].title);
}

std::optional<size_t> TitlePicker::run() {
  (void)window_.takeMenuRequest();
  // What is held when the picker opens (the A that chose it, say) is not a press.
  Held before = Held::from(pad_.menu());
  select(selected_);
  while (!window_.closeRequested()) {
    const std::vector<SDL_FPoint> clicks = window_.pollMenu();
    (void)window_.takeMenuRequest();
    const Layout current = layout();
    std::optional<size_t> chosen;
    for (const SDL_FPoint &click : clicks) {
      if (const auto index = hit(current, click.x, click.y)) {
        chosen = index;
      }
    }
    const Held now = Held::from(pad_.menu());
    const size_t count = library_.games().size();
    if (now.left && !before.left) {
      select((selected_ + count - 1) % count);
    }
    if (now.right && !before.right) {
      select((selected_ + 1) % count);
    }
    if (now.choose && !before.choose) {
      chosen = selected_;
    }
    if (chosen) {
      if (!install_.wanted(library_.games()[*chosen].setup) || setUp(*chosen)) {
        return chosen;
      }
      before = Held::from(pad_.menu());
      select(*chosen);
      continue;
    }
    if (now.options && !before.options) {
      OptionsMenu(renderer_, engine_, font_, window_, pad_, options_[selected_],
                  library_.games()[selected_].title)
          .run();
      // The menu left the font at its own size.
      textHeight_ = 0.0F;
      before = Held::from(pad_.menu());
      select(selected_);
      continue;
    }
    before = now;
    draw(current);
  }
  return std::nullopt;
}

TitlePicker::Layout TitlePicker::layout() const {
  int w = 0;
  int h = 0;
  SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
  Layout result{static_cast<float>(std::max(w, 1)), static_cast<float>(std::max(h, 1)), {}, 0.0F};
  const auto count = static_cast<float>(library_.games().size());
  const float gap = result.width / (count * 6.0F + 1.0F);
  const float side =
      std::min((result.width - gap * (count + 1.0F)) / count, result.height * kTileShare);
  const float left = (result.width - side * count - gap * (count - 1.0F)) / 2.0F;
  // Above centre, leaving room for two lines of title over the hint.
  const float top = (result.height - side) / 2.0F - result.height * kTileLift;
  for (size_t i = 0; i < library_.games().size(); ++i) {
    result.tiles.push_back({left + static_cast<float>(i) * (side + gap), top, side, side});
  }
  result.iconInset = side * 0.12F;
  return result;
}

void TitlePicker::fitText(float height) {
  if (height == textHeight_) {
    return;
  }
  textHeight_ = height;
  TTF_SetFontSize(font_, height * kHeadingShare);
  TTF_DestroyText(heading_);
  heading_ = makeText(engine_, font_, kHeading, kText);
  TTF_SetFontSize(font_, height * kTitleShare);
  TTF_DestroyText(hint_);
  hint_ = makeText(engine_, font_, hint(), kDim);
  for (size_t i = 0; i < titles_.size(); ++i) {
    TTF_DestroyText(titles_[i]);
    titles_[i] = makeText(engine_, font_, library_.games()[i].title, kText);
  }
}

void TitlePicker::draw(const Layout &layout) {
  fitText(layout.height);
  SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer_);
  const SDL_FPoint heading = textSize(heading_);
  TTF_DrawRendererText(heading_, (layout.width - heading.x) / 2.0F, layout.height * 0.08F);
  for (size_t i = 0; i < layout.tiles.size(); ++i) {
    const SDL_FRect &tile = layout.tiles[i];
    if (i == selected_) {
      const float ring = layout.iconInset * 0.35F;
      fill(renderer_, kSelected,
           {tile.x - ring, tile.y - ring, tile.w + 2 * ring, tile.h + 2 * ring});
    }
    fill(renderer_, kTile, tile);
    const float inset = layout.iconInset;
    const SDL_FRect icon{tile.x + inset, tile.y + inset, tile.w - 2 * inset, tile.h - 2 * inset};
    SDL_RenderTexture(renderer_, icons_[i], nullptr, &icon);
    TTF_SetTextWrapWidth(titles_[i], static_cast<int>(tile.w));
    const SDL_FPoint title = textSize(titles_[i]);
    TTF_DrawRendererText(titles_[i], tile.x + (tile.w - title.x) / 2.0F, tile.y + tile.h + inset);
  }
  TTF_SetTextWrapWidth(hint_, static_cast<int>(layout.width * kTextWidth));
  const SDL_FPoint hint = textSize(hint_);
  TTF_DrawRendererText(hint_, (layout.width - hint.x) / 2.0F, layout.height * 0.92F - hint.y);
  SDL_RenderPresent(renderer_);
}

bool TitlePicker::setUp(size_t index) {
  const Game &game = library_.games()[index];
  const std::string heading = kSetUpHeading + game.title;
  lucent::info("menu", "picker: setting up {}", game.title);
  install_.start(game.setup);
  while (install_.running()) {
    (void)window_.pollMenu();
    if (window_.closeRequested()) {
      return false;
    }
    drawSetUp(heading, install_.lastLine().empty() ? kSetUpStarting : install_.lastLine());
  }
  if (install_.succeeded()) {
    return true;
  }
  lucent::error("menu", "picker: setting up {} failed", game.title);
  Held before = Held::from(pad_.menu());
  while (!window_.closeRequested()) {
    (void)window_.pollMenu();
    const Held now = Held::from(pad_.menu());
    if ((now.choose && !before.choose) || window_.takeMenuRequest()) {
      break;
    }
    before = now;
    drawSetUp(kSetUpFailed, install_.failure());
  }
  return false;
}

void TitlePicker::drawSetUp(const std::string &heading, const std::string &line) {
  const Layout current = layout();
  fitText(current.height);
  SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer_);
  TTF_SetFontSize(font_, current.height * kHeadingShare);
  TTF_Text *top = makeText(engine_, font_, heading, kText);
  TTF_SetTextWrapWidth(top, static_cast<int>(current.width * kTextWidth));
  const SDL_FPoint topSize = textSize(top);
  // The two together sit in the middle; the lower one shrinks until its lines fit under the other.
  const float gap = current.height * 0.05F;
  const float room = current.height * kTextHeight - topSize.y - gap;
  float size = current.height * kTitleShare;
  TTF_Text *bottom = nullptr;
  SDL_FPoint bottomSize{};
  do {
    if (bottom != nullptr) {
      TTF_DestroyText(bottom);
      size *= kShrink;
    }
    TTF_SetFontSize(font_, size);
    bottom = makeText(engine_, font_, line, kDim);
    TTF_SetTextWrapWidth(bottom, static_cast<int>(current.width * kTextWidth));
    bottomSize = textSize(bottom);
  } while (bottomSize.y > room && size > current.height * kSmallestShare);
  const float y = (current.height - topSize.y - gap - bottomSize.y) / 2.0F;
  TTF_DrawRendererText(top, (current.width - topSize.x) / 2.0F, y);
  TTF_DestroyText(top);
  TTF_DrawRendererText(bottom, (current.width - bottomSize.x) / 2.0F, y + topSize.y + gap);
  TTF_DestroyText(bottom);
  SDL_RenderPresent(renderer_);
}

std::optional<size_t> TitlePicker::hit(const Layout &layout, float x, float y) const {
  for (size_t i = 0; i < layout.tiles.size(); ++i) {
    const SDL_FRect &tile = layout.tiles[i];
    if (x >= tile.x && x < tile.x + tile.w && y >= tile.y && y < tile.y + tile.h) {
      return i;
    }
  }
  return std::nullopt;
}

} // namespace z3d::app
