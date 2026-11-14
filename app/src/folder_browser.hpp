// The in-window folder chooser: the folders of one place as rows, its files listed under them,
// walked with the pad or the mouse. Up and Down pick a row, A opens it, B goes to the parent,
// X or Start takes the folder shown, Escape gives up.
#pragma once

#include "menu_canvas.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace z3d::app {

class GameWindow;
class PadInput;

class FolderBrowser {
public:
  // `heading` says what the folder is for; `start` is where it opens.
  FolderBrowser(const MenuCanvas &canvas, GameWindow &window, const PadInput &pad,
                std::string heading, const std::filesystem::path &start);

  // The folder taken, or nothing when the player gives up or the window closes.
  [[nodiscard]] std::optional<std::filesystem::path> run();

private:
  struct Layout;
  [[nodiscard]] Layout layout() const;
  // Lists `folder` and logs it, which the input tests wait on.
  void enter(const std::filesystem::path &folder);
  void select(size_t row);
  // What a row reads.
  [[nodiscard]] std::string name(size_t row) const;
  // What A does on a row; the folder when that takes it.
  [[nodiscard]] std::optional<std::filesystem::path> open(size_t row);
  void draw(const Layout &layout);
  void line(const std::string &text, float x, float y, Rgb color) const;
  // `folder` as it fits in `width` at the font's size now, its leading folders left out.
  [[nodiscard]] std::string fitted(const std::filesystem::path &folder, float width) const;
  // `name` as it fits in `width`, its middle left out.
  [[nodiscard]] std::string shortened(const std::string &name, float width) const;

  const MenuCanvas &canvas_;
  GameWindow &window_;
  const PadInput &pad_;
  std::string heading_;
  std::filesystem::path folder_;
  // Rows after the first, which takes the folder: its parent when it has one, then its folders.
  std::vector<std::filesystem::path> rows_;
  std::vector<std::string> files_;
  bool unreadable_ = false;
  size_t selected_ = 0;
  size_t first_ = 0;
};

} // namespace z3d::app
