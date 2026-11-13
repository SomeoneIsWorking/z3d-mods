// OoT3D cutscene scripts (" BDQ"): finding the text entry a script holds.
#pragma once

#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {

class CutsceneScript {
public:
  explicit CutsceneScript(const void *script) : script_(script) {}

  // Whether the text entry ending at `frame` asks the player a question.
  bool textIsChoiceAt(uint16_t frame) const {
    const auto count = guest::read<int32_t>(script_, kCsScriptCommandCount);
    uint32_t at = kCsScriptHeaderSize;
    for (int32_t i = 0; i < count; ++i) {
      const auto id = guest::read<int32_t>(script_, at);
      if (id == kCsCmdEnd) {
        return false;
      }
      if (id == kCsCmdText && textListHasChoiceAt(at, frame)) {
        return true;
      }
      at += commandSize(at, id);
    }
    return false;
  }

private:
  bool textListHasChoiceAt(uint32_t at, uint16_t frame) const {
    const auto entries = guest::read<uint32_t>(script_, at + sizeof(int32_t));
    for (uint32_t i = 0; i < entries; ++i) {
      const uint32_t entry = at + kCsListHeadSize + i * kCsListEntrySize;
      const auto start = guest::read<uint16_t>(script_, entry + kCsTextStart);
      const auto end = guest::read<uint16_t>(script_, entry + kCsTextEnd);
      if (start < frame && frame <= end) {
        return guest::read<uint16_t>(script_, entry + kCsTextType) == kCsTextChoice;
      }
    }
    return false;
  }

  uint32_t commandSize(uint32_t at, int32_t id) const {
    switch (id) {
    case kCsCmdCamEyeSpline:
    case kCsCmdCamAtSpline:
    case kCsCmdCamEyeSplineRel:
    case kCsCmdCamAtSplineRel: {
      uint32_t point = at + kCsCamSplineHeadSize;
      while (guest::read<uint8_t>(script_, point) != 0xFF) {
        point += kCsCamPointSize;
      }
      return point + kCsCamPointSize - at;
    }
    case kCsCmdTransition:
    case kCsCmdDestination:
      return kCsCmdFixedSize;
    case kCsCmdCamEye:
    case kCsCmdCamAt:
      return kCsCmdCamSize;
    case kCsCmdRumble:
    case kCsCmdText:
    case kCsCmdTime:
      return kCsListHeadSize + guest::read<uint32_t>(script_, at + sizeof(int32_t)) * kCsListEntrySize;
    case kCsCmdUnk96:
      return kCsCmdUnk96HeadSize + guest::read<uint16_t>(script_, at + kCsCmdUnk96Count) * kCsCmdUnk96EntrySize;
    case kCsCmdCamSpline:
      return kCsCmdCamSplineHeadSize + guest::read<uint32_t>(script_, at + sizeof(int32_t));
    default:
      return kCsListHeadSize + guest::read<uint32_t>(script_, at + sizeof(int32_t)) * kCsActorCueSize;
    }
  }

  const void *script_;
};

} // namespace z3dmods::oot3d
