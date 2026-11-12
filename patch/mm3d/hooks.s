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
