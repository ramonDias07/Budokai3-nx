// Real-time battle speed. Technique adapted from Budokai-NX by Unknown (MIT)
// and DBZ-Budokai-3-HD-Collection by NovaPowers; see CONTRIBUTORS.md.
#include "dbz3_pch.h"

#include "b3_battle_delta_bridge.h"
#include "frame_delta.h"
#include "game_speed.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>

REXCVAR_DECLARE(bool, b3_log_periodico);  // graphics.cpp
REXCVAR_DEFINE_INT32(b3_velocidade_jogo, 1, "Budokai 3",
                     "Velocidade da luta: 1 = tempo real (delta time), 0 = original do jogo.");

REXCVAR_DEFINE_BOOL(b3_tempo_jogo_mundo, true, "Budokai 3",
                    "GAME TIME (delta e teto de FPS) tambem no mapa do Dragon Universe (fase 8), com o mesmo valor escolhido para a luta.");

REX_EXTERN(__imp__sub_82088F70);
REX_EXTERN(__imp__sub_820FC260);
REX_EXTERN(__imp__sub_820AB818);
extern "C" void DeltaBattle_sub_820FC260(PPCContext&, uint8_t*);

namespace {
std::atomic<int> publishedPhase{-1};
std::atomic<bool> appliedThisFrame{false};
std::atomic<int> codeGuard{-1};  // -1 unchecked, 0 mismatch, 1 ok

thread_local b3::timing::DeltaSample lastSample;
thread_local int lastPhase = -1;
thread_local uint64_t battleAdjusted = 0;
thread_local b3::timing::SameFrameBattleDelta battleClock;
thread_local bool battleActive = false;
thread_local uint32_t activeManager = 0;
thread_local float activeTicks = 1;
thread_local b3::timing::GuestDeltaPatch patch;

bool EnabledNow() { return REXCVAR_GET(b3_velocidade_jogo) != 0; }
bool WorldNow() { return REXCVAR_GET(b3_tempo_jogo_mundo); }
}  // namespace

namespace b3 {
bool GameSpeedAppliedThisFrame() { return appliedThisFrame.load(std::memory_order_relaxed); }
int PublishedPhase() { return publishedPhase.load(std::memory_order_relaxed); }
bool TimedScene(int phase) { return b3::timing::FrameDelta::Scene(phase, WorldNow()); }
}  // namespace b3

REX_HOOK_RAW(sub_82088F70) {
  // This helper also exists outside the main loop. Never retime another caller.
  if (ctx.lr != 0x82165C3C) {
    __imp__sub_82088F70(ctx, base);
    return;
  }
  if (codeGuard.load(std::memory_order_relaxed) < 0) {
    constexpr uint32_t first[] = {0x7D8802A6, 0x9181FFF8, 0xFBE1FFF0, 0x9421FF90};
    constexpr uint32_t battle[] = {0x7D8802A6, 0x481F9285, 0x9421FF80, 0x3F80823D};
    constexpr uint32_t sequence[] = {0x3D608202, 0xC00BF60C, 0xFF010000, 0x419A0010};
    bool ok = true;
    for (unsigned i = 0; i < 4; ++i) {
      ok &= REX_LOAD_U32(0x82088F70 + i * 4) == first[i];
      ok &= REX_LOAD_U32(0x820FC260 + i * 4) == battle[i];
      ok &= REX_LOAD_U32(0x820AB818 + i * 4) == sequence[i];
    }
    codeGuard.store(ok ? 1 : 0, std::memory_order_relaxed);
    REXLOG_INFO("[B3 velocidade] guarda de codigo={}", ok ? "ok" : "FALHOU (desligado)");
  }
  struct Local {
    b3::timing::FrameDelta clock;
    uint64_t calls = 0, applied = 0, resets = 0, clamps = 0;
  };
  static thread_local Local local;
  auto read = [&](uint32_t address) { return REX_LOAD_U32(address); };
  auto write = [&](uint32_t address, uint32_t value) { REX_STORE_U32(address, value); };
  patch.Restore(read, write);
  const bool enabled = EnabledNow() && codeGuard.load(std::memory_order_relaxed) == 1;
  const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch()).count();
  // Original call order, register effects, video callbacks and vblank reset stay.
  __imp__sub_82088F70(ctx, base);
  const int phase = REX_LOAD_U8(0x824BA110);
  if (publishedPhase.exchange(phase, std::memory_order_relaxed) != phase) {
    REXLOG_INFO("[B3 fase] {} (tempo real {})", phase, b3::TimedScene(phase) ? "sim" : "nao");
  }
  if (!enabled) {
    lastSample = {};
    appliedThisFrame.store(false, std::memory_order_relaxed);
    return;
  }
  const float original = std::bit_cast<float>(read(b3::timing::FrameDelta::stepAddress));
  const auto sample = local.clock.Sample(ns, phase, original, true, WorldNow());
  lastSample = sample;
  lastPhase = phase;
  patch.Apply(sample, read, write);
  appliedThisFrame.store(sample.applied, std::memory_order_relaxed);
  ++local.calls;
  if (sample.applied) {
    ++local.applied;
    local.resets += sample.reset;
    local.clamps += sample.clamped;
  }
  if (local.calls % 120 == 0 && REXCVAR_GET(b3_log_periodico)) {
    REXLOG_INFO("[B3 velocidade] fase={} chamadas={} aplicadas={} dt_ms={:.3f} ticks={:.5f} resets={} clamps={}",
                phase, local.calls, local.applied, sample.seconds * 1000, sample.ticks, local.resets, local.clamps);
  }
}

REX_HOOK_RAW(sub_820FC260) {
  const int phase = REX_LOAD_U8(0x824BA110);
  if (battleActive || !lastSample.applied || !b3::timing::FrameDelta::BattleScene(phase) || lastPhase != phase) {
    __imp__sub_820FC260(ctx, base);
    return;
  }
  const uint32_t manager = REX_LOAD_U32(0x823D6CF4);
  // Original code dereferences this same manager before accessing +35124.
  if (!manager) {
    __imp__sub_820FC260(ctx, base);
    return;
  }
  auto read = [&](uint32_t address) { return REX_LOAD_U32(address); };
  auto write = [&](uint32_t address, uint32_t value) { REX_STORE_U32(address, value); };
  activeManager = manager;
  activeTicks = lastSample.ticks;
  battleClock.Begin(manager, activeTicks, read, write);
  battleActive = true;
  ++battleAdjusted;
  try {
    DeltaBattle_sub_820FC260(ctx, base);
  } catch (...) {
    battleActive = false;
    throw;
  }
  battleActive = false;
}

namespace b3::timing {
void PublishBattleEffectiveDelta(uint32_t manager, uint32_t originalBits, uint8_t* base) {
  if (!battleActive || manager != activeManager) return;
  auto write = [&](uint32_t address, uint32_t value) { REX_STORE_U32(address, value); };
  // Only the effective time output is adapted, including the exact original zero/hit-stop value.
  battleClock.effective.Publish(manager + 35120, originalBits, activeTicks, write);
}

void PublishBattleEffectsDelta(uint32_t address, uint32_t originalBits, uint8_t* base) {
  if (!battleActive) return;
  auto write = [&](uint32_t at, uint32_t value) { REX_STORE_U32(at, value); };
  if (address == SameFrameBattleDelta::effectsTicksAddress)
    battleClock.effectsTicks.Publish(address, originalBits, activeTicks, write);
  else if (address == SameFrameBattleDelta::effectsSecondsAddress)
    battleClock.effectsSeconds.Publish(address, originalBits, activeTicks, write);
}
}  // namespace b3::timing

REX_HOOK_RAW(sub_820AB818) {
  __imp__sub_820AB818(ctx, base);
  if (!battleActive) return;
  constexpr uint32_t address = 0x82395924;
  const uint32_t original = REX_LOAD_U32(address);
  auto write = [&](uint32_t at, uint32_t value) { REX_STORE_U32(at, value); };
  battleClock.sequence.Publish(address, original, activeTicks, write);
}
