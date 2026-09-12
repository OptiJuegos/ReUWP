#include "shim/render_distance_policy.h"

namespace shim::render_distance_policy {
namespace {

constexpr int kBlocksPerChunk = 16;
constexpr int kCustomMaximumBlocks = 4 * kBlocksPerChunk;
constexpr int kDecreaseBridgeBlocks = 5 * kBlocksPerChunk;

}  // namespace

bool TryResolveLowStep(int current, int minimum, bool increase,
                       int* next_value) noexcept {
  if (next_value == nullptr) {
    return false;
  }

  if (minimum < 1) {
    minimum = 1;
  } else if (minimum > 4) {
    minimum = 4;
  }
  const int minimum_blocks = minimum * kBlocksPerChunk;

  if (increase) {
    if (current >= kCustomMaximumBlocks) {
      return false;
    }
    *next_value =
        current < minimum_blocks ? minimum_blocks : current + kBlocksPerChunk;
    if (*next_value > kCustomMaximumBlocks) {
      *next_value = kCustomMaximumBlocks;
    }
    return true;
  }

  if (current > kDecreaseBridgeBlocks) {
    return false;
  }
  if (current <= minimum_blocks) {
    *next_value = current;
    return true;
  }

  *next_value = current - kBlocksPerChunk;
  if (*next_value < minimum_blocks) {
    *next_value = minimum_blocks;
  }
  return true;
}

}  // namespace shim::render_distance_policy
