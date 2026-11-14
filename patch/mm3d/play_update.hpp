// What runs once per play frame before the game's own update (play_update.cpp).
#pragma once

namespace z3dmods::mm3d {

// Plain play: no cutscene, scene change or text box, the HUD up.
bool inPlainPlay(void *play);

void updateSlotItems(void *play);
void updateAutosave(void *play);

} // namespace z3dmods::mm3d
