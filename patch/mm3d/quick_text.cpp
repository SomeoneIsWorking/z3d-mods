// Quick Text: a text box prints in full in one update.
#include "../common/guest.hpp"
#include "options.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {

using PrinterMethodFn = void (*)(void *printer);

// Called by MessagePrinter_Update before it runs (hooks.s).
void quickTextPrinter(void *printer) {
  if (options().quickText == 0 || guest::read<uint8_t>(printer, kPrinterState) != kPrinterStatePrinting) {
    return;
  }
  const auto vtable = guest::read<uintptr_t>(printer, kPrinterVtable);
  guest::function<PrinterMethodFn>(guest::read<uintptr_t>(vtable + kPrinterInstantOnSlot))(printer);
}

} // namespace z3dmods::mm3d
