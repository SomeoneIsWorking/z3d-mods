// Typed access to guest memory at fixed addresses and offsets.
#pragma once

#include <stdint.h>

namespace z3dmods::guest {

template <typename T> T read(const void *base, uint32_t offset) {
  return *reinterpret_cast<const volatile T *>(static_cast<const uint8_t *>(base) + offset);
}

template <typename T> void write(void *base, uint32_t offset, T value) {
  *reinterpret_cast<volatile T *>(static_cast<uint8_t *>(base) + offset) = value;
}

template <typename T> T read(uintptr_t address) {
  return *reinterpret_cast<const volatile T *>(address);
}

template <typename Fn> Fn function(uintptr_t address) {
  return reinterpret_cast<Fn>(address);
}

} // namespace z3dmods::guest
