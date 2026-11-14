// What runs once per play frame before the game's own update (play_update.cpp).
#pragma once

namespace z3dmods::oot3d {

void updateSlotItems(void *play);
void updateAutosave(void *play);

} // namespace z3dmods::oot3d
