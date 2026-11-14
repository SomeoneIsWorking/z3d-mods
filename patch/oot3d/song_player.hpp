// Plays a song on the ocarina for the player: once the ocarina is out and takes notes, the song's
// notes go into the pad word the game maps to notes, so the game hears and recognises it itself.
#pragma once

#include <stdint.h>

namespace z3dmods::oot3d {

// A song is its place among the quest items' songs, from QUEST_SONG_MINUET.
inline constexpr uint8_t kSongCount = 12;

// Plays `song` when the ocarina next comes out; forgotten if it does not come soon.
void playSong(uint8_t song);

} // namespace z3dmods::oot3d
