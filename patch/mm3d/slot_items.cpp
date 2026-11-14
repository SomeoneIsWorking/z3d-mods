// Item slots: the player's eleven slots, each an inventory slot or a song, kept in equipment
// bytes the game leaves alone. The app says which slots are held (slot_link.hpp); a held slot's
// item is put on one of the game's four item buttons and that button pressed for the player; the
// ocarina and a song's slot press the game's ocarina button, and a song is then played on it
// (song_sequence.hpp). The buttons get the game's own items back while the Items or Masks screen
// is up and before a save.
#include "../common/guest.hpp"
#include "../common/slots.hpp"
#include "../common/song_sequence.hpp"
#include "game.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

using MessageStateFn = int32_t (*)(void *messages);

// A slot's value, seven bits: an inventory slot, a song list entry from kFirstSong on, or nothing.
using Slots = SlotTable<7>;
constexpr uint8_t kNoValue = Slots::kNone;
constexpr uint8_t kFirstSong = 0x40;

// One of the game's item buttons the slots use: its place in the equipment's first row, and what
// presses it: a pad button, or one of the touch screen's.
struct Lane {
  uint8_t button;
  uint32_t pad;
  uint8_t touch;
};
// The touch screen's two first: the pad cannot press them, so the game's own X and Y keep theirs longest.
constexpr Lane kLanes[] = {
    {kEquipButtonI, 0, kTouchButtonI},
    {kEquipButtonII, 0, kTouchButtonII},
    {kEquipButtonX, kButtonX, 0},
    {kEquipButtonY, kButtonY, 0},
};
constexpr uint32_t kLaneCount = sizeof(kLanes) / sizeof(kLanes[0]);
constexpr uint32_t kRowButtons = 5;

// The slots by number, as the app's pad table presses them (app/src/pad_input.cpp).
constexpr uint32_t kSlotY = 0;
constexpr uint32_t kSlotB = 1;
constexpr uint32_t kSlotRB = 2;
constexpr uint32_t kSlotUp = 3;
constexpr uint32_t kSlotDown = 5;

// What the patch keeps in the save: a marker, then the slots. Its bytes are the item-slot bytes
// of the equipment's rows 1 to 3 from Y to II, which the game never reads (decomp docs/items.md).
struct Store {
  uint8_t marker;
  uint8_t table[Slots::bytesFor(kSlotCount)];
};
constexpr uint32_t kStoreRows = 3;
constexpr uint32_t kStoreColumns = kRowButtons - 1;
static_assert(sizeof(Store) <= kStoreRows * kStoreColumns);
constexpr uint8_t kStoreMarker = 0x5A;

constexpr uint32_t kNoteButtons[kSongNotes] = {kButtonL, kButtonR, kButtonY, kButtonX, kButtonA};
constexpr uint32_t kNoRow = UINT32_MAX;
constexpr uint32_t kItemGridColumns = 5;
constexpr uint32_t kMaskGridColumns = 6;

class SlotItems {
public:
  void update(void *play) {
    volatile SlotLink &link = slotLink();
    const uint16_t held = link.held;
    const uint16_t pressed = held & static_cast<uint16_t>(~held_);
    held_ = held;
    link.mode = static_cast<uint8_t>(PadMode::ByName);
    auto *common = reinterpret_cast<uint8_t *>(kCommonData);
    if (guest::read<int32_t>(common, kGameMode) != kGameModeNormal) {
      // The title screen: the next file loads with its own items on the buttons.
      lanes_ = {};
      applied_ = 0;
      return;
    }
    uint8_t *save = common + kCommonSave;
    Store store = load(save);
    if (store.marker != kStoreMarker) {
      seed(store, save);
    }
    Slots table(store.table);
    const auto *screen = guest::read<const uint8_t *>(kUiScreenContext + kUiActiveScreen);
    bool editing = false;
    if (screen == screenAt(kUiScreenItems)) {
      editing = true;
      link.mode = static_cast<uint8_t>(PadMode::Assign);
      table.assign(0, kSlotCount, pressed, cellUnderCursor(screen, save, kSaveItemGrid, kItemGridColumns, 0));
    } else if (screen == screenAt(kUiScreenMasks)) {
      editing = true;
      link.mode = static_cast<uint8_t>(PadMode::Assign);
      table.assign(0, kSlotCount, pressed, cellUnderCursor(screen, save, kSaveMaskGrid, kMaskGridColumns, kMaskSlotsFirst));
    } else if (screen == screenAt(kUiScreenOcarina)) {
      if (guest::read<int32_t>(screen, kOcarinaScreenPlayPage) == 0) {
        link.mode = static_cast<uint8_t>(PadMode::Assign);
        table.assign(0, kSlotCount, pressed, songUnderCursor(screen, save));
      }
    } else if (screen == screenAt(kUiScreenMain)) {
      link.mode = static_cast<uint8_t>(PadMode::Play);
    }
    drop(save, table);
    if (editing) {
      // The screens that assign the game's own buttons show and change the game's own items.
      unapply(save);
    } else {
      apply(save, table);
      if (link.mode == static_cast<uint8_t>(PadMode::Play) && settled(play, common)) {
        use(play, save, table, held, pressed);
      }
    }
    playNotes(screen);
    keep(save, store);
    publish(save, table);
  }

  // A saved file has the game's own items on the buttons, so it reads the same without the patch.
  void beforeSave() {
    unapply(reinterpret_cast<uint8_t *>(kCommonData) + kCommonSave);
  }

private:
  static const uint8_t *screenAt(uint32_t index) {
    return guest::read<const uint8_t *>(kUiScreens + index * sizeof(uint32_t));
  }

  static volatile uint8_t *storeByte(uint8_t *save, uint32_t index) {
    return reinterpret_cast<volatile uint8_t *>(save + kSaveEquipment + kEquipButtonSlots +
                                                (1 + index / kStoreColumns) * kRowButtons + 1 + index % kStoreColumns);
  }

  static Store load(uint8_t *save) {
    Store store{};
    auto *bytes = reinterpret_cast<uint8_t *>(&store);
    for (uint32_t index = 0; index < sizeof(Store); ++index) {
      bytes[index] = *storeByte(save, index);
    }
    return store;
  }

  static void keep(uint8_t *save, const Store &store) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&store);
    for (uint32_t index = 0; index < sizeof(Store); ++index) {
      *storeByte(save, index) = bytes[index];
    }
  }

  static volatile uint8_t *buttonItems(uint8_t *save) {
    return reinterpret_cast<volatile uint8_t *>(save + kSaveEquipment + kEquipButtonItems);
  }

  static volatile uint8_t *buttonSlots(uint8_t *save) {
    return reinterpret_cast<volatile uint8_t *>(save + kSaveEquipment + kEquipButtonSlots);
  }

  static uint8_t itemIn(const uint8_t *save, uint8_t inventorySlot) {
    return inventorySlot < kInventorySlots ? guest::read<uint8_t>(save, kSaveItems + inventorySlot)
                                           : static_cast<uint8_t>(kItemNone);
  }

  // A file new to the slots: the items on the game's four buttons, and the ocarina on Down.
  static void seed(Store &store, uint8_t *save) {
    Slots table(store.table);
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
      table.set(slot, kNoValue);
    }
    constexpr uint8_t kFrom[][2] = {
        {kSlotY, kEquipButtonY}, {kSlotB, kEquipButtonX}, {kSlotRB, kEquipButtonI}, {kSlotUp, kEquipButtonII}};
    bool ocarina = false;
    for (const auto &[slot, button] : kFrom) {
      const uint8_t inventorySlot = buttonSlots(save)[button];
      if (inventorySlot < kInventorySlots) {
        table.set(slot, inventorySlot);
        ocarina = ocarina || inventorySlot == kSlotOcarina;
      }
    }
    if (!ocarina) {
      table.set(kSlotDown, kSlotOcarina);
    }
    store.marker = kStoreMarker;
  }

  // The Items or Masks screen: the slot value of the cell under the cursor; none on an empty
  // cell and on the item buttons' column.
  static uint8_t cellUnderCursor(const uint8_t *screen, const uint8_t *save, uint32_t grid, uint32_t columns,
                                 uint32_t firstSlot) {
    const int32_t column = guest::read<int8_t>(screen, kItemScreenCursorColumn);
    const int32_t row = guest::read<int8_t>(screen, kItemScreenCursorRow);
    if (column < 0 || static_cast<uint32_t>(column) >= columns || row < 0) {
      return kNoValue;
    }
    const uint32_t inventorySlot = firstSlot + guest::read<uint8_t>(save, grid + static_cast<uint32_t>(row) * columns +
                                                                              static_cast<uint32_t>(column));
    return inventorySlot < kInventorySlots && itemIn(save, static_cast<uint8_t>(inventorySlot)) != kItemNone
               ? static_cast<uint8_t>(inventorySlot)
               : kNoValue;
  }

  // A song list entry's row of the song table, or kNoRow for one the player cannot play.
  static uint32_t songRow(const uint8_t *save, uint32_t entry) {
    if (entry >= kOcarinaListSongCount) {
      return kNoRow;
    }
    const uint32_t row = guest::read<uint8_t>(kOcarinaListSongs + entry) - kOcarinaListSongBase;
    const uint32_t learned = guest::read<uint32_t>(save, kSaveQuestItems);
    // The two songs after the quest's twelve are the Song of Time played another way; then the
    // Goron Lullaby's intro.
    constexpr uint32_t kSongOfTime = 6;
    uint32_t bit = 0;
    if (row < kQuestSongCount) {
      bit = kQuestSongFirst + row;
    } else if (row < kQuestSongCount + 2) {
      bit = kQuestSongFirst + kSongOfTime;
    } else if (row == kQuestSongCount + 2) {
      bit = kQuestSongLullabyIntro;
    } else {
      return kNoRow;
    }
    return (learned >> bit) & 1U ? row : kNoRow;
  }

  // The ocarina's song list: the slot value of the entry under the cursor; none on one not learned.
  static uint8_t songUnderCursor(const uint8_t *screen, const uint8_t *save) {
    const int32_t entry = guest::read<int32_t>(screen, kOcarinaScreenSongCursor);
    return entry >= 0 && songRow(save, static_cast<uint32_t>(entry)) != kNoRow
               ? static_cast<uint8_t>(kFirstSong + entry)
               : kNoValue;
  }

  // Plain play: no cutscene, scene change or text box, the HUD up.
  static bool settled(void *play, const uint8_t *common) {
    return guest::read<uint8_t>(play, kPlayCutscene + kCsState) == 0 &&
           guest::read<int8_t>(play, kPlayTransitionTrigger) == 0 &&
           guest::read<uint16_t>(common, kCommonHudVisibility) == kHudVisibilityAll &&
           guest::function<MessageStateFn>(kMessageGetState)(static_cast<uint8_t *>(play) + kPlayMessageContext) ==
               kMessageStateNone;
  }

  // Whether a slot value presses the game's ocarina button instead of an item button.
  static bool onOcarina(uint8_t value) {
    return value == kSlotOcarina || (value >= kFirstSong && value != kNoValue);
  }

  // The item a slot value uses now: a bottle's contents change, and an item can be gone.
  static uint8_t itemOf(const uint8_t *save, uint8_t value) {
    if (value == kNoValue) {
      return kItemNone;
    }
    return itemIn(save, value < kFirstSong ? value : static_cast<uint8_t>(kSlotOcarina));
  }

  // The lanes whose slot was assigned away or used up go back to the game.
  void drop(uint8_t *save, const Slots &table) {
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t slot = lanes_.slot(lane);
      if (slot != kSlotEmpty && (onOcarina(table.get(slot)) || itemOf(save, table.get(slot)) == kItemNone)) {
        unapply(save, lane);
        lanes_.drop(lane);
      }
    }
  }

  // Each lane's slot item on its button; the game's own item there is remembered first.
  void apply(uint8_t *save, const Slots &table) {
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t slot = lanes_.slot(lane);
      if (slot == kSlotEmpty) {
        continue;
      }
      const uint8_t button = kLanes[lane].button;
      if (((applied_ >> lane) & 1U) == 0) {
        ownItems_[lane] = buttonItems(save)[button];
        ownSlots_[lane] = buttonSlots(save)[button];
        applied_ |= 1U << lane;
      }
      buttonItems(save)[button] = itemOf(save, table.get(slot));
      buttonSlots(save)[button] = table.get(slot);
    }
  }

  void unapply(uint8_t *save, uint32_t lane) {
    if ((applied_ >> lane) & 1U) {
      buttonItems(save)[kLanes[lane].button] = ownItems_[lane];
      buttonSlots(save)[kLanes[lane].button] = ownSlots_[lane];
      applied_ &= ~(1U << lane);
    }
  }

  void unapply(uint8_t *save) {
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      unapply(save, lane);
    }
  }

  // Each held slot's item on a lane and the lane's button pressed for the player; the ocarina
  // and a song press the ocarina's button.
  void use(void *play, uint8_t *save, const Slots &table, uint16_t held, uint16_t pressed) {
    uint32_t busy = 0;
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      if (lanes_.slot(lane) != kSlotEmpty && ((held >> lanes_.slot(lane)) & 1U)) {
        busy |= 1U << lane;
      }
    }
    uint32_t padPressed = 0;
    uint8_t touchPressed = 0;
    uint8_t touchHeld = 0;
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
      const uint8_t value = table.get(slot);
      if (itemOf(save, value) == kItemNone) {
        continue;
      }
      if (onOcarina(value)) {
        if ((held >> slot) & 1U) {
          touchHeld |= kTouchButtonOcarina;
        }
        if ((pressed >> slot) & 1U) {
          touchPressed |= kTouchButtonOcarina;
          if (value >= kFirstSong) {
            song_.play(reinterpret_cast<const uint8_t *>(
                kOcarinaSongButtons + songRow(save, value - kFirstSong) * kOcarinaSongButtonsSize));
          }
        }
        continue;
      }
      if (((pressed >> slot) & 1U) == 0) {
        continue;
      }
      const uint32_t lane = lanes_.take(static_cast<uint8_t>(slot), busy);
      if (lane != kLaneCount) {
        busy |= 1U << lane;
        padPressed |= kLanes[lane].pad;
        touchPressed |= kLanes[lane].touch;
      }
    }
    apply(save, table);
    uint32_t padHeld = 0;
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      if (lanes_.slot(lane) != kSlotEmpty && ((held >> lanes_.slot(lane)) & 1U)) {
        padHeld |= kLanes[lane].pad;
        touchHeld |= kLanes[lane].touch;
      }
    }
    guest::write<uint32_t>(play, kPlayInputHeld, guest::read<uint32_t>(play, kPlayInputHeld) | padHeld);
    guest::write<uint32_t>(play, kPlayInputPressed, guest::read<uint32_t>(play, kPlayInputPressed) | padPressed);
    auto *touch = reinterpret_cast<volatile uint8_t *>(kTouchButtonsHeld);
    *touch = *touch | touchHeld | touchPressed;
    touch = reinterpret_cast<volatile uint8_t *>(kTouchButtonsPressed);
    *touch = *touch | touchPressed;
  }

  // A song under way: its notes go into the next pad update while the ocarina takes them.
  void playNotes(const uint8_t *screen) {
    const auto *message = guest::read<const uint8_t *>(kMessageObject);
    const bool ready = screen == screenAt(kUiScreenOcarina) && guest::read<int32_t>(screen, kOcarinaScreenPlayPage) != 0 &&
                       message != nullptr &&
                       guest::read<uint32_t>(message, kMessageOcarinaMode) == kMessageOcarinaModeFreePlay;
    if (const uint32_t notes = song_.next(ready, kNoteButtons); notes != 0) {
      auto *injected = reinterpret_cast<volatile uint32_t *>(kPadInjected);
      *injected = *injected | notes;
    }
  }

  // What each slot holds and which button it is on, for the app's HUD.
  void publish(const uint8_t *save, const Slots &table) const {
    volatile SlotLink &link = slotLink();
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
      const uint8_t value = table.get(slot);
      const bool song = value >= kFirstSong && value != kNoValue;
      link.items[slot] = song ? kSlotEmpty : itemOf(save, value);
      link.songs[slot] = song ? static_cast<uint8_t>(value - kFirstSong + 1) : 0;
      link.lanes[slot] = 0;
    }
    for (uint32_t lane = 0; lane < kLaneCount; ++lane) {
      if (lanes_.slot(lane) != kSlotEmpty) {
        link.lanes[lanes_.slot(lane)] = kLanes[lane].button;
      }
    }
  }

  SlotLanes<kLaneCount> lanes_;
  // The game's own item and slot of each lane's button, while a bit of applied_ says the slot's is there.
  uint8_t ownItems_[kLaneCount] = {};
  uint8_t ownSlots_[kLaneCount] = {};
  uint32_t applied_ = 0;
  uint16_t held_ = 0;
  SongSequence song_;
};

constinit SlotItems slotItems;

} // namespace

// Called at the start of Play_Update (hooks.s).
void updateSlotItems(void *play) {
  slotItems.update(play);
}

// Called at the start of Play_SaveAtStatue (hooks.s).
void beforeSave() {
  slotItems.beforeSave();
}

} // namespace z3dmods::mm3d
