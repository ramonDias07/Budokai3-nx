#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace b3 {

// Build 0.1.53: the main menu draws its UI sprites with the "HUD pair" shaders, and the hint is a 12-vertex triangle
// list (two quads). Up to 24 vertices (4 quads) are read.
using MenuHintVertices = std::array<uint32_t, 144>;

struct MenuHintSprite {
  float x0, y0, x1, y1;  // screen rectangle
  float u0, v0, u1, v1;  // texture coordinates
  uint32_t tint;         // the first vertex's tint word, raw
};

// Rectangle of one quad (6 vertices of 6 words). False when a value is not finite.
inline bool MenuHintQuad(const uint32_t* quad, MenuHintSprite& out) {
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
  for (size_t i = 0; i < 36; i += 6) {
    const float x = std::bit_cast<float>(quad[i]);
    const float y = std::bit_cast<float>(quad[i + 1]);
    const float u = std::bit_cast<float>(quad[i + 4]);
    const float v = std::bit_cast<float>(quad[i + 5]);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(u) || !std::isfinite(v)) return false;
    x0 = x < x0 ? x : x0;
    y0 = y < y0 ? y : y0;
    x1 = x > x1 ? x : x1;
    y1 = y > y1 ? y : y1;
    u0 = u < u0 ? u : u0;
    v0 = v < v0 ? v : v0;
    u1 = u > u1 ? u : u1;
    v1 = v > v1 ? v : v1;
  }
  out = {x0, y0, x1, y1, u0, v0, u1, v1, quad[3]};
  return true;
}

// True when ANY quad of the draw (vertex_count is a multiple of 6) lies inside the (tolerant) area of the hint.
inline bool MatchMenuHintSprite(const uint32_t* vertices, uint32_t vertex_count, MenuHintSprite& out) {
  // Build 0.1.54: the main-menu UI space is 800x450 (x1.6 -> 1280x720 guest). Hint rect observed on 0.1.53:
  // 501,395-669,427.
  constexpr float kX0Min = 485.0f, kX0Max = 520.0f, kX1Min = 650.0f, kX1Max = 690.0f;
  constexpr float kY0Min = 380.0f, kY0Max = 410.0f, kY1Min = 412.0f, kY1Max = 442.0f;
  for (uint32_t q = 0; q + 6 <= vertex_count; q += 6) {
    MenuHintSprite quad;
    if (!MenuHintQuad(vertices + size_t(q) * 6, quad)) continue;
    const float w = quad.x1 - quad.x0, h = quad.y1 - quad.y0;
    if (w < 140.0f || w > 200.0f || h < 20.0f || h > 45.0f) continue;
    if (quad.x0 < kX0Min || quad.x0 > kX0Max || quad.x1 < kX1Min || quad.x1 > kX1Max) continue;
    if (quad.y0 < kY0Min || quad.y0 > kY0Max || quad.y1 < kY1Min || quad.y1 > kY1Max) continue;
    out = quad;
    return true;
  }
  return false;
}

// True while one of the port's menus (quick, advanced, achievements) is open. Written by main.cpp, read by the GPU
// thread once per swap.
inline std::atomic<bool> g_port_menu_open{false};

// Selects the legend image (false: English, true: Portuguese). Defined in main.cpp. Step 2 of the translation calls it.
void SetMenuLegendLanguage(bool pt);

}  // namespace b3
