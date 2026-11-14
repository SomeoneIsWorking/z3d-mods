#include "song_player.hpp"

#include "../common/guest.hpp"
#include "symbols.hpp"

namespace z3dmods::oot3d {
namespace {

// gOcarinaSongButtons' row of each song: the warp songs are in quest order, the rest are not.
constexpr uint8_t kSongRows[kSongCount] = {0, 1, 2, 3, 4, 5, 8, 7, 6, 9, 10, 11};
// The pad button of each of the five notes, low to high.
constexpr uint32_t kNoteButtons[] = {kButtonL, kButtonR, kButtonY, kButtonX, kButtonA};
// In pad updates. The game drops one update after a note changes, so a note and the gap after it
// are held longer than that; the ocarina takes about a second to come out.
constexpr uint32_t kNoteUpdates = 4;
constexpr uint32_t kGapUpdates = 4;
constexpr uint32_t kLeadUpdates = 8;
constexpr uint32_t kWaitUpdates = 240;
constexpr uint8_t kNoSong = 0xFF;

class SongPlayer {
public:
  void play(uint8_t song) {
    song_ = song < kSongCount ? song : kNoSong;
    waited_ = 0;
    played_ = 0;
  }

  // The note buttons to hold in this pad update.
  uint32_t next() {
    if (song_ == kNoSong) {
      return 0;
    }
    if (guest::read<int32_t>(kOcarinaScreenState) != kOcarinaScreenPlay ||
        guest::read<uint8_t>(kAudioOcarinaInputEnabled) != 1) {
      // Not out yet; or it went away mid-song.
      if (played_ != 0 || ++waited_ > kWaitUpdates) {
        song_ = kNoSong;
      }
      return 0;
    }
    const uint32_t at = played_++;
    if (at < kLeadUpdates) {
      return 0;
    }
    const auto *notes = reinterpret_cast<const uint8_t *>(kOcarinaSongButtons + kSongRows[song_] * kOcarinaSongButtonsSize);
    const uint32_t note = (at - kLeadUpdates) / (kNoteUpdates + kGapUpdates);
    if (note >= notes[0]) {
      song_ = kNoSong;
      return 0;
    }
    const uint8_t button = notes[1 + note];
    const bool sounding = (at - kLeadUpdates) % (kNoteUpdates + kGapUpdates) < kNoteUpdates;
    return sounding && button < sizeof(kNoteButtons) / sizeof(kNoteButtons[0]) ? kNoteButtons[button] : 0;
  }

private:
  uint8_t song_ = kNoSong;
  uint32_t waited_ = 0;
  uint32_t played_ = 0;
};

constinit SongPlayer songPlayer;

} // namespace

void playSong(uint8_t song) {
  songPlayer.play(song);
}

// Called by PadMgr_RequestPadData where it has the pad word its note mapping reads (hooks.s).
uint32_t nextSongNotes() {
  return songPlayer.next();
}

} // namespace z3dmods::oot3d
