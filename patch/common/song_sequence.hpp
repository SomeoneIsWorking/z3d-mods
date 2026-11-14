// A song played on the ocarina for the player, shared by both games' patches: once the ocarina is
// out and takes notes, the song's notes are held one after another in the pad word the game maps
// to notes, so the game hears and recognises the song itself.
#pragma once

#include <stdint.h>

namespace z3dmods {

// The ocarina's five notes, low to high: both games number them so in their song tables.
inline constexpr uint32_t kSongNotes = 5;

class SongSequence {
public:
  // Plays the song whose notes are `row` (a count, then that many note numbers) when the ocarina
  // next comes out; forgotten if it does not come soon.
  void play(const uint8_t *row) {
    row_ = row;
    waited_ = 0;
    played_ = 0;
  }

  // Once a pad update: the pad buttons to hold in it, from `buttons` by note number. `ready` is
  // whether the ocarina is out and takes notes.
  uint32_t next(bool ready, const uint32_t (&buttons)[kSongNotes]) {
    if (row_ == nullptr) {
      return 0;
    }
    if (!ready) {
      // Not out yet; or it went away mid-song.
      if (played_ != 0 || ++waited_ > kWaitUpdates) {
        row_ = nullptr;
      }
      return 0;
    }
    const uint32_t at = played_++;
    if (at < kLeadUpdates) {
      return 0;
    }
    const uint32_t note = (at - kLeadUpdates) / (kNoteUpdates + kGapUpdates);
    if (note >= row_[0]) {
      row_ = nullptr;
      return 0;
    }
    const uint8_t number = row_[1 + note];
    const bool sounding = (at - kLeadUpdates) % (kNoteUpdates + kGapUpdates) < kNoteUpdates;
    return sounding && number < kSongNotes ? buttons[number] : 0;
  }

private:
  // In pad updates. The games drop one update after a note changes, so a note and the gap after
  // it are held longer than that; the ocarina takes about a second to come out.
  static constexpr uint32_t kNoteUpdates = 4;
  static constexpr uint32_t kGapUpdates = 4;
  static constexpr uint32_t kLeadUpdates = 8;
  static constexpr uint32_t kWaitUpdates = 240;

  const uint8_t *row_ = nullptr;
  uint32_t waited_ = 0;
  uint32_t played_ = 0;
};

} // namespace z3dmods
