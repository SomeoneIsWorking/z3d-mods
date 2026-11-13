// Gameplay_Update's call of Play_Update, with our per-frame features in front of it.
#include "play_update.hpp"

#include "../common/guest.hpp"
#include "symbols.hpp"

namespace z3dmods::oot3d {

using PlayUpdateFn = void (*)(void *play);

// Called instead of Play_Update by Gameplay_Update (hooks.s).
void updatePlay(void *play) {
  updateChordItems(play);
  updateAutosave(play);
  guest::function<PlayUpdateFn>(kPlayUpdate)(play);
}

} // namespace z3dmods::oot3d
