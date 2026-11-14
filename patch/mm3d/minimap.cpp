// The game's minimap on the top screen, in its bottom right corner as OoT3D's is: the map and
// marker quads MainScreen_DrawMinimap queues for the touch screen move to the top screen's queue,
// scaled down (decomp docs/hud.md "Minimap").
#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

// The touch screen's 320x240 pixels drawn at this share, their bottom right corner this far in
// from the top screen's.
constexpr float kShare = 0.3F;
constexpr float kInset = 4.0F;
constexpr float kTopWidth = 400.0F;
constexpr float kTopHeight = 240.0F;
constexpr float kTouchWidth = 320.0F;
constexpr float kTouchHeight = 240.0F;

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Queue {
  uint32_t *count;
  uintptr_t *entries;
  uint32_t *priorities;
};

Queue queue(uint32_t index) {
  auto *manager = reinterpret_cast<uint8_t *>(kLayoutDrawManager);
  return {reinterpret_cast<uint32_t *>(manager + kDrawQueueCounts) + index,
          reinterpret_cast<uintptr_t *>(manager + kDrawQueues) + index * kDrawQueueCapacity,
          reinterpret_cast<uint32_t *>(manager + kDrawQueuePriorities) + index * kDrawQueueCapacity};
}

bool isMinimap(uintptr_t batch) {
  return batch != 0 && (batch == guest::read<uintptr_t>(kMinimapQuads) ||
                        batch == guest::read<uintptr_t>(kMinimapQuads + sizeof(uint32_t)) ||
                        batch == guest::read<uintptr_t>(kMinimapMarkerQuads));
}

void shrink(uintptr_t batch) {
  const float left = kTopWidth - kInset - kTouchWidth * kShare;
  const float top = kTopHeight - kInset - kTouchHeight * kShare;
  auto *base = reinterpret_cast<uint8_t *>(batch);
  auto *positions = guest::read<Vec3 *>(base, kQuadPositions);
  auto *sizes = guest::read<Vec3 *>(base, kQuadSizes);
  const uint32_t count = guest::read<uint32_t>(base, kQuadCount);
  for (uint32_t i = 0; i < count; ++i) {
    positions[i].x = left + positions[i].x * kShare;
    positions[i].y = top + positions[i].y * kShare;
    sizes[i].x *= kShare;
    sizes[i].y *= kShare;
  }
}

// Into `to` in ascending priority, after the entries of the same priority.
void insert(const Queue &to, uintptr_t batch, uint32_t priority) {
  uint32_t at = *to.count;
  while (at > 0 && to.priorities[at - 1] > priority) {
    to.entries[at] = to.entries[at - 1];
    to.priorities[at] = to.priorities[at - 1];
    --at;
  }
  to.entries[at] = batch;
  to.priorities[at] = priority;
  ++*to.count;
}

} // namespace

// In place of MainScreen_Draw's call of MainScreen_DrawMinimap (hooks.s).
void drawMinimap(void *play) {
  guest::function<void (*)(void *)>(kMainScreenDrawMinimap)(play);
  const Queue touch = queue(kDrawQueueTouch);
  const Queue top = queue(kDrawQueueTop);
  uint32_t kept = 0;
  for (uint32_t i = 0; i < *touch.count; ++i) {
    const uintptr_t batch = touch.entries[i];
    const uint32_t priority = touch.priorities[i];
    if (isMinimap(batch) && *top.count < kDrawQueueCapacity) {
      shrink(batch);
      insert(top, batch, priority);
      continue;
    }
    touch.entries[kept] = batch;
    touch.priorities[kept] = priority;
    ++kept;
  }
  *touch.count = kept;
}

} // namespace z3dmods::mm3d
