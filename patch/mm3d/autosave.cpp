// Autosave: once a scene change has finished, save as a statue does, at the scene's entrance and
// the clock's time. Off unless the option is on.
#include "../common/guest.hpp"
#include "game.hpp"
#include "options.hpp"
#include "play_update.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

using SaveAtStatueFn = void (*)(void *play, uint16_t entrance);
using StartWriteFn = void (*)(void *writer);
using UpdateWriteFn = int32_t (*)(void *writer);

// The final hours: a file saved there could leave no time to play the Song of Time.
bool finalHours(const uint8_t *save) {
  const auto day = guest::read<int32_t>(save, kSaveDay);
  return day > kFinalDay || (day == kFinalDay && guest::read<uint16_t>(save, kSaveTime) < kClockTimeDawn);
}

class Autosave {
public:
  void update(void *play) {
    auto *writer = static_cast<uint8_t *>(play) + kPlaySaveWriter;
    // SaveFile_UpdateWrite frees the write's job once it is done, as a statue's wait does.
    const bool writing = guest::read<int16_t>(writer, kSaveWriterState) != kSaveWriterIdle;
    if (writing) {
      guest::function<UpdateWriteFn>(kSaveUpdateWrite)(writer);
    }
    const auto *common = reinterpret_cast<const uint8_t *>(kCommonData);
    if (options().autosave == 0 || guest::read<int32_t>(common, kGameMode) != kGameModeNormal ||
        finalHours(common + kCommonSave)) {
      due_ = false;
      return;
    }
    if (guest::read<int8_t>(play, kPlayTransitionTrigger) == kTransitionStart) {
      due_ = true;
      return;
    }
    if (!due_ || writing || !inPlainPlay(play)) {
      return;
    }
    due_ = false;
    const auto entrance = guest::read<uint32_t>(common, kCommonEntrance) & ~kEntranceLayerMask;
    guest::function<SaveAtStatueFn>(kPlaySaveAtStatue)(play, static_cast<uint16_t>(entrance));
    guest::function<StartWriteFn>(kSaveStartWrite)(writer);
  }

private:
  // A scene change started in play and no save has followed it yet.
  bool due_ = false;
};

constinit Autosave autosave;

} // namespace

void updateAutosave(void *play) {
  autosave.update(play);
}

} // namespace z3dmods::mm3d
