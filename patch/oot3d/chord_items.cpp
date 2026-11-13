// Chord item buttons: while the chord button is held, the four item buttons (Y, X, I, II) hold a
// second page of items. The item screen's layout owns the buttons, so the pages trade places
// there: the page that is away waits in the screen's grid, where the game keeps unassigned items,
// and the save's unused bytes only remember which grid items belong to it.
#include "../common/guest.hpp"
#include "game.hpp"
#include "options.hpp"
#include "play_update.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using SetItemButtonFn = void (*)(uint32_t button, uint32_t item);

constexpr uint32_t kItemButtons = 4;

// One item button: its node in the item screen's layout, its place in equips, its HUD index.
struct Lane {
  uint8_t node;
  uint8_t equipsItem;
  uint8_t equipsSlot;
  uint8_t hudButton;
};
constexpr Lane kLanes[kItemButtons] = {
    {kItemMenuNodeY, 1, 0, 4},
    {kItemMenuNodeX, 2, 1, 3},
    {kItemMenuNodeI, 3, 2, 1},
    {kItemMenuNodeII, 4, 3, 2},
};

// The page of one age that is away: each lane's inventory slot and the grid node it waits in.
struct Page {
  uint8_t slots[kItemButtons];
  uint8_t nodes[kItemButtons];
};

// What the patch keeps in the save's unused bytes.
struct Store {
  uint8_t state;
  Page child;
  Page adult;
};
static_assert(sizeof(Store) <= kSaveUnusedSize);

// Store::state: a marker, and whether the away page is the main one (the chord page is on the buttons).
constexpr uint8_t kStoreMarker = 0xC4;
constexpr uint8_t kStoreChordIn = 0x01;

constexpr bool isButtonNode(uint32_t node) {
  return node == kItemMenuNodeY || node == kItemMenuNodeX || node == kItemMenuNodeI || node == kItemMenuNodeII;
}

class ChordItems {
public:
  void update(void *play) {
    if (guest::read<int32_t>(kSaveContext + kSaveGameMode) != kGameModeNormal ||
        guest::read<int32_t>(kSaveContext + kSaveFileNum) == kFileNumDebug) {
      return;
    }
    auto *store = reinterpret_cast<volatile Store *>(kSaveContext + kSaveUnused);
    if ((store->state & ~kStoreChordIn) != kStoreMarker) {
      clear(store);
    }
    const bool in = (store->state & kStoreChordIn) != 0;
    const bool calm = settled(play);
    const bool wanted = options().chordItems != 0 && calm &&
                        (guest::read<uint32_t>(play, kPlayInputHeld) & kButtonChord) != 0;
    // A cutscene or scene change takes the main page back at once; otherwise wait for the HUD.
    if (in == wanted || (calm && guest::read<uint32_t>(kHud + kHudState) != kHudStateGameplay)) {
      return;
    }
    trade(store);
    store->state = static_cast<uint8_t>(kStoreMarker | (wanted ? kStoreChordIn : 0));
  }

  // A saved file always has the first page on the buttons, so it reads the same without the patch.
  void beforeSave() {
    auto *store = reinterpret_cast<volatile Store *>(kSaveContext + kSaveUnused);
    if (store->state == (kStoreMarker | kStoreChordIn)) {
      trade(store);
      store->state = kStoreMarker;
    }
  }

private:
  static void clear(volatile Store *store) {
    clear(&store->child);
    clear(&store->adult);
    store->state = kStoreMarker;
  }

  static void clear(volatile Page *page) {
    for (uint32_t i = 0; i < kItemButtons; ++i) {
      page->slots[i] = kSlotNone;
      page->nodes[i] = 0;
    }
  }

  // No cutscene and no scene change: those can change Link's age, and with it the layout in use.
  static bool settled(void *play) {
    return guest::read<uint8_t>(play, kPlayCutscene + kCsState) == 0 &&
           guest::read<int8_t>(play, kPlayTransitionTrigger) == 0;
  }

  // The grid node holding `slot`, trying `first`; kItemMenuNodes when the grid does not hold it.
  static uint32_t gridNode(const volatile uint8_t *layout, uint8_t slot, uint32_t first) {
    if (first < kItemMenuNodes && !isButtonNode(first) && layout[first] == slot) {
      return first;
    }
    for (uint32_t node = 0; node < kItemMenuNodes; ++node) {
      if (!isButtonNode(node) && layout[node] == slot) {
        return node;
      }
    }
    return kItemMenuNodes;
  }

  static void trade(volatile Store *store) {
    const bool child = guest::read<int32_t>(kSaveContext + kSaveLinkAge) == kLinkAgeChild;
    volatile Page *page = child ? &store->child : &store->adult;
    auto *layout = reinterpret_cast<volatile uint8_t *>(kSaveContext + (child ? kSaveItemMenuChild : kSaveItemMenuAdult));
    auto *equips = reinterpret_cast<volatile uint8_t *>(kSaveContext + kSaveEquips);
    // First every arriving item comes off the grid, then the leaving ones go onto it: an item
    // the player put on both pages stays where it was put last.
    uint8_t arriving[kItemButtons];
    uint32_t hole[kItemButtons];
    for (uint32_t i = 0; i < kItemButtons; ++i) {
      arriving[i] = page->slots[i];
      hole[i] = arriving[i] == kSlotNone ? kItemMenuNodes : gridNode(layout, arriving[i], page->nodes[i]);
      if (hole[i] == kItemMenuNodes) {
        // The player has since moved it elsewhere.
        arriving[i] = kSlotNone;
      } else {
        layout[hole[i]] = kSlotNone;
      }
    }
    for (uint32_t i = 0; i < kItemButtons; ++i) {
      const Lane &lane = kLanes[i];
      const uint8_t leaving = layout[lane.node];
      if (leaving != kSlotNone) {
        const uint32_t home = hole[i] != kItemMenuNodes ? hole[i] : gridNode(layout, kSlotNone, kItemMenuNodes);
        if (home == kItemMenuNodes) {
          // A full grid has no place for the button's item: this button keeps it on both pages.
          continue;
        }
        layout[home] = leaving;
        hole[i] = home;
      }
      page->slots[i] = leaving;
      page->nodes[i] = static_cast<uint8_t>(hole[i]);
      layout[lane.node] = arriving[i];
      const uint8_t item = arriving[i] == kSlotNone
                               ? static_cast<uint8_t>(kItemNone)
                               : guest::read<uint8_t>(kSaveContext + kSaveInventory + kInventoryItems + arriving[i]);
      equips[kEquipsItems + lane.equipsItem] = item;
      equips[kEquipsSlots + lane.equipsSlot] = arriving[i];
      guest::function<SetItemButtonFn>(kHudSetItemButton)(lane.hudButton, item);
    }
  }
};

constinit ChordItems chordItems;

} // namespace

// Called at the start of Sram_WriteSave (hooks.s).
void beforeSave() {
  chordItems.beforeSave();
}

void updateChordItems(void *play) {
  chordItems.update(play);
}

} // namespace z3dmods::oot3d
