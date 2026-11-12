OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_ZN7z3dmods4mm3d12stepCutsceneEPvS1_PKv)
SECTIONS {
  .patch.interpreter_start 0x00174374 : { KEEP(*(.patch.interpreter_start)) }
  .patch.interpreter_state1 0x004CF9F0 : { KEEP(*(.patch.interpreter_state1)) }
  .patch.interpreter_tail 0x003F1A74 : { KEEP(*(.patch.interpreter_tail)) }
  . = PAYLOAD;
  .payload : { *(.text*) *(.rodata*) *(.data*) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
