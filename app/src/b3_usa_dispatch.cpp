#include "dbz3_pch.h"

#include <rex/hook.h>

// USA sub_820F2280 loads an event handler address into CTR. ReXGlue emits
// a switch on the numeric case index here, even with the targets registered.
REX_HOOK_RAW(sub_820F2280) {
  ctx.r10.u64 = ctx.r3.u32 << 2;
  ctx.r11.u64 = REX_LOAD_U32(0x8201E350u + ctx.r10.u32);
  ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
  if (ctx.cr6.eq) {
    return;
  }

  ctx.r3.u64 = ctx.r4.u64;
  ctx.ctr.u64 = ctx.r11.u64;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

// USA 0x820BB938 tail-branches through the two executable pointers at
// 0x82322B08. The emitted switch traps on a real destination (0x820BB958).
REX_HOOK_RAW(sub_820BB938) {
  ctx.r3.u64 = REX_LOAD_U32(ctx.r3.u32 + 48);
  ctx.r10.s64 = int16_t(REX_LOAD_U16(ctx.r3.u32 + 82));
  ctx.r10.u64 = ctx.r10.u32 << 2;
  ctx.r11.u64 = REX_LOAD_U32(0x82322B08u + ctx.r10.u32);
  ctx.ctr.u64 = ctx.r11.u64;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}
