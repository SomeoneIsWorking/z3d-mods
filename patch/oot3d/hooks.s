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
@ Message layout: code 0x05 (instant text off) goes straight to the handlers' common tail.
.section .patch.instant_off, "a", %progbits
    .word kMessageLayoutText + 0xAEC
.section .expect.instant_off, "", %progbits
    .word 0x002E06C8
.text
quick_text_element:
    push {r0-r3, r12, lr}
    mov r0, r5
    mov r1, r8
    mov r2, r9
    bl _ZN7z3dmods5oot3d16quickTextElementEPvPKcj
    pop {r0-r3, r12, lr}
    b kTextParseNext
