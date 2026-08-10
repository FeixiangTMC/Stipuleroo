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

    static StipulerooConfig& get();

    void load(std::filesystem::path const& path);
    void save(std::filesystem::path const& path) const;

    int getFreecamKeyCode() const;
    int getAutoToolKeyCode() const;
    int getFakeSneakKeyCode() const;
    int getNightVisionKeyCode() const;
    int getAutoBridgeKeyCode() const;
};
