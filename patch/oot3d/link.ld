OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_ZN7z3dmods5oot3d12stepCutsceneEPvS1_PKv)
INCLUDE symbols.ld
SECTIONS {
  /* Fixed sites in address order. */
  .patch.text_element kMessageLayoutText + 0xA8 : { KEEP(*(.patch.text_element)) }
  .patch.instant_off kMessageLayoutText + 0x1B8 : { KEEP(*(.patch.instant_off)) }
  .patch.interpreter_catchup kCutsceneRunScript + 0xF0 : { KEEP(*(.patch.interpreter_catchup)) }
  .patch.interpreter_tail kCutsceneRunScript + 0x120 : { KEEP(*(.patch.interpreter_tail)) }
  . = PAYLOAD;
  .payload : { *(.text*) *(.rodata*) *(.data*) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
