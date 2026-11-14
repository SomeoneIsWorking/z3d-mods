// The game's minimap on the top screen, in its bottom right corner as OoT3D's is: the map and
// marker quads MainScreen_DrawMinimap queues for the touch screen move to the top screen's queue,
// the map's drawn extent fitted to one box whatever the scene (decomp docs/hud.md "Minimap").
#include "../common/guest.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

// The box in the top screen's 400x240 pixels, and the most a touch screen pixel grows to in it.
constexpr float kBoxRight = 396.0F;
constexpr float kBoxBottom = 236.0F;
constexpr float kBoxWidth = 76.0F;
constexpr float kBoxHeight = 64.0F;
constexpr float kMostShare = 0.6F;
// A quad's turn: three rows of four floats.
constexpr uint32_t kRotationRow = 4;
constexpr uint32_t kRotationFloats = 12;
constexpr uint32_t kCorners = 4;
// The batches one call queues: the room's two layers and the markers.
constexpr uint32_t kMostBatches = 3;

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Extent {
  float left = 1e9F;
  float top = 1e9F;
  float right = -1e9F;
  float bottom = -1e9F;
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

uintptr_t mapLayer(uint32_t layer) {
  return guest::read<uintptr_t>(kMinimapQuads + layer * sizeof(uint32_t));
}

bool isMinimap(uintptr_t batch) {
  return batch != 0 && (batch == mapLayer(0) || batch == mapLayer(1) ||
                        batch == guest::read<uintptr_t>(kMinimapMarkerQuads));
}

// Every corner of the batch's quads as the game places them.
void extend(uintptr_t batch, Extent &extent) {
  auto *base = reinterpret_cast<uint8_t *>(batch);
  const auto *positions = guest::read<const Vec3 *>(base, kQuadPositions);
  const auto *sizes = guest::read<const Vec3 *>(base, kQuadSizes);
  const auto *rotations = guest::read<const float *>(base, kQuadRotations);
  const auto *corners = reinterpret_cast<const Vec3 *>(base + kQuadCorners);
  const uint32_t count = guest::read<uint32_t>(base, kQuadCount);
  for (uint32_t i = 0; i < count; ++i) {
    const float *turn = rotations + i * kRotationFloats;
    for (uint32_t c = 0; c < kCorners; ++c) {
      const Vec3 point{corners[c].x * sizes[i].x, corners[c].y * sizes[i].y, corners[c].z * sizes[i].z};
      const float x = positions[i].x + turn[0] * point.x + turn[1] * point.y + turn[2] * point.z;
      const float y = positions[i].y + turn[kRotationRow] * point.x + turn[kRotationRow + 1] * point.y +
                      turn[kRotationRow + 2] * point.z;
      extent.left = x < extent.left ? x : extent.left;
      extent.right = x > extent.right ? x : extent.right;
      extent.top = y < extent.top ? y : extent.top;
      extent.bottom = y > extent.bottom ? y : extent.bottom;
    }
  }
}

// The batch's quads scaled by `share` about the extent's middle, which lands on the box's.
void place(uintptr_t batch, const Extent &extent, float share) {
  const float fromX = (extent.left + extent.right) / 2.0F;
  const float fromY = (extent.top + extent.bottom) / 2.0F;
  const float toX = kBoxRight - kBoxWidth / 2.0F;
  const float toY = kBoxBottom - kBoxHeight / 2.0F;
  auto *base = reinterpret_cast<uint8_t *>(batch);
  auto *positions = guest::read<Vec3 *>(base, kQuadPositions);
  auto *sizes = guest::read<Vec3 *>(base, kQuadSizes);
  const uint32_t count = guest::read<uint32_t>(base, kQuadCount);
  for (uint32_t i = 0; i < count; ++i) {
    positions[i].x = toX + (positions[i].x - fromX) * share;
    positions[i].y = toY + (positions[i].y - fromY) * share;
    sizes[i].x *= share;
    sizes[i].y *= share;
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
  uintptr_t moved[kMostBatches] = {};
  uint32_t priorities[kMostBatches] = {};
  uint32_t movedCount = 0;
  uint32_t kept = 0;
  for (uint32_t i = 0; i < *touch.count; ++i) {
    const uintptr_t batch = touch.entries[i];
    if (isMinimap(batch) && movedCount < kMostBatches) {
      moved[movedCount] = batch;
      priorities[movedCount] = touch.priorities[i];
      ++movedCount;
      continue;
    }
    touch.entries[kept] = batch;
    touch.priorities[kept] = touch.priorities[i];
    ++kept;
  }
  *touch.count = kept;
  Extent extent;
  for (uint32_t i = 0; i < movedCount; ++i) {
    if (moved[i] == mapLayer(0) || moved[i] == mapLayer(1)) {
      extend(moved[i], extent);
    }
  }
  // No map drawn: the markers stay where the game put them, so the draw still uses up their quads.
  if (extent.right <= extent.left || extent.bottom <= extent.top) {
    for (uint32_t i = 0; i < movedCount; ++i) {
      insert(touch, moved[i], priorities[i]);
    }
    return;
  }
  const float wide = kBoxWidth / (extent.right - extent.left);
  const float tall = kBoxHeight / (extent.bottom - extent.top);
  float share = wide < tall ? wide : tall;
  share = share < kMostShare ? share : kMostShare;
  for (uint32_t i = 0; i < movedCount && *top.count < kDrawQueueCapacity; ++i) {
    place(moved[i], extent, share);
    insert(top, moved[i], priorities[i]);
  }
}

} // namespace z3dmods::mm3d
