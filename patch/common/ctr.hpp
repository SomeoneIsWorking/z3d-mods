// 3DS kernel calls and service IPC (3dbrew's SVC and IPC tables), for patches inside a title.
#pragma once

#include <stdint.h>

namespace z3dmods::ctr {

using Handle = uint32_t;

inline constexpr uint32_t kMemStateFree = 0;
inline constexpr uint32_t kMemPermRead = 1;
inline constexpr uint32_t kMemPermDontCare = 0x10000000;
inline constexpr uint32_t kPageSize = 0x1000;

struct MemInfo {
  uint32_t base;
  uint32_t size;
  uint32_t perm;
  uint32_t state;
};

inline int32_t connectToPort(Handle *out, const char *name) {
  register uint32_t r0 asm("r0") = 0;
  register const char *r1 asm("r1") = name;
  asm volatile("svc 0x2D" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "memory");
  *out = reinterpret_cast<uintptr_t>(r1);
  return static_cast<int32_t>(r0);
}

inline int32_t sendSyncRequest(Handle session) {
  register uint32_t r0 asm("r0") = session;
  asm volatile("svc 0x32" : "+r"(r0) : : "r1", "r2", "r3", "r12", "memory");
  return static_cast<int32_t>(r0);
}

inline int32_t closeHandle(Handle handle) {
  register uint32_t r0 asm("r0") = handle;
  asm volatile("svc 0x23" : "+r"(r0) : : "r1", "r2", "r3", "r12", "memory");
  return static_cast<int32_t>(r0);
}

inline int32_t mapMemoryBlock(Handle block, uint32_t address, uint32_t perm, uint32_t otherPerm) {
  register uint32_t r0 asm("r0") = block;
  register uint32_t r1 asm("r1") = address;
  register uint32_t r2 asm("r2") = perm;
  register uint32_t r3 asm("r3") = otherPerm;
  asm volatile("svc 0x1F" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : : "r12", "memory");
  return static_cast<int32_t>(r0);
}

inline int32_t queryMemory(MemInfo *info, uint32_t address) {
  register uint32_t r0 asm("r0") = 0;
  register uint32_t r1 asm("r1") = 0;
  register uint32_t r2 asm("r2") = address;
  register uint32_t r3 asm("r3") = 0;
  register uint32_t r4 asm("r4") = 0;
  register uint32_t r5 asm("r5") = 0;
  asm volatile("svc 0x02" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4), "+r"(r5) : : "r12", "memory");
  *info = {r1, r2, r3, r4};
  return static_cast<int32_t>(r0);
}

// The calling thread's IPC command buffer: thread-local storage + 0x80.
inline uint32_t *commandBuffer() {
  uint32_t tls = 0;
  asm volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
  return reinterpret_cast<uint32_t *>(tls + 0x80);
}

inline constexpr uint32_t header(uint32_t command, uint32_t normal, uint32_t translate) {
  return (command << 16) | (normal << 6) | translate;
}

inline int32_t call(Handle session) {
  const int32_t sent = sendSyncRequest(session);
  return sent < 0 ? sent : static_cast<int32_t>(commandBuffer()[1]);
}

// srv: the service manager.
class ServiceManager {
public:
  ServiceManager() = default;
  ServiceManager(const ServiceManager &) = delete;
  ServiceManager &operator=(const ServiceManager &) = delete;
  ~ServiceManager() {
    if (session_ != 0) {
      closeHandle(session_);
    }
  }

  int32_t open() {
    int32_t result = connectToPort(&session_, "srv:");
    if (result < 0) {
      return result;
    }
    uint32_t *cmd = commandBuffer();
    cmd[0] = header(0x1, 0, 2); // RegisterClient
    cmd[1] = 0x20;              // the calling process id
    return call(session_);
  }

  int32_t serviceHandle(Handle *out, const char *name) {
    uint32_t *cmd = commandBuffer();
    uint32_t words[2] = {0, 0};
    uint32_t length = 0;
    for (; length < 8 && name[length] != 0; ++length) {
      words[length / 4] |= static_cast<uint32_t>(static_cast<uint8_t>(name[length])) << (8 * (length % 4));
    }
    cmd[0] = header(0x5, 4, 0); // GetServiceHandle
    cmd[1] = words[0];
    cmd[2] = words[1];
    cmd[3] = length;
    cmd[4] = 0;
    const int32_t result = call(session_);
    *out = cmd[3];
    return result;
  }

private:
  Handle session_ = 0;
};

// The first free page-aligned range of `size` bytes in [start, end).
inline uint32_t freeRange(uint32_t start, uint32_t end, uint32_t size) {
  for (uint32_t address = start; address >= start && address + size <= end;) {
    MemInfo info{};
    if (queryMemory(&info, address) < 0 || info.size == 0) {
      return 0;
    }
    if (info.state == kMemStateFree && info.base + info.size - address >= size) {
      return address;
    }
    address = info.base + info.size;
  }
  return 0;
}

} // namespace z3dmods::ctr
