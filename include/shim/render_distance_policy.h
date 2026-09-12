#pragma once

namespace shim::render_distance_policy {

// Handles the low 1..4 chunk range added by ReUWP plus the 5->4 decrease
// bridge used by builds whose native list starts at five chunks. Minecraft
// stores these values internally in blocks (16 blocks per chunk). Returns true
// when the caller must consume the input locally; false means the game's
// original render-distance callback should run unchanged.
bool TryResolveLowStep(int current, int minimum, bool increase,
                       int* next_value) noexcept;

}  // namespace shim::render_distance_policy
