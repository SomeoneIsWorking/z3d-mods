.syntax unified
.arm
@ Cutscene update: the catch-up loop's interpreter call.
.section .patch.interpreter_catchup, "ax", %progbits
    bl _ZN7z3dmods5oot3d12stepCutsceneEPvS1_PKv
.section .expect.interpreter_catchup, "", %progbits
    .word 0xEBFE8ED6 @ bl 0x2C5BA0
@ Cutscene update: the per-frame tail call into the interpreter.
.section .patch.interpreter_tail, "ax", %progbits
    b _ZN7z3dmods5oot3d12stepCutsceneEPvS1_PKv
.section .expect.interpreter_tail, "", %progbits
    .word 0xEAFE8ECA @ b 0x2C5BA0
@ Message layout: before each element is parsed, the text object, next character and index.
.section .patch.text_element, "ax", %progbits
    bl quick_text_element
.section .expect.text_element, "", %progbits
    .word 0xEB07810A @ bl 0x4C08C0
@ Message layout: code 0x05 (instant text off) asks Quick Text first.
.section .patch.instant_off, "a", %progbits
    .word quick_text_instant_off
.section .expect.instant_off, "", %progbits
    .word 0x002E06C8
@ Gameplay_Draw and Play_Update: the active camera's update.
.section .patch.draw_camera, "ax", %progbits
    bl _ZN7z3dmods5oot3d18redrawActiveCameraEPvS1_
.section .expect.draw_camera, "", %progbits
    .word 0xEBFFD5B1 @ bl 0x2D84C4
.section .patch.update_camera, "ax", %progbits
    bl _ZN7z3dmods5oot3d18updateActiveCameraEPvS1_
.section .expect.update_camera, "", %progbits
    .word 0xEBFFD047 @ bl 0x2D84C4
@ Gameplay_Update: the play update.
.section .patch.play_update, "ax", %progbits
    bl _ZN7z3dmods5oot3d10updatePlayEPv
.section .expect.play_update, "", %progbits
    .word 0xEBFA428C @ bl 0x2E2E60
@ Sram_WriteSave's first instruction: every save passes here.
.section .patch.save_entry, "ax", %progbits
    b save_entry
.section .expect.save_entry, "", %progbits
    .word 0xE92D47F0 @ push {r4-r10, lr}
@ Gameplay_Update's tail call: The One Screen's orbit camera hook, back to the retail branch.
.section .patch.base_camera, "ax", %progbits
    b kGameplayDraw
.section .expect.base_camera, "", %progbits
    .word 0xEA070C77 @ b tos_after
@ The One Screen's Hud_Submit: its HUD is never visible; ours is drawn by the app (hud.toml).
.section .patch.base_hud, "ax", %progbits
    mov r0, #0
.section .expect.base_hud, "", %progbits
    .word 0xC3A00001 @ movgt r0, #1
@ File select's BACK button (quads 173 and 174, its lit frame 118 to 120; decomp
@ docs/file_select.md): 40 pixels left of the touch screen, into the top screen's corner, which
@ file_select.cpp and base_file_select_* bring into view.
.section .patch.back_lit, "a", %progbits
    .float -40.0, 202.0, 10.0, 202.0, -40.0, 204.0
.section .expect.back_lit, "", %progbits
    .float 0.0, 202.0, 50.0, 202.0, 0.0, 204.0
.section .patch.back_button, "a", %progbits
    .float -40.0, 204.0, 0.0, 204.0
.section .expect.back_button, "", %progbits
    .float 0.0, 204.0, 40.0, 204.0
@ The touch screen's 2D layers (TouchScreen2D_BeginLayer0 to 3): each one's projection, widened
@ while file select is up.
.section .patch.touch_projection0, "ax", %progbits
    bl _ZN7z3dmods5oot3d20fileSelectProjectionEPvPfPKf
.section .expect.touch_projection0, "", %progbits
    .word 0xEBFC163B @ bl Camera2D_SetMatrices
.section .patch.touch_projection1, "ax", %progbits
    bl _ZN7z3dmods5oot3d20fileSelectProjectionEPvPfPKf
.section .expect.touch_projection1, "", %progbits
    .word 0xEBFC1CE7 @ bl Camera2D_SetMatrices
.section .patch.touch_projection2, "ax", %progbits
    bl _ZN7z3dmods5oot3d20fileSelectProjectionEPvPfPKf
.section .expect.touch_projection2, "", %progbits
    .word 0xEBFC2006 @ bl Camera2D_SetMatrices
.section .patch.touch_projection3, "ax", %progbits
    bl _ZN7z3dmods5oot3d20fileSelectProjectionEPvPfPKf
.section .expect.touch_projection3, "", %progbits
    .word 0xEBFC2569 @ bl Camera2D_SetMatrices
@ The One Screen's file select on the top screen: framebuffer rows 0 to 400, not 40 to 360,
@ for the projection file_select.cpp widens to match.
.section .patch.base_file_select_row, "ax", %progbits
    mov r1, #0
.section .expect.base_file_select_row, "", %progbits
    .word 0xE3A01028 @ mov r1, #40
.section .patch.base_file_select_rows, "ax", %progbits
    mov r3, #400
.section .expect.base_file_select_rows, "", %progbits
    .word 0xE3A03D05 @ mov r3, #320
@ The One Screen's Minimap_Project lifts the minimap clear of the game's B button while mounted;
@ that button is off the top screen (base_b_ring), so the minimap stays where it is on foot.
.section .patch.base_minimap, "ax", %progbits
    nop
.section .expect.base_minimap, "", %progbits
    .word 0x0A000092 @ beq: mounted
@ The game's own B button on the top screen (the bow while mounted, a minigame's item): ours
@ shows it (hud.toml). TopHud_UpdateButtons keeps its ring off screen in every mode, and
@ TopHud_Draw does not draw its item and count.
.section .patch.base_b_ring, "ax", %progbits
    b . + 0x18
.section .expect.base_b_ring, "", %progbits
    .word 0x0A000004 @ beq: no B button mode
.section .patch.base_b_item, "ax", %progbits
    nop
.section .expect.base_b_item, "", %progbits
    .word 0x1BFB2E77 @ blne HudItemButtons_Draw
@ PadMgr_RequestPadData: the pad word its ocarina note mapping reads, after The One Screen's hook.
.section .patch.song_notes, "ax", %progbits
    bl song_notes
.section .expect.song_notes, "", %progbits
    .word 0xE1A08000 @ mov r8, r0
@ TextRenderer_Init: every renderer keeps its font in the static glyph texture (font_atlas.cpp).
.section .patch.glyph_texture_mode, "ax", %progbits
    bl glyph_texture_mode
.section .expect.glyph_texture_mode, "", %progbits
    .word 0xE1A0100B @ mov r1, r11
@ TextRenderer_AddGlyph's static branch: the cell of the character in r7, into r5.
.section .patch.static_glyph_cell, "ax", %progbits
    mov r0, r4
    mov r1, r7
    bl _ZN7z3dmods5oot3d15staticGlyphCellEPvj
    mov r5, r0
    nop
    nop
    nop
.section .expect.static_glyph_cell, "", %progbits
    .word 0xE3570C01 @ cmp r7, #256
    .word 0xE1A05007 @ mov r5, r7
    .word 0x3A000004 @ bcc +0x18
    .word 0xE5940010 @ ldr r0, [r4, #16]
    .word 0xEB005D58 @ bl QbfFont_GetDefaultChar
    .word 0xE3500C01 @ cmp r0, #256
    .word 0xE1A05000 @ mov r5, r0
@ TextRenderer_FillStaticGlyphTexture: the glyph of each cell.
.section .patch.static_cell_glyph, "ax", %progbits
    bl _ZN7z3dmods5oot3d15staticCellGlyphEPvjS1_
.section .expect.static_cell_glyph, "", %progbits
    .word 0xEBF99FAD @ bl QbfFont_FindGlyph
.text
@ r0 to r3 are set after the site.
glyph_texture_mode:
    push {r4, lr}
    bl _ZN7z3dmods5oot3d16glyphTextureModeEv
    mov r1, r0
    pop {r4, pc}
song_notes:
    push {r0, lr}
    bl _ZN7z3dmods5oot3d13nextSongNotesEv
    pop {r1, lr}
    orr r8, r0, r1
    bx lr
quick_text_element:
    push {r0-r3, r12, lr}
    mov r0, r5
    mov r1, r8
    mov r2, r9
    bl _ZN7z3dmods5oot3d16quickTextElementEPvPKcj
    pop {r0-r3, r12, lr}
    b kTextParseNext
save_entry:
    push {r0-r3, r12, lr}
    bl _ZN7z3dmods5oot3d10beforeSaveEv
    pop {r0-r3, r12, lr}
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    b kSramWriteSave + 4
@ With Quick Text on, code 0x05 goes straight to the handlers' common tail; off, to its own handler.
quick_text_instant_off:
    push {r0-r3, r12, lr}
    bl _ZN7z3dmods5oot3d11quickTextOnEv
    cmp r0, #0
    pop {r0-r3, r12, lr}
    bne kMessageLayoutText + 0xAEC
    b kMessageLayoutText + 0x2E0
