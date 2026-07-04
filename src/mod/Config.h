#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct StipulerooConfig {
    std::string autoBridgeKey = "R";
    std::string freecamKey    = "C"; // 灵魂出窍快捷键

    static StipulerooConfig& get();

    void load(std::filesystem::path const& path);
    void save(std::filesystem::path const& path) const;

    int getAutoBridgeKeyCode() const;
    int getFreecamKeyCode() const;
};
