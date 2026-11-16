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
  .patch.interpreter_catchup kCutsceneRunScript + 0xF0 : { KEEP(*(.patch.interpreter_catchup)) }
  .patch.interpreter_tail kCutsceneRunScript + 0x120 : { KEEP(*(.patch.interpreter_tail)) }
  .patch.touch_projection3 kTouchScreen2DLayer3 + 0x1B8 : { KEEP(*(.patch.touch_projection3)) }
  .patch.touch_projection2 kTouchScreen2DLayer2 + 0x1F0 : { KEEP(*(.patch.touch_projection2)) }
  .patch.touch_projection1 kTouchScreen2DLayer1 + 0x1C4 : { KEEP(*(.patch.touch_projection1)) }
  .patch.touch_projection0 kTouchScreen2DLayer0 + 0x1CC : { KEEP(*(.patch.touch_projection0)) }
  .patch.song_notes kPadRequestData + 0x98 : { KEEP(*(.patch.song_notes)) }
  .patch.base_b_ring kTopHudUpdateButtons + 0x40 : { KEEP(*(.patch.base_b_ring)) }
  .patch.base_b_item kTopHudDraw + 0x458 : { KEEP(*(.patch.base_b_item)) }
  .patch.play_update kGameplayUpdate + 0x9C : { KEEP(*(.patch.play_update)) }
  .patch.base_camera kGameplayUpdate + 0xA8 : { KEEP(*(.patch.base_camera)) }
  .patch.static_cell_glyph kTextRendererFillStatic + 0x80 : { KEEP(*(.patch.static_cell_glyph)) }
  /* File select's quad places (hooks.s back_lit, back_button), two floats a quad. */
  .patch.back_lit kFileChoose + kFileChooseQuadPositions + kFileChooseQuadBackLit * 8 : { KEEP(*(.patch.back_lit)) }
  .patch.back_button kFileChoose + kFileChooseQuadPositions + kFileChooseQuadBack * 8 : { KEEP(*(.patch.back_button)) }
  /* The One Screen 891b60e's payload (decomp docs/hud.md): Hud_Submit's visible flag. */
  .patch.base_hud 0x612C84 : { KEEP(*(.patch.base_hud)) }
  /* Minimap_Project's mounted test. */
  .patch.base_minimap 0x6148C8 : { KEEP(*(.patch.base_minimap)) }
  /* Presentation_Target's file-select pass on the top screen: its viewport's first row and rows. */
  .patch.base_file_select_row 0x616400 : { KEEP(*(.patch.base_file_select_row)) }
  .patch.base_file_select_rows 0x616408 : { KEEP(*(.patch.base_file_select_rows)) }
  . = PAYLOAD;
  .payload : { KEEP(*(.options)) *(.text*) *(.rodata*) *(.data*) }
  /* The item slots' block the app reads and writes (tools/z3dmods/slot_link.py). */
  .link : { KEEP(*(.link)) }
  .payload_zero (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) }
}
