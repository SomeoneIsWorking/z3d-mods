#include "song_player.hpp"

#include "../common/guest.hpp"
#include "../common/song_sequence.hpp"
#include "symbols.hpp"

namespace z3dmods::oot3d {
namespace {

// gOcarinaSongButtons' row of each song: the warp songs are in quest order, the rest are not.
constexpr uint8_t kSongRows[kSongCount] = {0, 1, 2, 3, 4, 5, 8, 7, 6, 9, 10, 11};
constexpr uint32_t kNoteButtons[kSongNotes] = {kButtonL, kButtonR, kButtonY, kButtonX, kButtonA};

constinit SongSequence sequence;

} // namespace

void playSong(uint8_t song) {
  if (song < kSongCount) {
    sequence.play(reinterpret_cast<const uint8_t *>(kOcarinaSongButtons + kSongRows[song] * kOcarinaSongButtonsSize));
  }
}

// Called by PadMgr_RequestPadData where it has the pad word its note mapping reads (hooks.s).
uint32_t nextSongNotes() {
  const bool ready = guest::read<int32_t>(kOcarinaScreenState) == kOcarinaScreenPlay &&
                     guest::read<uint8_t>(kAudioOcarinaInputEnabled) == 1;
  return sequence.next(ready, kNoteButtons);
}

} // namespace z3dmods::oot3d
