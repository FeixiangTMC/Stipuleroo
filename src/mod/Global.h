#pragma once

#include "mc/world/level/GameType.h"

class Player;

// 灵魂出窍状态标记
extern bool     g_FreeCamEnabled;
extern GameType g_OriginalGameType;
extern Player*  g_FreeCamPlayer;

// 自动工具状态
extern bool g_AutoToolEnabled;

// 伪潜行状态
extern bool g_FakeSneakEnabled;

// 夜视状态
extern bool g_NightVisionEnabled;

// 自动搭路状态
extern bool g_AutoBridgeEnabled;
extern bool g_AutoBridgePending; // /ab 后等待 B 键确认

namespace Stipuleroo {
extern void EnableFreeCamera(Player* pl);
extern void DisableFreeCamera(Player* pl);
extern void freecameraHook(bool enable);
extern void autoToolHook(bool enable);
extern void fakeSneakHook(bool enable);
extern void nightVisionHook(bool enable);
extern void autoBridgeHook(bool enable);
} // namespace Stipuleroo
