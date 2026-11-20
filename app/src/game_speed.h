#pragma once

namespace b3 {
// True when the real-time delta was written for the current battle frame.
bool GameSpeedAppliedThisFrame();
// Guest phase byte (0x824BA110) published by the main loop; -1 until known.
int PublishedPhase();
// Phases where GAME TIME (delta + FPS cap) applies: battle, plus the world when b3_tempo_jogo_mundo is on.
bool TimedScene(int phase);
}
