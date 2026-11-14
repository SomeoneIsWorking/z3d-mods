// The game's own A button on the top screen is hidden: the app's HUD draws the A button and its
// word (hud.toml). The widget stays hidden once its flags are written (decomp docs/hud.md).
#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {

// Called once a frame from Actor_UpdateAll (hooks.s).
void hideBaseHud() {
  auto *hud = guest::read<uint8_t *>(kUiCommonLayouts + kUiLayoutHud * sizeof(uint32_t));
  if (hud == nullptr) {
    return;
  }
  auto *button = guest::read<uint8_t *>(hud, kPlayHudAActionButton);
  if (button == nullptr || guest::read<uint32_t>(button, kUiWidgetFlags) == kUiWidgetFlagsHidden) {
    return;
  }
  guest::write<uint32_t>(button, kUiWidgetFlags, kUiWidgetFlagsHidden);
  guest::write<uint32_t>(button, kUiWidgetActiveFlags, kUiWidgetActiveFlagsHidden);
}

} // namespace z3dmods::mm3d
