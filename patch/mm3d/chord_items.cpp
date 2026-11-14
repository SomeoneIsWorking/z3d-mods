// Chord item buttons: while the chord button is held, the four item buttons (Y, X, I, II) hold a
// second page of items. The page that is away is four inventory slots, kept in the item-slot
// bytes of the equipment's second row, which the game leaves empty.
#include "../common/guest.hpp"
#include "game.hpp"
#include "options.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

using MessageStateFn = int32_t (*)(void *messages);

// The equipment row the item buttons use, and the row whose slot bytes hold the page that is away.
constexpr uint32_t kButtonRow = 0;
constexpr uint32_t kAwayRow = 1;
constexpr uint32_t kRowButtons = 5;

class ChordItems {
public:
  void update(void *play) {
    auto *common = reinterpret_cast<uint8_t *>(kCommonData);
    if (guest::read<int32_t>(common, kGameMode) != kGameModeNormal) {
      // The title screen: the next file loads with its first page on the buttons.
      chordIn_ = false;
      return;
    }
    uint8_t *save = common + kCommonSave;
    const bool wanted = options().chordItems != 0 && settled(play, common) &&
                        guest::read<uint8_t>(save, kSavePlayerForm) == kPlayerFormHuman &&
                        (guest::read<uint32_t>(play, kPlayInputHeld) & kButtonChord) != 0;
    if (chordIn_ == wanted) {
      return;
    }
    trade(save);
    chordIn_ = wanted;
  }

  // The file always holds the first page on the buttons.
  void beforeSave() {
    if (chordIn_) {
      trade(reinterpret_cast<uint8_t *>(kCommonData) + kCommonSave);
      chordIn_ = false;
    }
  }

private:
  // Plain play: no cutscene, scene change or text box, the HUD up.
  static bool settled(void *play, const uint8_t *common) {
    return guest::read<uint8_t>(play, kPlayCutscene + kCsState) == 0 &&
           guest::read<int8_t>(play, kPlayTransitionTrigger) == 0 &&
           guest::read<uint16_t>(common, kCommonHudVisibility) == kHudVisibilityAll &&
           guest::function<MessageStateFn>(kMessageGetState)(static_cast<uint8_t *>(play) + kPlayMessageContext) ==
               kMessageStateNone;
  }

  static void trade(uint8_t *save) {
    auto *equipment = reinterpret_cast<volatile uint8_t *>(save + kSaveEquipment);
    volatile uint8_t *items = equipment + kEquipButtonItems + kButtonRow * kRowButtons;
    volatile uint8_t *slots = equipment + kEquipButtonSlots + kButtonRow * kRowButtons;
    volatile uint8_t *away = equipment + kEquipButtonSlots + kAwayRow * kRowButtons;
    for (uint32_t button = kEquipButtonY; button <= kEquipButtonII; ++button) {
      const uint8_t leaving = slots[button];
      uint8_t arriving = away[button];
      // The slot's item now: a bottle's contents change, and an item can be gone.
      uint8_t item = kItemNone;
      if (arriving < kInventorySlots) {
        item = guest::read<uint8_t>(save, kSaveItems + arriving);
      }
      if (item == kItemNone) {
        arriving = kItemNone;
      }
      away[button] = leaving;
      slots[button] = arriving;
      items[button] = item;
    }
  }

  bool chordIn_ = false;
};

constinit ChordItems chordItems;

} // namespace

// Called once a frame from Actor_UpdateAll (hooks.s).
void updateChordItems(void *play) {
  chordItems.update(play);
}

// Called at the start of Play_SaveAtStatue (hooks.s).
void beforeSave() {
  chordItems.beforeSave();
}

} // namespace z3dmods::mm3d
