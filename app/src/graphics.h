#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <rex/system/interfaces/graphics.h>

namespace b3 {
enum DekoOption : uint32_t {
  kReduzirCopias = 1u << 0,
  kDesfoqueLeve = 1u << 1,
  kBloom = 1u << 2,
  kRestaurarZ = 1u << 3,
  kSombrasProjetadas = 1u << 4,
};

using DekoOptions = std::atomic<uint32_t>;
constexpr uint32_t kDofResolutionMask = 3u << 8;

constexpr uint32_t DofResolutionOption(int height) {
  return (height == 360 ? 1u : height == 540 ? 2u : 0u) << 8;
}

constexpr int DofResolutionHeight(uint32_t options) {
  const uint32_t mode = (options & kDofResolutionMask) >> 8;
  return mode == 1 ? 360 : mode == 2 ? 540 : 720;
}

std::unique_ptr<rex::system::IGraphicsSystem> CreateXenosDeko(
    const std::filesystem::path& shader_library, DekoOptions& options);
}
