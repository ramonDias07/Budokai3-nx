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

// Internal resolution, live (cvar b3_deko_resolucao_interna): 720, 540, 360 or 0 = automatic. Takes effect at the next frame
// boundary; never above the output height.
void PedirResolucaoInterna(int altura);
// Official GPU boost (cvar b3_gpu_boost): handheld only. ligar=true asks for the 460.8 MHz profile in the background
// (returns false if docked or a request is still running); ligar=false restores the previous profile. Re-evaluated by
// the renderer every 30 swaps and on dock/undock.
bool AplicarGpuBoost(bool ligar);

std::unique_ptr<rex::system::IGraphicsSystem> CreateXenosDeko(
    const std::filesystem::path& shader_library, DekoOptions& options);
}
