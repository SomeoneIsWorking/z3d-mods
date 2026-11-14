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
.text
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
