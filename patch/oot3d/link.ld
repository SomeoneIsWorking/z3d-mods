OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_ZN7z3dmods5oot3d12stepCutsceneEPvS1_PKv)
INCLUDE symbols.ld
SECTIONS {
  /* Fixed sites in address order. */
  .patch.text_element kMessageLayoutText + 0xA8 : { KEEP(*(.patch.text_element)) }
  .patch.instant_off kMessageLayoutText + 0x1B8 : { KEEP(*(.patch.instant_off)) }
  .patch.draw_camera kGameplayDraw + 0x808 : { KEEP(*(.patch.draw_camera)) }
  .patch.update_camera kPlayUpdate + 0x1540 : { KEEP(*(.patch.update_camera)) }
  .patch.interpreter_catchup kCutsceneRunScript + 0xF0 : { KEEP(*(.patch.interpreter_catchup)) }
  .patch.interpreter_tail kCutsceneRunScript + 0x120 : { KEEP(*(.patch.interpreter_tail)) }
  .patch.play_update kGameplayUpdate + 0x9C : { KEEP(*(.patch.play_update)) }
  .patch.base_camera kGameplayUpdate + 0xA8 : { KEEP(*(.patch.base_camera)) }
  . = PAYLOAD;
  .payload : { KEEP(*(.options)) *(.text*) *(.rodata*) *(.data*) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
