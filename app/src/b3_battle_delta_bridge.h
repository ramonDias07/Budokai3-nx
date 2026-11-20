#pragma once
#include <cstdint>
namespace b3::timing {
void PublishBattleEffectiveDelta(uint32_t manager, uint32_t originalBits, uint8_t* base);
void PublishBattleEffectsDelta(uint32_t address, uint32_t originalBits, uint8_t* base);
}
