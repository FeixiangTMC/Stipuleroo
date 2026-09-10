#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct StipulerooConfig {
    std::string freecamKey     = ""; // 灵魂出窍快捷键
    std::string autoToolKey    = ""; // 自动工具快捷键
    std::string fakeSneakKey   = ""; // 伪潜行快捷键
    std::string nightVisionKey = ""; // 夜视快捷键
    std::string autoBridgeKey  = ""; // 自动搭路快捷键

    // ---- 自动鼠标操作（/am）----
    std::string autoMouseLeftClickKey  = ""; // 连续左键点击（攻击）
    std::string autoMouseRightClickKey = ""; // 连续右键点击（放置/使用）
    std::string autoMouseHoldLeftKey   = ""; // 持续左键长按（挖掘）

    // 点击间隔（秒），有效范围 0.05 ~ 1000（超出会被夹紧到边界）
    double autoMouseLeftClickInterval  = 0.20;
    double autoMouseRightClickInterval = 0.20;

#ifdef STIPULEROO_DEBUG_BUILD
    bool debugLog = true;  // 调试构建：默认输出诊断日志
#else
    bool debugLog = false; // 正式版：默认静默（排查问题时改为 true）
#endif

    static StipulerooConfig& get();

    void load(std::filesystem::path const& path);
    // 局内热重载：文件必须存在且能解析；成功时以文件内容为准（缺省项回落默认值）
    bool reload(std::filesystem::path const& path);
    void save(std::filesystem::path const& path) const;

    int getFreecamKeyCode() const;
    int getAutoToolKeyCode() const;
    int getFakeSneakKeyCode() const;
    int getNightVisionKeyCode() const;
    int getAutoBridgeKeyCode() const;

    int getAutoMouseLeftClickKeyCode() const;
    int getAutoMouseRightClickKeyCode() const;
    int getAutoMouseHoldLeftKeyCode() const;

    // 夹紧后的点击间隔（毫秒）：下限 50ms(0.05s)、上限 1000000ms(1000s)
    int getAutoMouseLeftClickIntervalMs() const;
    int getAutoMouseRightClickIntervalMs() const;
};
