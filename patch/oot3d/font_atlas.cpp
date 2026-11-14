// The text font as one fixed texture: every text renderer keeps its font whole in the game's
// static glyph texture, so a texture pack can replace it (decomp docs/message.md).
#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using FindGlyphFn = int (*)(void *font, uint32_t character, void *glyph);

// The static texture holds characters 0 to 255 by their codes; the control codes' cells take the
// font's characters past 255, in the table's order.
inline constexpr uint32_t kSpareCells = 0x20;

uint32_t glyphCode(const void *header, uint32_t index) {
  return guest::read<uint16_t>(header, kQbfHeaderSize + index * kQbfGlyphSize + kQbfGlyphCode);
}

// The character in a cell of the static texture; 0 where the cell is free.
uint32_t cellCharacter(const void *font, uint32_t cell) {
  if (cell >= kSpareCells) {
    return cell;
  }
  const auto *header = guest::read<const void *>(font, kQbfFontData);
  if (header == nullptr) {
    return 0;
  }
  const uint32_t count = guest::read<uint16_t>(header, kQbfGlyphCount);
  uint32_t spare = 0;
  for (uint32_t i = 0; i < count; i++) {
    const uint32_t code = glyphCode(header, i);
    if (code >= kGlyphTextureChars) {
      if (spare == cell) {
        return code;
      }
      spare++;
    }
  }
  return 0;
}

} // namespace

// TextRenderer_Init's mode for its glyph texture (hooks.s).
uint32_t glyphTextureMode() {
  return kGlyphTextureStatic;
}

// TextRenderer_AddGlyph's static branch (hooks.s): the cell a character is drawn from.
uint32_t staticGlyphCell(void *renderer, uint32_t character) {
  if (character < kGlyphTextureChars) {
    return character;
  }
  void *font = guest::read<void *>(renderer, kTextRendererFont);
  for (uint32_t cell = 0; cell < kSpareCells; cell++) {
    const uint32_t held = cellCharacter(font, cell);
    if (held == character) {
      return cell;
    }
    if (held == 0) {
      break;
    }
  }
  const auto *header = guest::read<const void *>(font, kQbfFontData);
  return header == nullptr ? 0 : guest::read<uint16_t>(header, kQbfDefaultChar);
}

// TextRenderer_FillStaticGlyphTexture's lookup (hooks.s): the glyph that fills a cell.
int staticCellGlyph(void *font, uint32_t cell, void *glyph) {
  return guest::function<FindGlyphFn>(kQbfFindGlyph)(font, cellCharacter(font, cell), glyph);
}

} // namespace z3dmods::oot3d
