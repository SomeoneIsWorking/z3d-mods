// MM3D cutscene scripts (.jdb): finding the text entry a script holds.
#pragma once

#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {

class CutsceneScript {
public:
  explicit CutsceneScript(const void *script) : script_(script) {}

  // Whether the text entry ending at `frame` asks the player a question. The script only shows it
  // by what follows an answer, so a question whose answers both just close the box is not seen.
  bool textIsChoiceAt(uint16_t frame) const {
    const auto count = static_cast<int32_t>(word(kScriptCommandCount));
    uint32_t at = kScriptHeaderSize;
    for (int32_t i = 0; i < count; ++i) {
      const auto id = static_cast<int32_t>(word(at + kCsCmdId));
      if (id == kCsCmdEnd) {
        return false;
      }
      const auto entries = word(at + kCsCmdCount);
      if (id == kCsCmdText && textListHasChoiceAt(at + kCsCmdHeaderSize, entries, frame)) {
        return true;
      }
      at += kCsCmdHeaderSize + listSize(id, entries);
    }
    return false;
  }

private:
  // Commands may sit at any byte offset; the game copies their words out a byte at a time.
  uint32_t word(uint32_t offset) const {
    uint32_t value = 0;
    for (uint32_t i = 0; i < sizeof(value); ++i) {
      value |= static_cast<uint32_t>(guest::read<uint8_t>(script_, offset + i)) << (i * 8);
    }
    return value;
  }

  uint16_t half(uint32_t offset) const {
    return static_cast<uint16_t>(guest::read<uint8_t>(script_, offset) |
                                 (guest::read<uint8_t>(script_, offset + 1) << 8));
  }

  bool textListHasChoiceAt(uint32_t first, uint32_t entries, uint16_t frame) const {
    for (uint32_t i = 0; i < entries; ++i) {
      const uint32_t entry = first + i * kCsTextSize;
      if (half(entry + kCsTextId) == kCsTextNone) {
        continue;
      }
      if (half(entry + kCsTextStart) < frame && frame <= half(entry + kCsTextEnd)) {
        return asksQuestion(entry);
      }
    }
    return false;
  }

  // The interpreter reads the alternates after an answer; types 2, 4 and 5 use them otherwise.
  bool asksQuestion(uint32_t entry) const {
    const auto type = half(entry + kCsTextType);
    if (type == kCsTextOcarina || type == kCsTextBossesRemains || type == kCsTextAllMasks) {
      return false;
    }
    return half(entry + kCsTextAlt1) != kCsTextNone || half(entry + kCsTextAlt2) != kCsTextNone;
  }

  static bool isActorCue(int32_t id) {
    return (id >= kCsCmdActorCueFirst && id < kCsCmdActorCueFirst + kCsCmdActorCueCount) ||
           id == kCsCmdPlayerCue || id == kCsCmdActorCue201 ||
           (id >= kCsCmdActorCueExtraFirst && id < kCsCmdActorCueExtraFirst + kCsCmdActorCueExtraCount);
  }

  static uint32_t listSize(int32_t id, uint32_t entries) {
    if (id == kCsCmdCamera) {
      return entries;
    }
    if (isActorCue(id)) {
      return entries * kCsActorCueSize;
    }
    switch (id) {
    case kCsCmdText:
    case kCsCmdTransitionGeneral:
    case kCsCmdFadeOutSeq:
    case kCsCmdTime:
    case kCsCmdRumble:
    case kCsCmd601:
    case kCsCmd604:
      return entries * kCsEntrySizeWide;
    default:
      return entries * kCsEntrySize;
    }
  }

  const void *script_;
};

} // namespace z3dmods::mm3d
