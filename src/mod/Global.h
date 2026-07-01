#pragma once

#include "mc/world/level/GameType.h"

class Player;

// 灵魂出窍状态标记
extern bool     g_FreeCamEnabled;
extern GameType g_OriginalGameType;
extern Player*  g_FreeCamPlayer;

// 自动工具状态
extern bool g_AutoToolEnabled;

namespace Stipuleroo {
extern void EnableFreeCamera(Player* pl);
extern void DisableFreeCamera(Player* pl);
extern void freecameraHook(bool enable);
extern void autoToolHook(bool enable);
} // namespace Stipuleroo
