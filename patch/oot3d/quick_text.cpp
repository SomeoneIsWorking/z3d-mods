// Quick Text: every text box prints in full on its first frame.
#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using TextMethodFn = void (*)(void *text);

bool startsBox(const char *next, uint32_t index) {
  if (index == 0) {
    return true;
  }
  return static_cast<uint8_t>(next[-2]) == kTextCtrlPrefix &&
         static_cast<uint8_t>(next[-1]) == kTextCtrlWaitForInput;
}

} // namespace

// Called by Message_LayoutText before it parses each element (hooks.s).
void quickTextElement(void *text, const char *next, uint32_t index) {
  if (!startsBox(next, index)) {
    return;
  }
  const auto vtable = guest::read<uintptr_t>(text, 0);
  guest::function<TextMethodFn>(guest::read<uintptr_t>(vtable + kTextInstantOnSlot))(text);
}

} // namespace z3dmods::oot3d
