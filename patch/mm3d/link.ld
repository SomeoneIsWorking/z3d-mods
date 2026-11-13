OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_ZN7z3dmods4mm3d12stepCutsceneEPvS1_PKv)
INCLUDE symbols.ld
SECTIONS {
  .patch.interpreter_start kCutsceneBeginScript + 0x1D8 : { KEEP(*(.patch.interpreter_start)) }
  .patch.interpreter_state1 kCutsceneStartScript + 0x2C : { KEEP(*(.patch.interpreter_state1)) }
  .patch.interpreter_tail kCutsceneRunScript + 0x28 : { KEEP(*(.patch.interpreter_tail)) }
  . = PAYLOAD;
  .payload : { *(.text*) *(.rodata*) *(.data*) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
