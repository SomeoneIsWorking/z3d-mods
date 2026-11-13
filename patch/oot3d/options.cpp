#include "options.hpp"

namespace z3dmods::oot3d {
namespace {

[[gnu::section(".options"), gnu::used]] constinit volatile Options block;

} // namespace

const volatile Options &options() {
  return block;
}

} // namespace z3dmods::oot3d
