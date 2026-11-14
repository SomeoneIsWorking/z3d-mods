// Play_Update's entry, with our per-frame features in front of the game's own update.
#include "play_update.hpp"

#include "../common/guest.hpp"
#include "game.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {

using MessageStateFn = int32_t (*)(void *messages);

bool inPlainPlay(void *play) {
  return guest::read<uint8_t>(play, kPlayCutscene + kCsState) == 0 &&
         guest::read<int8_t>(play, kPlayTransitionTrigger) == 0 &&
         guest::read<uint16_t>(kCommonData + kCommonHudVisibility) == kHudVisibilityAll &&
         guest::function<MessageStateFn>(kMessageGetState)(static_cast<uint8_t *>(play) + kPlayMessageContext) ==
             kMessageStateNone;
}

// Called at the start of Play_Update (hooks.s).
void updatePlay(void *play) {
  updateSlotItems(play);
  updateAutosave(play);
  updateNotebook(play);
  dropUntakenLook();
}

} // namespace z3dmods::mm3d
