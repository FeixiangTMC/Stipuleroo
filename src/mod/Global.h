#pragma once

class Player;

// 灵魂出窍状态标记
extern bool g_FreeCamEnabled;

// 自动工具状态
extern bool g_AutoToolEnabled;

// 伪潜行状态
extern bool g_FakeSneakEnabled;

// 夜视状态
extern bool g_NightVisionEnabled;

// 自动搭路状态
extern bool g_AutoBridgeEnabled;

namespace Stipuleroo {
extern void EnableFreeCamera(Player* pl);
extern void DisableFreeCamera(Player* pl);
extern void freecameraHook(bool enable);
extern void autoToolHook(bool enable);
extern void fakeSneakHook(bool enable);
extern void nightVisionHook(bool enable);
extern void autoBridgeHook(bool enable);
} // namespace Stipuleroo
