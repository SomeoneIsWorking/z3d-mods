#include "title_picker.hpp"

#include "core_settings.hpp"
#include "frame.hpp"
#include "game_window.hpp"
#include "options_menu.hpp"
#include "pad_input.hpp"
#include "set_install.hpp"

#include "common/settings.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <lucent/log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace z3d::app {
namespace {

std::runtime_error sdlError(const char *what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

constexpr Rgb kBackground = kMenuBackground;
constexpr Rgb kTile = kMenuTile;
constexpr Rgb kEdge = kMenuSelected;
constexpr Rgb kText = kMenuText;
constexpr Rgb kDim = kMenuDim;

constexpr const char *kPlay = "Play";
constexpr const char *kSetUp = "Set up from your game";
constexpr const char *kUpdate = "Set up again for this version";
constexpr const char *kOptions = "Options";
constexpr const char *kReturn = "Back here from a game";
constexpr const char *kBack = "Back";
constexpr const char *kSetUpHeading = "Setting up ";
constexpr const char *kSetUpStarting = "Reading your game";
constexpr const char *kSetUpFailed = "Setup failed";
// Text size as a share of the window's height.
constexpr float kTitleShare = 1.0F / 22.0F;
constexpr float kHeadingShare = 1.0F / 14.0F;
// A line of text wraps at this share of the window's width.
constexpr float kTextWidth = 0.9F;
// The setup screen's text fills at most this share of the window's height: its lower part
// shrinks by kShrink a step, down to kSmallestShare, until it does.
constexpr float kTextHeight = 0.9F;
constexpr float kShrink = 0.9F;
constexpr float kSmallestShare = 1.0F / 60.0F;
// The share of the window's height above a line of hints.
constexpr float kAboveHints = 0.86F;

// The selected side's share of the window's width.
constexpr float kWide = 0.64F;
// How far the edge leans: its top is this share of the window's height right of its bottom.
constexpr float kSlant = 0.3F;
// The edge's line, as a share of the window's height.
constexpr float kEdgeLine = 0.006F;
// The edge eases with this time constant: it closes 63% of its way in that time.
constexpr float kEaseSeconds = 0.12F;
// A held game's frame: grey at this brightness.
constexpr float kGreyLight = 0.5F;
// The band behind the titles and hints: its top as a share of the window's height, its alpha.
constexpr float kBandTop = 0.7F;
constexpr uint8_t kBandAlpha = 170;
constexpr float kTitleTop = 0.75F;
// A game without a frame yet: its icon's size and middle as shares of the window's height.
constexpr float kIconShare = 0.3F;
constexpr float kIconMiddle = 0.36F;

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

SDL_FColor color(Rgb rgb, float alpha = 1.0F) {
  return {static_cast<float>(rgb.r) / 255.0F, static_cast<float>(rgb.g) / 255.0F,
          static_cast<float>(rgb.b) / 255.0F, alpha};
}

// The frame into `texture`, made again when its size changed; `grey` darkens it to grey.
void upload(SDL_Renderer *renderer, SDL_Texture *&texture, const Frame &frame, bool grey) {
  const auto width = static_cast<int>(frame.width());
  const auto height = static_cast<int>(frame.height());
  if (texture != nullptr && (texture->w != width || texture->h != height)) {
    SDL_DestroyTexture(texture);
    texture = nullptr;
  }
  if (texture == nullptr) {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                width, height);
    if (texture == nullptr) {
      throw sdlError("SDL_CreateTexture");
    }
  }
  if (!grey) {
    SDL_UpdateTexture(texture, nullptr, frame.pixels().data(), width * 4);
    return;
  }
  std::vector<uint32_t> pixels(frame.pixels().size());
  std::ranges::transform(frame.pixels(), pixels.begin(), [](uint32_t pixel) {
    const uint32_t luma =
        ((pixel >> 16 & 0xFFU) * 77 + (pixel >> 8 & 0xFFU) * 150 + (pixel & 0xFFU) * 29) >> 8;
    const auto level = static_cast<uint32_t>(static_cast<float>(luma) * kGreyLight);
    return level << 16 | level << 8 | level;
  });
  SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 4);
}

// A four-cornered shape (top left, top right, bottom right, bottom left), with `texture` drawn
// over `box` as if it covered it, or filled with `fill`.
void quad(SDL_Renderer *renderer, std::array<SDL_FPoint, 4> corners, SDL_Texture *texture,
          SDL_FRect box, SDL_FColor fill) {
  std::array<SDL_Vertex, 4> vertices{};
  for (size_t i = 0; i < corners.size(); ++i) {
    vertices[i].position = corners[i];
    vertices[i].color = texture != nullptr ? SDL_FColor{1.0F, 1.0F, 1.0F, 1.0F} : fill;
  }
  if (texture != nullptr) {
    const auto width = static_cast<float>(texture->w);
    const auto height = static_cast<float>(texture->h);
    const float scale = std::max(box.w / width, box.h / height);
    const float drawnW = width * scale;
    const float drawnH = height * scale;
    const float left = box.x + (box.w - drawnW) / 2.0F;
    const float top = box.y + (box.h - drawnH) / 2.0F;
    for (SDL_Vertex &vertex : vertices) {
      vertex.tex_coord = {(vertex.position.x - left) / drawnW, (vertex.position.y - top) / drawnH};
    }
  }
  constexpr std::array<int, 6> kTriangles{0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()),
                     kTriangles.data(), static_cast<int>(kTriangles.size()));
}

} // namespace

TitlePicker::TitlePicker(const AppConfig &config, const MenuCanvas &canvas, GameWindow &window,
                         PadInput &pad, const GameLibrary &library,
                         std::vector<GameOptions> &options, SetInstall &install, bool folders)
    : config_(config), canvas_(canvas), renderer_(canvas.renderer()), font_(canvas.font()),
      window_(window), pad_(pad), library_(library), options_(options), install_(install),
      folders_(folders), sides_(library.games().size()) {
  if (sides_.empty() || sides_.size() > 2) {
    throw std::runtime_error("the title picker shows one or two games, not " +
                             std::to_string(sides_.size()));
  }
  TTF_SetFontWrapAlignment(font_, TTF_HORIZONTAL_ALIGN_CENTER);
  for (size_t i = 0; i < sides_.size(); ++i) {
    const Game &game = library_.games()[i];
    SDL_Texture *icon = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                          SDL_TEXTUREACCESS_STATIC, game.iconSize, game.iconSize);
    if (icon == nullptr) {
      throw sdlError("SDL_CreateTexture");
    }
    SDL_UpdateTexture(icon, nullptr, game.icon.data(),
                      game.iconSize * static_cast<int>(sizeof(uint32_t)));
    SDL_SetTextureScaleMode(icon, SDL_SCALEMODE_PIXELART);
    sides_[i].icon = icon;
    sides_[i].title = canvas_.text(game.title, kText);
  }
  // The games render off the window and their input is the picker's.
  Settings::values.present_to_window = false;
  window_.setMenuPumps(true);
  pad_.setMode(PadMode::None);
}

TitlePicker::~TitlePicker() {
  // The game played next takes the window on its first frame, after the canvas is gone.
  Settings::values.present_to_window = true;
  window_.setMenuPumps(false);
  pad_.setMode(PadMode::ByName);
  for (Side &side : sides_) {
    TTF_DestroyText(side.title);
    SDL_DestroyTexture(side.icon);
    SDL_DestroyTexture(side.frame);
    SDL_DestroyTexture(side.grey);
  }
  TTF_SetFontWrapAlignment(font_, TTF_HORIZONTAL_ALIGN_LEFT);
}

std::vector<Hint> TitlePicker::hints() const {
  const Hint options{{PadInput::menuPrompt(&MenuPad::x)}, kOptions};
  const auto choose = [](const char *words) { return Hint{{PadInput::menuPrompt(&MenuPad::a)}, words}; };
  switch (install_.state(library_.games()[selected_].setup)) {
  case SetInstall::State::Missing:
    return {choose(kSetUp), options};
  case SetInstall::State::Outdated:
    return {choose(kUpdate), options};
  case SetInstall::State::Ready:
    break;
  }
  return {choose(kPlay), options, Hint{{GameWindow::menuPrompt()}, kReturn}};
}

void TitlePicker::hintLine(const Stage &stage, std::span<const Hint> line) const {
  const float size = stage.height * kTitleShare;
  canvas_.hints(line, pad_.lastDevice(),
                {stage.width / 2.0F, stage.height * 0.92F - size * MenuCanvas::kHintArtShare}, size,
                stage.width * kTextWidth, true);
}

void TitlePicker::load(size_t index) {
  const Game &game = library_.games()[index];
  if (install_.state(game.setup) != SetInstall::State::Ready) {
    return;
  }
  lucent::info("menu", "picker: loading {}", game.title);
  applyGameSettings(options_[index].setup());
  sides_[index].emulator = std::make_unique<Emulator>(config_, game.setup.rom, window_);
  options_[index].apply(*sides_[index].emulator, pad_);
}

void TitlePicker::step() {
  Side &side = sides_[selected_];
  if (!side.emulator) {
    return;
  }
  side.emulator->runFrames(1, Emulator::Capture::Top);
  if (!side.emulator->frame().pixels().empty()) {
    upload(renderer_, side.frame, side.emulator->frame(), false);
  }
}

void TitlePicker::select(size_t index) {
  if (index != selected_ && sides_[selected_].emulator &&
      !sides_[selected_].emulator->frame().pixels().empty()) {
    upload(renderer_, sides_[selected_].grey, sides_[selected_].emulator->frame(), true);
  }
  selected_ = index;
  lucent::info("menu", "picker: on {}", library_.games()[selected_].title);
}

TitlePicker::Stage TitlePicker::stage() const {
  int w = 0;
  int h = 0;
  SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
  const auto width = static_cast<float>(std::max(w, 1));
  const auto height = static_cast<float>(std::max(h, 1));
  const float middle = edge_ * width;
  const float lean = height * kSlant / 2.0F;
  return {width, height, middle + lean, middle - lean};
}

float TitlePicker::edgeTarget(const Stage &stage) const {
  if (picking_ || sides_.size() == 1) {
    // Past the window's side by the lean and a little more.
    const float past = (stage.height * kSlant / 2.0F) / stage.width + 0.02F;
    return selected_ == 0 ? 1.0F + past : -past;
  }
  return selected_ == 0 ? kWide : 1.0F - kWide;
}

bool TitlePicker::slide(const Stage &stage) {
  const uint64_t now = SDL_GetTicks();
  const float seconds = std::min(static_cast<float>(now - edgeTicks_) / 1000.0F, 0.1F);
  edgeTicks_ = now;
  const float target = edgeTarget(stage);
  edge_ += (target - edge_) * (1.0F - std::exp(-seconds / kEaseSeconds));
  if (!picking_) {
    return false;
  }
  const Stage moved = this->stage();
  return selected_ == 0 ? std::min(moved.top, moved.bottom) >= moved.width
                        : std::max(moved.top, moved.bottom) <= 0.0F;
}

std::optional<TitlePicker::Picked> TitlePicker::run() {
  (void)window_.takeMenuRequest();
  // What is held when the picker opens (the A that chose it, say) is not a press.
  Held before = Held::from(pad_.menu());
  select(selected_);
  edge_ = edgeTarget(stage());
  edgeTicks_ = SDL_GetTicks();
  draw(stage());
  // The selected game first, so it starts while the other loads.
  load(selected_);
  for (size_t i = 0; i < sides_.size(); ++i) {
    if (i != selected_) {
      load(i);
    }
  }
  while (!window_.closeRequested()) {
    step();
    const std::vector<SDL_FPoint> clicks = window_.pollMenu();
    (void)window_.takeMenuRequest();
    const Stage current = stage();
    if (picking_) {
      if (slide(current)) {
        return Picked{selected_, std::move(sides_[selected_].emulator)};
      }
      draw(this->stage());
      continue;
    }
    std::optional<size_t> chosen;
    for (const SDL_FPoint &click : clicks) {
      const size_t side = sideAt(current, click);
      if (side == selected_) {
        chosen = side;
      } else {
        select(side);
      }
    }
    const Held now = Held::from(pad_.menu());
    if (now.left && !before.left) {
      select(0);
    }
    if (now.right && !before.right) {
      select(sides_.size() - 1);
    }
    if (now.choose && !before.choose) {
      chosen = selected_;
    }
    if (chosen) {
      if (install_.wanted(library_.games()[*chosen].setup)) {
        if (setUp(*chosen)) {
          return Picked{chosen, nullptr};
        }
        before = Held::from(pad_.menu());
        select(*chosen);
        continue;
      }
      lucent::info("menu", "picker: picked {}", library_.games()[*chosen].title);
      picking_ = true;
      continue;
    }
    if (now.options && !before.options) {
      OptionsMenu menu(canvas_, window_, pad_, options_[selected_],
                       library_.games()[selected_].title, folders_);
      if (menu.run() == OptionsMenu::Left::ForFolders) {
        return Picked{};
      }
      if (sides_[selected_].emulator) {
        options_[selected_].apply(*sides_[selected_].emulator, pad_);
      }
      // The menu left the font at its own size.
      textHeight_ = 0.0F;
      before = Held::from(pad_.menu());
      select(selected_);
      continue;
    }
    before = now;
    (void)slide(current);
    draw(this->stage());
  }
  return std::nullopt;
}

void TitlePicker::fitText(float height) {
  if (height == textHeight_) {
    return;
  }
  textHeight_ = height;
  TTF_SetFontSize(font_, height * kTitleShare);
  for (size_t i = 0; i < sides_.size(); ++i) {
    TTF_DestroyText(sides_[i].title);
    sides_[i].title = canvas_.text(library_.games()[i].title, kText);
  }
}

size_t TitlePicker::sideAt(const Stage &stage, SDL_FPoint at) const {
  if (sides_.size() == 1) {
    return 0;
  }
  const float edge = stage.top + (stage.bottom - stage.top) * at.y / stage.height;
  return at.x < edge ? 0 : 1;
}

void TitlePicker::drawSide(const Stage &stage, size_t index, bool title) {
  const Side &side = sides_[index];
  const float w = stage.width;
  const float h = stage.height;
  const bool left = index == 0;
  // A lone game's side is the whole window.
  const float top = sides_.size() == 1 ? w : stage.top;
  const float bottom = sides_.size() == 1 ? w : stage.bottom;
  const std::array<SDL_FPoint, 4> corners =
      left ? std::array<SDL_FPoint, 4>{{{0.0F, 0.0F}, {top, 0.0F}, {bottom, h}, {0.0F, h}}}
           : std::array<SDL_FPoint, 4>{{{top, 0.0F}, {w, 0.0F}, {w, h}, {bottom, h}}};
  const float from = left ? 0.0F : std::clamp(std::min(top, bottom), 0.0F, w);
  const float to = left ? std::clamp(std::max(top, bottom), 0.0F, w) : w;
  if (to - from < 1.0F) {
    return;
  }
  // Across the side at half height: where its icon and title go.
  const float middle = (top + bottom) / 2.0F;
  const float across = left ? middle : w - middle;
  const float centre = left ? middle / 2.0F : (middle + w) / 2.0F;
  if (title) {
    if (across < h * kTitleShare * 2.0F) {
      return;
    }
    const Rgb ink = index == selected_ ? kText : kDim;
    TTF_SetTextColor(side.title, ink.r, ink.g, ink.b, SDL_ALPHA_OPAQUE);
    TTF_SetTextWrapWidth(side.title, static_cast<int>(across * 0.85F));
    const SDL_FPoint size = textSize(side.title);
    TTF_DrawRendererText(side.title, centre - size.x / 2.0F, h * kTitleTop);
    return;
  }
  SDL_Texture *shown = index == selected_ ? side.frame : side.grey;
  if (shown != nullptr) {
    quad(renderer_, corners, shown, {from, 0.0F, to - from, h}, {});
  } else {
    quad(renderer_, corners, nullptr, {}, color(kTile));
    const float size = std::min(h * kIconShare, across * 0.55F);
    if (size >= 4.0F) {
      const SDL_FRect icon{centre - size / 2.0F, h * kIconMiddle - size / 2.0F, size, size};
      SDL_RenderTexture(renderer_, side.icon, nullptr, &icon);
    }
  }
}

void TitlePicker::draw(const Stage &stage) {
  fitText(stage.height);
  SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer_);
  for (size_t i = 0; i < sides_.size(); ++i) {
    drawSide(stage, i, false);
  }
  if (sides_.size() == 2) {
    const float line = std::max(2.0F, stage.height * kEdgeLine);
    quad(renderer_,
         {{{stage.top - line, 0.0F},
           {stage.top + line, 0.0F},
           {stage.bottom + line, stage.height},
           {stage.bottom - line, stage.height}}},
         nullptr, {}, color(kEdge));
  }
  if (!picking_) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, kBandAlpha);
    const SDL_FRect band{0.0F, stage.height * kBandTop, stage.width,
                         stage.height * (1.0F - kBandTop)};
    SDL_RenderFillRect(renderer_, &band);
    for (size_t i = 0; i < sides_.size(); ++i) {
      drawSide(stage, i, true);
    }
    hintLine(stage, hints());
  }
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
    drawSetUp(heading, install_.lastLine().empty() ? kSetUpStarting : install_.lastLine(), {});
  }
  if (install_.succeeded()) {
    return true;
  }
  lucent::error("menu", "picker: setting up {} failed", game.title);
  Held before = Held::from(pad_.menu());
  const std::array back{Hint{{PadInput::menuPrompt(&MenuPad::a)}, kBack}};
  while (!window_.closeRequested()) {
    (void)window_.pollMenu();
    const Held now = Held::from(pad_.menu());
    if ((now.choose && !before.choose) || window_.takeMenuRequest()) {
      break;
    }
    before = now;
    drawSetUp(kSetUpFailed, install_.failure(), back);
  }
  return false;
}

void TitlePicker::drawSetUp(const std::string &heading, const std::string &line, std::span<const Hint> below) {
  const Stage current = stage();
  // The titles are made again at their size on the stage's next frame.
  textHeight_ = 0.0F;
  SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(renderer_);
  TTF_SetFontSize(font_, current.height * kHeadingShare);
  TTF_Text *top = canvas_.text(heading, kText);
  TTF_SetTextWrapWidth(top, static_cast<int>(current.width * kTextWidth));
  const SDL_FPoint topSize = textSize(top);
  // The two together sit in the middle; the lower one shrinks until its lines fit under the other.
  const float gap = current.height * 0.05F;
  const float area = current.height * (below.empty() ? 1.0F : kAboveHints);
  const float room = area * kTextHeight - topSize.y - gap;
  float size = current.height * kTitleShare;
  TTF_Text *bottom = nullptr;
  SDL_FPoint bottomSize{};
  do {
    if (bottom != nullptr) {
      TTF_DestroyText(bottom);
      size *= kShrink;
    }
    TTF_SetFontSize(font_, size);
    bottom = canvas_.text(line, kDim);
    TTF_SetTextWrapWidth(bottom, static_cast<int>(current.width * kTextWidth));
    bottomSize = textSize(bottom);
  } while (bottomSize.y > room && size > current.height * kSmallestShare);
  const float y = (area - topSize.y - gap - bottomSize.y) / 2.0F;
  TTF_DrawRendererText(top, (current.width - topSize.x) / 2.0F, y);
  TTF_DestroyText(top);
  TTF_DrawRendererText(bottom, (current.width - bottomSize.x) / 2.0F, y + topSize.y + gap);
  TTF_DestroyText(bottom);
  if (!below.empty()) {
    hintLine(current, below);
  }
  SDL_RenderPresent(renderer_);
}

} // namespace z3d::app
