#include "folder_browser.hpp"

#include "game_window.hpp"
#include "pad_input.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <lucent/log.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <system_error>
#include <vector>

namespace z3d::app {
namespace {

constexpr const char *kTake = "Use this folder";
constexpr const char *kParent = "..";
// Stands for what is left out of a path or a name too long for its line.
constexpr std::string_view kElided = "...";
constexpr const char *kUnreadable = "This folder cannot be read";
constexpr const char *kOpen = "Open";
constexpr const char *kUp = "Up";
constexpr const char *kUse = "Use this folder";
constexpr const char *kCancel = "Cancel";
// Text heights as shares of the window's height, capped by its width.
constexpr float kHeadingShare = 1.0F / 16.0F;
constexpr float kRowShare = 1.0F / 26.0F;
constexpr float kHeadingWidthShare = 1.0F / 20.0F;
constexpr float kRowWidthShare = 1.0F / 34.0F;
constexpr float kMargin = 0.06F;
constexpr float kRowPitch = 1.7F;
constexpr float kRowsTop = 0.24F;
constexpr float kRowsBottom = 0.86F;
// A held direction steps again after kRepeatDelayMs, then every kRepeatMs.
constexpr uint64_t kRepeatDelayMs = 400;
constexpr uint64_t kRepeatMs = 70;

struct Held {
  bool up = false;
  bool down = false;
  bool open = false;
  bool parent = false;
  bool take = false;

  static Held from(const MenuPad &pad) {
    return {pad.up, pad.down, pad.a, pad.b, pad.x || pad.start};
  }
};

} // namespace

struct FolderBrowser::Layout {
  float width;
  float height;
  float left;
  float rowSize;
  float pitch;
  float top;
  // Rows that fit, the files under them included.
  size_t visible;
};

FolderBrowser::FolderBrowser(const MenuCanvas &canvas, GameWindow &window, const PadInput &pad,
                             std::string heading, const std::filesystem::path &start)
    : canvas_(canvas), window_(window), pad_(pad), heading_(std::move(heading)) {
  enter(start);
}

void FolderBrowser::enter(const std::filesystem::path &folder) {
  folder_ = folder;
  rows_.clear();
  files_.clear();
  if (folder_.has_relative_path()) {
    rows_.push_back(folder_.parent_path());
  }
  std::vector<std::filesystem::path> folders;
  std::error_code error;
  std::filesystem::directory_iterator entries(folder_, error);
  unreadable_ = static_cast<bool>(error);
  for (; !error && entries != std::filesystem::directory_iterator(); entries.increment(error)) {
    const std::string name = entries->path().filename().string();
    if (name.starts_with(".")) {
      continue;
    }
    std::error_code kind;
    if (entries->is_directory(kind)) {
      folders.push_back(entries->path());
    } else {
      files_.push_back(name);
    }
  }
  std::ranges::sort(folders);
  std::ranges::sort(files_);
  rows_.insert(rows_.end(), folders.begin(), folders.end());
  first_ = 0;
  lucent::info("menu", "folders: in {} ({} folders, {} files)", folder_.string(), folders.size(),
               files_.size());
  select(0);
}

void FolderBrowser::select(size_t row) {
  selected_ = row;
  lucent::info("menu", "folders: row {}", name(row));
}

std::string FolderBrowser::name(size_t row) const {
  if (row == 0) {
    return kTake;
  }
  return rows_[row - 1] == folder_.parent_path() ? kParent : rows_[row - 1].filename().string();
}

std::optional<std::filesystem::path> FolderBrowser::open(size_t row) {
  if (row == 0) {
    return folder_;
  }
  const std::filesystem::path from = folder_;
  enter(rows_[row - 1]);
  // Going up lands on the folder just left.
  const auto back = std::ranges::find(rows_, from);
  if (folder_ == from.parent_path() && back != rows_.end()) {
    select(static_cast<size_t>(back - rows_.begin()) + 1);
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> FolderBrowser::run() {
  (void)window_.takeMenuRequest();
  Held before = Held::from(pad_.menu());
  uint64_t repeatAt = 0;
  while (!window_.closeRequested()) {
    const std::vector<SDL_FPoint> clicks = window_.pollMenu();
    if (window_.takeMenuRequest()) {
      break;
    }
    const Layout current = layout();
    const Held now = Held::from(pad_.menu());
    const size_t count = rows_.size() + 1;
    std::optional<size_t> opened;
    for (const SDL_FPoint &click : clicks) {
      const float row = (click.y - current.top) / current.pitch;
      if (row >= 0.0F && first_ + static_cast<size_t>(row) < count &&
          static_cast<size_t>(row) < current.visible) {
        opened = first_ + static_cast<size_t>(row);
      }
    }
    const uint64_t ticks = SDL_GetTicks();
    const bool stepping = now.up != now.down;
    const bool pressed = (now.up && !before.up) || (now.down && !before.down);
    if (stepping && (pressed || ticks >= repeatAt)) {
      repeatAt = ticks + (pressed ? kRepeatDelayMs : kRepeatMs);
      select(now.up ? (selected_ + count - 1) % count : (selected_ + 1) % count);
    }
    if (now.open && !before.open) {
      opened = selected_;
    }
    if (now.parent && !before.parent && folder_.has_relative_path()) {
      opened = 1;
    }
    if (now.take && !before.take) {
      opened = 0;
    }
    before = now;
    if (opened) {
      if (const auto taken = open(*opened)) {
        lucent::info("menu", "folders: took {}", taken->string());
        return taken;
      }
      continue;
    }
    // The selected row stays on screen.
    first_ = std::min(first_, selected_);
    if (current.visible > 0 && selected_ >= first_ + current.visible) {
      first_ = selected_ + 1 - current.visible;
    }
    draw(current);
  }
  lucent::info("menu", "folders: closed");
  return std::nullopt;
}

FolderBrowser::Layout FolderBrowser::layout() const {
  const SDL_FPoint size = canvas_.size();
  Layout result{size.x,
                size.y,
                size.x * kMargin,
                std::min(size.y * kRowShare, size.x * kRowWidthShare),
                0.0F,
                size.y * kRowsTop,
                0};
  result.pitch = result.rowSize * kRowPitch;
  result.visible = static_cast<size_t>(size.y * (kRowsBottom - kRowsTop) / result.pitch);
  return result;
}

std::string FolderBrowser::fitted(const std::filesystem::path &folder, float width) const {
  std::string shown = folder.string();
  // Leading folders go until the rest fits.
  for (size_t cut = shown.find('/', 1); cut != std::string::npos;
       cut = shown.find('/', kElided.size() + 1)) {
    TTF_Text *made = canvas_.text(shown, kMenuDim);
    const float wide = textSize(made).x;
    TTF_DestroyText(made);
    if (wide <= width) {
      break;
    }
    shown = std::string(kElided) + shown.substr(cut);
  }
  return shown;
}

std::string FolderBrowser::shortened(const std::string &name, float width) const {
  const auto wide = [this](std::string_view text) {
    int measured = 0;
    TTF_GetStringSize(canvas_.font(), text.data(), text.size(), &measured, nullptr);
    return static_cast<float>(measured);
  };
  if (wide(name) <= width) {
    return name;
  }
  // Where each character starts, so a cut never splits one.
  std::vector<size_t> starts;
  for (size_t at = 0; at < name.size(); ++at) {
    if ((static_cast<unsigned char>(name[at]) & 0xC0U) != 0x80U) {
      starts.push_back(at);
    }
  }
  // The most characters kept, split evenly around the middle, that still fit.
  size_t fits = 0;
  for (size_t low = 1, high = starts.size() - 1; low <= high;) {
    const size_t kept = (low + high) / 2;
    const size_t head = (kept + 1) / 2;
    const size_t tail = kept - head;
    const std::string shown =
        name.substr(0, starts[head]) + std::string(kElided) +
        (tail == 0 ? std::string() : name.substr(starts[starts.size() - tail]));
    if (wide(shown) <= width) {
      fits = kept;
      low = kept + 1;
    } else {
      high = kept - 1;
    }
  }
  const size_t head = (fits + 1) / 2;
  const size_t tail = fits - head;
  return name.substr(0, starts[head]) + std::string(kElided) +
         (tail == 0 ? std::string() : name.substr(starts[starts.size() - tail]));
}

void FolderBrowser::line(const std::string &text, float x, float y, Rgb color) const {
  TTF_Text *made = canvas_.text(text, color);
  TTF_DrawRendererText(made, x, y);
  TTF_DestroyText(made);
}

void FolderBrowser::draw(const Layout &layout) {
  canvas_.clear();
  TTF_SetFontSize(canvas_.font(),
                  std::min(layout.height * kHeadingShare, layout.width * kHeadingWidthShare));
  line(heading_, layout.left, layout.height * 0.06F, kMenuText);
  TTF_SetFontSize(canvas_.font(), layout.rowSize);
  const float wide = layout.width - 2.0F * layout.left;
  line(fitted(folder_, wide), layout.left, layout.height * 0.16F, kMenuDim);
  const size_t count = rows_.size() + 1;
  float y = layout.top;
  size_t drawn = 0;
  for (size_t row = first_; row < count && drawn < layout.visible;
       ++row, ++drawn, y += layout.pitch) {
    const bool selected = row == selected_;
    if (selected) {
      canvas_.fill(kMenuTile, {layout.left * 0.5F, y - layout.pitch * 0.15F,
                               layout.width - layout.left, layout.pitch});
    }
    line(shortened(name(row), wide), layout.left, y, selected ? kMenuSelected : kMenuText);
  }
  if (unreadable_ && drawn < layout.visible) {
    line(kUnreadable, layout.left, y, kMenuDim);
  }
  // The files show what the folder holds; they are not rows.
  for (size_t file = 0; file < files_.size() && drawn < layout.visible;
       ++file, ++drawn, y += layout.pitch) {
    const bool last = drawn + 1 == layout.visible && file + 1 < files_.size();
    line(last ? "and " + std::to_string(files_.size() - file) + " more files"
              : shortened(files_[file], wide),
         layout.left, y, kMenuDim);
  }
  const std::array hints{Hint{{PadInput::menuPrompt(&MenuPad::a)}, kOpen}, Hint{{PadInput::menuPrompt(&MenuPad::b)}, kUp},
                         Hint{{PadInput::menuPrompt(&MenuPad::x)}, kUse},
                         Hint{{GameWindow::menuPrompt()}, kCancel}};
  canvas_.hints(hints, pad_.lastDevice(), {layout.left, layout.height * 0.9F}, layout.rowSize, wide, false);
  SDL_RenderPresent(canvas_.renderer());
}

} // namespace z3d::app
