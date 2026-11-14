// What runs once per play frame before the game's own update (play_update.cpp).
#pragma once

namespace z3dmods::mm3d {

// Plain play: no cutscene, scene change or text box, the HUD up.
bool inPlainPlay(void *play);

void updateSlotItems(void *play);
void updateAutosave(void *play);
void updateNotebook(void *play);
// The mouse's turn the camera did not take is dropped (mouse_camera.cpp).
void dropUntakenLook();

} // namespace z3dmods::mm3d
