.syntax unified
.arm
@ Addresses are in Project Restoration's code layout (v1.0), which its v110 patch produces.
@ Cutscene update, first frame of a script.
.section .patch.interpreter_start, "ax", %progbits
    bl _ZN7z3dmods4mm3d12stepCutsceneEPvS1_PKv
.section .expect.interpreter_start, "", %progbits
    .word 0xEB0040CE @ bl 0x1846B4
@ State 1 handler.
.section .patch.interpreter_state1, "ax", %progbits
    bl _ZN7z3dmods4mm3d12stepCutsceneEPvS1_PKv
.section .expect.interpreter_state1, "", %progbits
    .word 0xEBF2D32F @ bl 0x1846B4
@ State 2 and 4 handler: the per-frame tail call into the interpreter.
.section .patch.interpreter_tail, "ax", %progbits
    b _ZN7z3dmods4mm3d12stepCutsceneEPvS1_PKv
.section .expect.interpreter_tail, "", %progbits
    .word 0xEAF64B0E @ b 0x1846B4
@ Actor_UpdateAll's first call, once a frame in play; r7 is the play state.
.section .patch.actor_update, "ax", %progbits
    bl actor_update
.section .expect.actor_update, "", %progbits
    .word 0xEBFFE947 @ bl 0x16EBAC
@ Play_SaveAtStatue's first instruction: both statues' saves pass here.
.section .patch.save_entry, "ax", %progbits
    b save_entry
.section .expect.save_entry, "", %progbits
    .word 0xE92D41F0 @ push {r4-r8, lr}
.text
save_entry:
    push {r0-r3, r12, lr}
    bl _ZN7z3dmods4mm3d10beforeSaveEv
    pop {r0-r3, r12, lr}
    push {r4, r5, r6, r7, r8, lr}
    b kPlaySaveAtStatue + 4
actor_update:
    push {r3, lr}
    mov r0, r7
    bl _ZN7z3dmods4mm3d16updateChordItemsEPv
    pop {r3, lr}
    b kActorUpdateFirstCall
