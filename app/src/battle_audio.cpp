#include "dbz3_pch.h"

#include <rex/audio/switch/switch_audio_system.h>
#include <rex/hook.h>

#include "game_speed.h"

REX_EXTERN(__imp__sub_821100B0);
REX_EXTERN(__imp__sub_82081FE8);

REX_HOOK_RAW(sub_821100B0) {
  // US/Japan battle manager. This update advances both fighters once per
  // simulation frame, independently of GPU presentation and its overlays.
  constexpr uint32_t kBattleManager = 0x823D6CF4;
  constexpr uint32_t kBattlePhase = 35128;
  const uint32_t manager = REX_LOAD_U32(kBattleManager);
  const uint32_t phase = manager ? REX_LOAD_U32(manager + kBattlePhase) : 0;
  // With real-time delta the game already runs at normal speed: do not slow effects.
  rex::audio::nx::ReportBattleAudioFrame(!b3::GameSpeedAppliedThisFrame() && phase >= 3 && phase <= 7);
  __imp__sub_821100B0(ctx, base);
}

REX_HOOK_RAW(sub_82081FE8) {
  const uint32_t slot = ctx.r3.u32;
  const uint32_t asset = ctx.r4.u32;
  const uint32_t mode = ctx.r5.u32;
  __imp__sub_82081FE8(ctx, base);
  // The original UI, campaign and stage music call this with ADX slot 0.
  if (slot == 0 && asset >> 16 == 2 && ctx.r3.s32 >= 0) {
    REXLOG_INFO("[B3 audio fonte] ADX slot={} faixa={} modo={}", slot, asset & 0xffff, mode);
  }
}
