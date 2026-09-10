#pragma once

#include "Log.h" // 统一日志入口（SROO_DEBUG / SROO_WARN / SROO_ERROR）

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

// ============================================================
//  自动鼠标操作（/am）— 三选一模式
// ============================================================
enum class AutoMouseMode : int {
    Off        = 0, // 关闭
    LeftClick  = 1, // 连续左键点击（攻击）
    RightClick = 2, // 连续右键点击（放置/使用）
    HoldLeft   = 3, // 持续左键长按（挖掘）
};

// 当前模式（同一时刻只会有一个模式生效）
extern AutoMouseMode g_AutoMouseMode;

namespace Stipuleroo {
extern void EnableFreeCamera(Player* pl);
extern void DisableFreeCamera(Player* pl);
// 立即（同步）关闭灵魂出窍：退出世界 / 加入世界 / 模组禁用时调用
extern void ForceDisableFreeCameraNow();
extern void freecameraHook(bool enable);
extern void autoToolHook(bool enable);
extern void fakeSneakHook(bool enable);
extern void nightVisionHook(bool enable);
extern void autoBridgeHook(bool enable);

// ---- 自动鼠标操作 ----
extern void autoMouseHook(bool enable);
extern void SetAutoMouseMode(AutoMouseMode mode);
extern void ToggleAutoMouseMode(AutoMouseMode mode);
// 立即（同步）结束自动操作（含取消正在进行的挖掘）：退出世界 / 死亡 / 禁用模组时调用
extern void ForceStopAutoMouse();
// 弹出 /am 的模式菜单（按钮文本里带各自的快捷键）
extern void showAutoMouseForm(Player& player);
} // namespace Stipuleroo
