// Autosave: once a scene change has finished, save through the game's own routine, the one the
// pause screen's Save runs. Off unless the option is on.
#include "../common/guest.hpp"
#include "game.hpp"
#include "options.hpp"
#include "play_update.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using WriteSaveFn = void (*)(void *play, int32_t fileSelect);
using IsWriteDoneFn = int32_t (*)();
using FinishWriteFn = void (*)();

class Autosave {
public:
  void update(void *play) {
    // The caller of Sram_WriteSave owns the queued write and frees it once it is done.
    if (writing_ && guest::function<IsWriteDoneFn>(kSramIsWriteDone)() != 0) {
      guest::function<FinishWriteFn>(kSramFinishWrite)();
      writing_ = false;
    }
    if (options().autosave == 0 || guest::read<int32_t>(kSaveContext + kSaveGameMode) != kGameModeNormal ||
        guest::read<int32_t>(kSaveContext + kSaveFileNum) == kFileNumDebug) {
      due_ = false;
      return;
    }
    const auto transition = guest::read<int8_t>(play, kPlayTransitionTrigger);
    if (transition == kTransitionStart) {
      due_ = true;
      return;
    }
    // Wait for the fade in, a cutscene and any screen over the HUD to end: plain play, as when
    // the player saves from the pause screen.
    if (!due_ || writing_ || transition != 0 || guest::read<uint8_t>(play, kPlayCutscene + kCsState) != 0 ||
        guest::read<uint32_t>(kHud + kHudState) != kHudStateGameplay) {
      return;
    }
    due_ = false;
    guest::function<WriteSaveFn>(kSramWriteSave)(play, 0);
    writing_ = true;
  }

private:
  // A scene change started in play and no save has followed it yet.
  bool due_ = false;
  // Our write is queued and not yet finished.
  bool writing_ = false;
};

constinit Autosave autosave;

} // namespace

void updateAutosave(void *play) {
  autosave.update(play);
}

} // namespace z3dmods::oot3d
