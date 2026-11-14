// The Bombers' Notebook from the Map screen: a press of View there (Select by name, as the app
// reads the pad outside play) changes to the notebook, the screen Start opens from play, once the
// notebook is owned (decomp docs/ui.md).
#include "../common/guest.hpp"
#include "game.hpp"
#include "play_update.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

uintptr_t screenAt(uint32_t index) {
  return guest::read<uintptr_t>(kUiScreens + index * sizeof(uint32_t));
}

} // namespace

void updateNotebook(void *play) {
  auto *context = reinterpret_cast<uint8_t *>(kUiScreenContext);
  const uint8_t *save = reinterpret_cast<uint8_t *>(kCommonData) + kCommonSave;
  if (guest::read<uintptr_t>(context, kUiActiveScreen) != screenAt(kUiScreenMap) ||
      guest::read<uintptr_t>(context, kUiNextScreen) != 0 ||
      (guest::read<uint32_t>(play, kPlayInputPressed) & kButtonSelect) == 0 ||
      (guest::read<uint32_t>(save, kSaveQuestItems) & (1U << kQuestBombersNotebook)) == 0) {
    return;
  }
  guest::write<uintptr_t>(context, kUiNextScreen, screenAt(kUiScreenSchedule));
}

} // namespace z3dmods::mm3d
