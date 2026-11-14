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
