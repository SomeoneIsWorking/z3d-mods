OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_ZN7z3dmods5oot3d12stepCutsceneEPvS1_PKv)
INCLUDE symbols.ld
SECTIONS {
  /* Fixed sites in address order. */
  .patch.static_glyph_cell kTextRendererAddGlyph + 0x150 : { KEEP(*(.patch.static_glyph_cell)) }
  .patch.glyph_texture_mode kTextRendererInit + 0x13C : { KEEP(*(.patch.glyph_texture_mode)) }
  .patch.text_element kMessageLayoutText + 0xA8 : { KEEP(*(.patch.text_element)) }
  .patch.instant_off kMessageLayoutText + 0x1B8 : { KEEP(*(.patch.instant_off)) }
  .patch.draw_camera kGameplayDraw + 0x808 : { KEEP(*(.patch.draw_camera)) }
  .patch.update_camera kPlayUpdate + 0x1540 : { KEEP(*(.patch.update_camera)) }
  .patch.save_entry kSramWriteSave : { KEEP(*(.patch.save_entry)) }
  .patch.static_cell_glyph kTextRendererFillStatic + 0x80 : { KEEP(*(.patch.static_cell_glyph)) }
  .patch.interpreter_catchup kCutsceneRunScript + 0xF0 : { KEEP(*(.patch.interpreter_catchup)) }
  .patch.interpreter_tail kCutsceneRunScript + 0x120 : { KEEP(*(.patch.interpreter_tail)) }
  .patch.song_notes kPadRequestData + 0x98 : { KEEP(*(.patch.song_notes)) }
  .patch.play_update kGameplayUpdate + 0x9C : { KEEP(*(.patch.play_update)) }
  .patch.base_camera kGameplayUpdate + 0xA8 : { KEEP(*(.patch.base_camera)) }
  /* The One Screen 891b60e's payload: Hud_Submit's visible flag (decomp docs/hud.md). */
  .patch.base_hud 0x612C84 : { KEEP(*(.patch.base_hud)) }
  . = PAYLOAD;
  .payload : { KEEP(*(.options)) *(.text*) *(.rodata*) *(.data*) }
  /* The item slots' block the app reads and writes (tools/z3dmods/slot_link.py). */
  .link : { KEEP(*(.link)) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
