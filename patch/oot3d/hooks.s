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
