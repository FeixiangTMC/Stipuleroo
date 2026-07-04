#include "Config.h"

#include "ll/api/Config.h"
#include "ll/api/io/FileUtils.h"

#include <Windows.h>
#include <unordered_map>

StipulerooConfig& StipulerooConfig::get() {
    static StipulerooConfig instance;
    return instance;
}

void StipulerooConfig::load(std::filesystem::path const& path) {
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path.parent_path());
        save(path);
        return;
    }
    auto content = ll::file_utils::readFile(path);
    if (!content || content->empty()) return;
    try {
        auto j = nlohmann::ordered_json::parse(*content);
        if (j.contains("freecamKey") && j["freecamKey"].is_string()) {
            freecamKey = j["freecamKey"].get<std::string>();
        }
        if (j.contains("autoBridgeKey") && j["autoBridgeKey"].is_string()) {
            autoBridgeKey = j["autoBridgeKey"].get<std::string>();
        }
    } catch (...) {}
}

void StipulerooConfig::save(std::filesystem::path const& path) const {
    nlohmann::ordered_json j;
    j["freecamKey"]    = freecamKey;
    j["autoBridgeKey"] = autoBridgeKey;
    j["_comment"]      = "键名支持: A~Z, 0~9, F1~F12, Space, Tab, Enter, Shift, Ctrl, Alt, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt";
    ll::file_utils::writeFile(path, j.dump(4));
}

static const std::unordered_map<std::string, int> kKeyNameMap = {
    {"Space",  VK_SPACE},
    {"Tab",    VK_TAB},
    {"Enter",  VK_RETURN},
    {"Return", VK_RETURN},
    {"Shift",  VK_LSHIFT},   {"LShift", VK_LSHIFT},   {"RShift", VK_RSHIFT},
    {"Ctrl",   VK_LCONTROL}, {"LCtrl",  VK_LCONTROL}, {"RCtrl",  VK_RCONTROL},
    {"Alt",    VK_LMENU},    {"LAlt",   VK_LMENU},    {"RAlt",   VK_RMENU},
    {"F1",  VK_F1},  {"F2",  VK_F2},  {"F3",  VK_F3},  {"F4",  VK_F4},
    {"F5",  VK_F5},  {"F6",  VK_F6},  {"F7",  VK_F7},  {"F8",  VK_F8},
    {"F9",  VK_F9},  {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
};

static int KeyNameToCode(std::string const& name) {
    if (name.empty()) return -1;
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') return (int)c;
        if (c >= 'a' && c <= 'z') return (int)toupper(c);
        if (c >= '0' && c <= '9') return (int)c;
        return -1;
    }
    auto it = kKeyNameMap.find(name);
    return it != kKeyNameMap.end() ? it->second : -1;
}

int StipulerooConfig::getAutoBridgeKeyCode() const { return KeyNameToCode(autoBridgeKey); }
int StipulerooConfig::getFreecamKeyCode() const    { return KeyNameToCode(freecamKey); }
