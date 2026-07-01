#pragma once

#include "mc/world/level/GameType.h"

class Player;

// 灵魂出窍状态标记
extern bool     g_FreeCamEnabled;
extern GameType g_OriginalGameType;
extern Player*  g_FreeCamPlayer;

namespace Stipuleroo {
extern void EnableFreeCamera(Player* pl);
extern void DisableFreeCamera(Player* pl);
extern void freecameraHook(bool enable);
} // namespace Stipuleroo
