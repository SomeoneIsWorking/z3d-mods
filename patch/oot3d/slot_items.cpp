// Item slots: the player's eleven slots, each an inventory slot or a song kept in the save's
// unused bytes per age. The app says which slots are held (slot_link.hpp); a held slot's item is
// put on one of the game's X and Y item buttons and that button pressed in the play input; a
// song's slot brings the ocarina out that way and plays the song on it (song_player.hpp). The item
// screen's layout, which owns those buttons, is never changed: a save and the other age get the
// layout's own items back.
#include "../common/guest.hpp"
#include "../common/slots.hpp"
#include "game.hpp"
#include "play_update.hpp"
#include "song_player.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

// One of the game's item buttons the slots use: its node in the item screen's layout, its place
// in equips and its pad bit.
struct Lane {
  uint8_t node;
  uint8_t equipsItem;
  uint8_t equipsSlot;
  uint32_t button;
};
constexpr Lane kLanes[] = {
    {kItemMenuNodeX, 2, 1, kButtonX},
    {kItemMenuNodeY, 1, 0, kButtonY},
};
constexpr uint32_t kLaneCount = sizeof(kLanes) / sizeof(kLanes[0]);

// A slot's value, six bits: an inventory slot, a song from kFirstSong on, or nothing.
using Slots = SlotTable<6>;
constexpr uint8_t kNoValue = Slots::kNone;
constexpr uint8_t kFirstSong = 0x20;

// The slots by number, as the app's pad table presses them (app/src/pad_input.cpp).
constexpr uint32_t kSlotY = 0;
constexpr uint32_t kSlotB = 1;
constexpr uint32_t kSlotRB = 2;
constexpr uint32_t kSlotUp = 3;
constexpr uint32_t kSlotDown = 5;

// What the patch keeps in the save's unused bytes: the child's slots, then the adult's.
struct Store {
  uint8_t marker;
  uint8_t table[Slots::bytesFor(2 * kSlotCount)];
};
static_assert(sizeof(Store) <= kSaveUnusedSize);
constexpr uint8_t kStoreMarker = 0x5A;

class SlotItems {
public:
  void update(void *play) {
    volatile SlotLink &link = slotLink();
    const uint16_t held = link.held;
    const uint16_t pressed = held & static_cast<uint16_t>(~held_);
    held_ = held;
    link.beat = static_cast<uint8_t>(link.beat + 1);
    link.mode = static_cast<uint8_t>(PadMode::ByName);
    if (guest::read<int32_t>(kSaveContext + kSaveGameMode) != kGameModeNormal ||
        guest::read<int32_t>(kSaveContext + kSaveFileNum) == kFileNumDebug) {
      // The title screen and file select: the next file starts with the game's own buttons.
      lanes_ = {};
      waiting_ = 0;
      return;
    }
    auto *store = reinterpret_cast<volatile Store *>(kSaveContext + kSaveUnused);
    if (store->marker != kStoreMarker) {
      seed(store);
    }
    const bool child = guest::read<int32_t>(kSaveContext + kSaveLinkAge) == kLinkAgeChild;
    if (lanes_.any() && child != lanesChild_) {
      // The age changed with slots on the lanes: the game put them away as the other age's.
      restore(lanesChild_ ? kSaveChildEquips : kSaveAdultEquips, lanesChild_);
    }
    Slots table(store->table);
    const uint32_t first = child ? 0 : kSlotCount;
    if (guest::read<int32_t>(kItemMenuState) != kMenuScreenClosed) {
      link.mode = static_cast<uint8_t>(PadMode::Assign);
      table.assign(first, kSlotCount, pressed, itemUnderCursor(child));
    } else if (guest::read<int32_t>(kOcarinaScreenState) == kOcarinaScreenSongList) {
      link.mode = static_cast<uint8_t>(PadMode::Assign);
      table.assign(first, kSlotCount, pressed, songOnShow());
    } else if (guest::read<uint32_t>(kHud + kHudState) == kHudStateGameplay) {
      link.mode = static_cast<uint8_t>(PadMode::Play);
      if (guest::read<uint8_t>(play, kPlayCutscene + kCsState) != 0) {
        // A cutscene can change Link's age.
        restore(kSaveEquips, child);
      } else if (guest::read<int8_t>(play, kPlayTransitionTrigger) == 0) {
        use(play, table, first, held, pressed, child);
      }
    }
    publish(table, first);
  }

  // A saved file has the layout's own items on the buttons, so it reads the same without the patch.
  void beforeSave() {
    restore(kSaveEquips, guest::read<int32_t>(kSaveContext + kSaveLinkAge) == kLinkAgeChild);
  }

private:
  static volatile uint8_t *layout(bool child) {
    return reinterpret_cast<volatile uint8_t *>(kSaveContext +
                                                (child ? kSaveItemMenuChild : kSaveItemMenuAdult));
  }

  static uint8_t itemIn(uint8_t inventorySlot) {
    return guest::read<uint8_t>(kSaveContext + kSaveInventory + kInventoryItems + inventorySlot);
  }

  // A file new to the slots: the items on the game's four buttons, and the ocarina on Down.
  static void seed(volatile Store *store) {
    Slots table(store->table);
    for (uint32_t index = 0; index < 2 * kSlotCount; ++index) {
      table.set(index, kNoValue);
    }
    for (uint32_t age = 0; age < 2; ++age) {
      const uint32_t first = age * kSlotCount;
      const volatile uint8_t *nodes = layout(age == 0);
      constexpr uint8_t kFrom[][2] = {{kSlotY, kItemMenuNodeY},
                                      {kSlotB, kItemMenuNodeX},
                                      {kSlotRB, kItemMenuNodeI},
                                      {kSlotUp, kItemMenuNodeII}};
      for (const auto &[slot, node] : kFrom) {
        if (nodes[node] < kFirstSong) {
          table.set(first + slot, nodes[node]);
        }
      }
      bool ocarina = false;
      for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
        ocarina = ocarina || table.get(first + slot) == kSlotOcarina;
      }
      if (!ocarina) {
        table.set(first + kSlotDown, kSlotOcarina);
      }
    }
    store->marker = kStoreMarker;
  }

  // The item screen: the slot value of the item under the cursor; none on an empty cell.
  static uint8_t itemUnderCursor(bool child) {
    const int32_t node = guest::read<int32_t>(kItemMenuCursorNode);
    if (node < 0 || node >= kItemMenuNodes) {
      return kNoValue;
    }
    const uint8_t inventorySlot = layout(child)[node];
    return inventorySlot < kFirstSong && itemIn(inventorySlot) != kItemNone ? inventorySlot
                                                                            : kNoValue;
  }

  // The ocarina's song list: the slot value of the song on show; none on one not learned.
  static uint8_t songOnShow() {
    const int32_t entry = guest::read<int32_t>(kOcarinaSongListShown);
    if (entry < 0 || entry >= kSongCount) {
      return kNoValue;
    }
    const uint32_t item = guest::read<uint32_t>(kOcarinaSongListItems +
                                                static_cast<uint32_t>(entry) * sizeof(uint32_t));
    if (item < kItemSongMinuet || item > kItemSongStorms) {
      return kNoValue;
    }
    const uint32_t song = item - kItemSongMinuet;
    const uint32_t learned =
        guest::read<uint32_t>(kSaveContext + kSaveInventory + kInventoryQuestItems);
    return (learned >> (kQuestSongMinuet + song)) & 1U ? static_cast<uint8_t>(kFirstSong + song)
                                                       : kNoValue;
  }

  // Plain play: each held slot's item on a lane, and the lane's button in the play input.
  void use(void *play, const Slots &table, uint32_t first, uint16_t held, uint16_t pressed,
           bool child) {
    uint32_t busy = 0;
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t slot = lanes_.slot(lane);
      if (slot == kSlotEmpty) {
        continue;
      }
      if (itemOf(table, first + slot) == kItemNone) {
        // Assigned away or used up.
        restore(kSaveEquips, child, lane);
        lanes_.drop(lane);
      } else if ((held >> slot) & 1U) {
        busy |= 1U << lane;
      }
    }
    uint32_t buttonsPressed = 0;
    if (waiting_ != 0 && handsEmpty(play)) {
      buttonsPressed = waiting_;
      waiting_ = 0;
    }
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
      if (((pressed >> slot) & 1U) == 0 || itemOf(table, first + slot) == kItemNone) {
        continue;
      }
      const uint32_t lane = lanes_.take(static_cast<uint8_t>(slot), busy);
      if (lane != kLaneCount) {
        busy |= 1U << lane;
        if (inHandFrom(play, lane, itemOf(table, first + slot))) {
          // The game puts the lane's last item away first and drops a press until it has.
          waiting_ |= kLanes[lane].button;
        } else {
          buttonsPressed |= kLanes[lane].button;
        }
        if (const uint8_t value = table.get(first + slot); value >= kFirstSong) {
          playSong(static_cast<uint8_t>(value - kFirstSong));
        }
      }
    }
    uint32_t buttonsHeld = 0;
    auto *equips = reinterpret_cast<volatile uint8_t *>(kSaveContext + kSaveEquips);
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t slot = lanes_.slot(lane);
      if (slot == kSlotEmpty) {
        continue;
      }
      equips[kEquipsItems + kLanes[lane].equipsItem] = itemOf(table, first + slot);
      equips[kEquipsSlots + kLanes[lane].equipsSlot] = inventorySlotOf(table.get(first + slot));
      if ((held >> slot) & 1U) {
        buttonsHeld |= kLanes[lane].button;
      }
    }
    lanesChild_ = child;
    guest::write<uint32_t>(play, kPlayInputHeld,
                           guest::read<uint32_t>(play, kPlayInputHeld) | buttonsHeld);
    guest::write<uint32_t>(play, kPlayInputPressed,
                           guest::read<uint32_t>(play, kPlayInputPressed) | buttonsPressed);
  }

  static uintptr_t playerOf(void *play) {
    return guest::read<uintptr_t>(play, kPlayActorCtx + kActorCtxLists +
                                            kActorCatPlayer * kActorListEntrySize + kActorListHead);
  }

  // The item in Link's hand; none without one.
  static uint8_t itemInHand(void *play) {
    const uintptr_t player = playerOf(play);
    return player != 0 ? guest::read<uint8_t>(player + kPlayerHeldItemId)
                       : static_cast<uint8_t>(kItemNone);
  }

  // Whether Link's hands are empty and done putting an item away.
  static bool handsEmpty(void *play) {
    const uintptr_t player = playerOf(play);
    return player == 0 ||
           (guest::read<uint8_t>(player + kPlayerHeldItemId) == kItemNone &&
            guest::read<int8_t>(player + kPlayerItemAction) == kPlayerItemActionNone);
  }

  // Whether `item` on this lane takes the item in Link's hand off its last button.
  static bool inHandFrom(void *play, uint32_t lane, uint8_t item) {
    const uint8_t inHand = itemInHand(play);
    if (inHand == kItemNone || inHand == item) {
      return false;
    }
    const auto *items =
        reinterpret_cast<const volatile uint8_t *>(kSaveContext + kSaveEquips + kEquipsItems);
    bool here = false;
    bool elsewhere = false;
    for (uint32_t button = 0; button < kEquipsSlots - kEquipsItems; ++button) {
      if (items[button] == inHand) {
        here = here || button == kLanes[lane].equipsItem;
        elsewhere = elsewhere || button != kLanes[lane].equipsItem;
      }
    }
    return here && !elsewhere;
  }

  // The inventory slot a slot value uses: its own, or the ocarina's for a song.
  static uint8_t inventorySlotOf(uint8_t value) {
    if (value == kNoValue) {
      return kSlotNone;
    }
    return value < kFirstSong ? value : static_cast<uint8_t>(kSlotOcarina);
  }

  // The item a slot value uses now: a bottle's contents change, and an item can be gone.
  static uint8_t itemOf(const Slots &table, uint32_t index) {
    const uint8_t inventorySlot = inventorySlotOf(table.get(index));
    return inventorySlot != kSlotNone ? itemIn(inventorySlot) : static_cast<uint8_t>(kItemNone);
  }

  // Gives one lane of an age's equips what that age's layout has on its button.
  static void restore(uint32_t equipsOffset, bool child, uint32_t lane) {
    auto *equips = reinterpret_cast<volatile uint8_t *>(kSaveContext + equipsOffset);
    const uint8_t inventorySlot = layout(child)[kLanes[lane].node];
    equips[kEquipsItems + kLanes[lane].equipsItem] =
        inventorySlot == kSlotNone ? static_cast<uint8_t>(kItemNone) : itemIn(inventorySlot);
    equips[kEquipsSlots + kLanes[lane].equipsSlot] = inventorySlot;
  }

  // Every lane a slot is on, and the lanes forget their slots.
  void restore(uint32_t equipsOffset, bool child) {
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      if (lanes_.slot(lane) != kSlotEmpty) {
        restore(equipsOffset, child, lane);
        lanes_.drop(lane);
      }
    }
  }

  // What each slot holds and which lane it is on, for the app's HUD.
  void publish(const Slots &table, uint32_t first) const {
    volatile SlotLink &link = slotLink();
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
      const uint8_t value = table.get(first + slot);
      const bool song = value >= kFirstSong && value != kNoValue;
      link.items[slot] = song ? kSlotEmpty : itemOf(table, first + slot);
      link.songs[slot] = song ? static_cast<uint8_t>(value - kFirstSong + 1) : 0;
      link.lanes[slot] = 0;
    }
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      if (lanes_.slot(lane) != kSlotEmpty) {
        link.lanes[lanes_.slot(lane)] = kLanes[lane].equipsItem;
      }
    }
  }

  SlotLanes<kLaneCount> lanes_;
  // The age whose equips the lanes were written into.
  bool lanesChild_ = false;
  uint16_t held_ = 0;
  // Lane buttons to press in the next update.
  uint32_t waiting_ = 0;
};

constinit SlotItems slotItems;

} // namespace

// Called at the start of Sram_WriteSave (hooks.s).
void beforeSave() {
  slotItems.beforeSave();
}

void updateSlotItems(void *play) {
  slotItems.update(play);
}

} // namespace z3dmods::oot3d
