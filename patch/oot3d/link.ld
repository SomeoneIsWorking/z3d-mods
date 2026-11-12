OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_ZN7z3dmods5oot3d12stepCutsceneEPvS1_PKv)
SECTIONS {
  .patch.interpreter_catchup 0x00322040 : { KEEP(*(.patch.interpreter_catchup)) }
  .patch.interpreter_tail 0x00322070 : { KEEP(*(.patch.interpreter_tail)) }
  . = PAYLOAD;
  .payload : { *(.text*) *(.rodata*) *(.data*) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
