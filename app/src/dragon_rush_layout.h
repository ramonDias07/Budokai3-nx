#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace b3 {

// Two triangles with x, y, z, tint, u and v in each vertex.
using DragonRushIconVertices = std::array<uint32_t, 36>;

inline bool RepositionDragonRushIcon(DragonRushIconVertices& vertices) {
  struct Icon {
    float x, y, width, u_left, u_right;
    size_t switch_slot;
  };
  static constexpr std::array<Icon, 4> icons{{
      {344, 356, 40, 0.000000f, 0.234375f, 1},  // X: left to top
      {380, 341, 40, 0.234375f, 0.468750f, 0},  // Y: top to left
      {417, 356, 40, 0.468750f, 0.703125f, 3},  // B: right to bottom
      {376, 372, 48, 0.703125f, 0.984375f, 2},  // A: bottom to right
  }};
  const float x0 = std::bit_cast<float>(vertices[0]);
  const float y0 = std::bit_cast<float>(vertices[1]);
  for (const Icon& icon : icons) {
    if (x0 != icon.x || y0 != icon.y) continue;
    const Icon& target = icons[icon.switch_slot];
    // Check the atlas coordinates and all corners before replacing positions.
    for (size_t i = 0; i < vertices.size(); i += 6) {
      const float x = std::bit_cast<float>(vertices[i]);
      const float y = std::bit_cast<float>(vertices[i + 1]);
      if ((x != icon.x && x != icon.x + icon.width) ||
          (y != icon.y && y != icon.y + 32) ||
          std::bit_cast<float>(vertices[i + 4]) != (x == icon.x ? icon.u_left : icon.u_right) ||
          std::bit_cast<float>(vertices[i + 5]) != (y == icon.y ? 0.0f : 0.09375f)) {
        return false;
      }
    }
    for (size_t i = 0; i < vertices.size(); i += 6) {
      const bool left = std::bit_cast<float>(vertices[i]) == icon.x;
      const bool top = std::bit_cast<float>(vertices[i + 1]) == icon.y;
      vertices[i] = std::bit_cast<uint32_t>(target.x + (left ? 0 : target.width));
      vertices[i + 1] = std::bit_cast<uint32_t>(target.y + (top ? 0 : 32));
    }
    return true;
  }
  return false;
}

}  // namespace b3
