#include "Config.h"

#include "ll/api/Config.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"

#include <Windows.h>
#include <cmath>
#include <unordered_map>

// 该文件独立于 Global.h（避免 Config 反向依赖），日志直接用原生 Logger
#define SROO_ERROR(...) ::ll::mod::NativeMod::current()->getLogger().error(__VA_ARGS__)

StipulerooConfig& StipulerooConfig::get() {
    static StipulerooConfig instance;
    return instance;
}

// 点击间隔（秒）夹紧：有效范围 0.05 ~ 1000，非法值回落到默认 0.2
static double clampInterval(double v) {
    if (!std::isfinite(v)) return 0.20;
    if (v < 0.05) return 0.05;
    if (v > 1000.0) return 1000.0;
    return v;
}

static int intervalToMs(double seconds) {
    double const s = clampInterval(seconds);
    return static_cast<int>(s * 1000.0 + 0.5);
}

// 把一个 JSON 文本解析进配置对象；成功返回 true（缺省项保持对象原有取值）
// 允许 // 与 /* */ 注释（ignore_comments = true），因此生成的文件可以带说明注释。
// 文件里出现未知字段（例如早期版本的 configVersion）会被直接忽略，不影响解析。
static bool parseInto(StipulerooConfig& cfg, std::string const& content) {
    try {
        auto j = nlohmann::ordered_json::parse(content, nullptr, true, true);
        if (j.contains("freecamKey") && j["freecamKey"].is_string()) {
            cfg.freecamKey = j["freecamKey"].get<std::string>();
        }
        if (j.contains("autoToolKey") && j["autoToolKey"].is_string()) {
            cfg.autoToolKey = j["autoToolKey"].get<std::string>();
        }
        if (j.contains("fakeSneakKey") && j["fakeSneakKey"].is_string()) {
            cfg.fakeSneakKey = j["fakeSneakKey"].get<std::string>();
        }
        if (j.contains("nightVisionKey") && j["nightVisionKey"].is_string()) {
            cfg.nightVisionKey = j["nightVisionKey"].get<std::string>();
        }
        if (j.contains("autoBridgeKey") && j["autoBridgeKey"].is_string()) {
            cfg.autoBridgeKey = j["autoBridgeKey"].get<std::string>();
        }
        if (j.contains("debugLog") && j["debugLog"].is_boolean()) {
            cfg.debugLog = j["debugLog"].get<bool>();
        }
        if (j.contains("autoMouseLeftClickKey") && j["autoMouseLeftClickKey"].is_string()) {
            cfg.autoMouseLeftClickKey = j["autoMouseLeftClickKey"].get<std::string>();
        }
        if (j.contains("autoMouseRightClickKey") && j["autoMouseRightClickKey"].is_string()) {
            cfg.autoMouseRightClickKey = j["autoMouseRightClickKey"].get<std::string>();
        }
        if (j.contains("autoMouseHoldLeftKey") && j["autoMouseHoldLeftKey"].is_string()) {
            cfg.autoMouseHoldLeftKey = j["autoMouseHoldLeftKey"].get<std::string>();
        }
        if (j.contains("autoMouseLeftClickInterval") && j["autoMouseLeftClickInterval"].is_number()) {
            cfg.autoMouseLeftClickInterval = clampInterval(j["autoMouseLeftClickInterval"].get<double>());
        }
        if (j.contains("autoMouseRightClickInterval") && j["autoMouseRightClickInterval"].is_number()) {
            cfg.autoMouseRightClickInterval = clampInterval(j["autoMouseRightClickInterval"].get<double>());
        }
        return true;
    } catch (...) {
        return false;
    }
}

void StipulerooConfig::load(std::filesystem::path const& path) {
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path.parent_path());
        save(path); // 首次生成：带注释的模板
        return;
    }
    auto content = ll::file_utils::readFile(path);
    if (!content || content->empty()) return;
    parseInto(*this, *content); // 解析失败：保持默认值，不覆盖用户的文件
}

// 局内热重载：以文件为唯一来源（先回到默认值再解析），失败时保持当前配置不变
bool StipulerooConfig::reload(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    auto content = ll::file_utils::readFile(path);
    if (!content || content->empty()) return false;
    StipulerooConfig fresh{}; // 默认值
    if (!parseInto(fresh, *content)) return false;
    *this = fresh;
    return true;
}

// JSON 字符串转义（键名只会是简单标识符，但用户可能手改成别的字符）
static std::string jsonEscape(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out;
}

// 写出带 // 注释、真正换行的配置文件（不是把说明塞进一个 _comment 字符串里）。
// 解析端用 nlohmann 的 ignore_comments 读回，所以这是合法的“JSONC”。
// 注释策略：文件头两行说清规则，每个字段后面跟一句行尾注释 —— 尽量短，方便直接改值。
// 写法说明：中间每条字段后带逗号，最后一条（autoMouseRightClickInterval）不带
// （JSON 不允许尾随逗号，nlohmann 也不接受）。
void StipulerooConfig::save(std::filesystem::path const& path) const {
    std::string t;
    t.reserve(2048);

    t += "// Stipuleroo 配置（支持 // 注释；游戏内改完约 2 秒自动生效，聊天栏会提示）\n";
    t += "// 键名：A~Z 0~9 F1~F12 Space Tab Enter Shift Ctrl Alt（可加 L/R 前缀）；留空 \"\" = 不绑定\n";
    t += "{\n";
    t += fmt::format("    \"debugLog\": {},", debugLog ? "true" : "false");
    t += "                       // 输出诊断日志到 logs/latest.log\n";
    t += fmt::format("    \"freecamKey\": \"{}\",", jsonEscape(freecamKey));
    t += "                   // 灵魂出窍      /fc\n";
    t += fmt::format("    \"autoToolKey\": \"{}\",", jsonEscape(autoToolKey));
    t += "                  // 自动工具      /at\n";
    t += fmt::format("    \"fakeSneakKey\": \"{}\",", jsonEscape(fakeSneakKey));
    t += "                 // 伪潜行        /fs（无效果）\n";
    t += fmt::format("    \"nightVisionKey\": \"{}\",", jsonEscape(nightVisionKey));
    t += "               // 夜视          /rv\n";
    t += fmt::format("    \"autoBridgeKey\": \"{}\",", jsonEscape(autoBridgeKey));
    t += "                // 自动搭路      /ab\n";
    t += fmt::format("    \"autoMouseLeftClickKey\": \"{}\",", jsonEscape(autoMouseLeftClickKey));
    t += "       // 自动鼠标      /am：连续左键点击（攻击）\n";
    t += fmt::format("    \"autoMouseRightClickKey\": \"{}\",", jsonEscape(autoMouseRightClickKey));
    t += "      // 自动鼠标      /am：连续右键点击（放置/使用）\n";
    t += fmt::format("    \"autoMouseHoldLeftKey\": \"{}\",", jsonEscape(autoMouseHoldLeftKey));
    t += "        // 自动鼠标      /am：持续左键长按（挖掘）\n";
    t += fmt::format("    \"autoMouseLeftClickInterval\": {:.2f},", autoMouseLeftClickInterval);
    t += "    // 左键点击间隔（秒，0.05~1000）\n";
    t += fmt::format("    \"autoMouseRightClickInterval\": {:.2f}", autoMouseRightClickInterval);
    t += "   // 右键点击间隔（秒，0.05~1000）\n";
    t += "}\n";

    ll::file_utils::writeFile(path, t);

    // 自检：确认刚写出的带注释模板仍能被解析（防止模板笔误导致下次启动配置失效）
    if (auto written = ll::file_utils::readFile(path)) {
        StipulerooConfig probe{};
        if (!parseInto(probe, *written)) {
            SROO_ERROR("Config: 生成的 config.json 无法解析，请反馈该问题（模板格式错误）");
        }
    }
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

int StipulerooConfig::getFreecamKeyCode() const     { return KeyNameToCode(freecamKey); }
int StipulerooConfig::getAutoToolKeyCode() const     { return KeyNameToCode(autoToolKey); }
int StipulerooConfig::getFakeSneakKeyCode() const    { return KeyNameToCode(fakeSneakKey); }
int StipulerooConfig::getNightVisionKeyCode() const  { return KeyNameToCode(nightVisionKey); }
int StipulerooConfig::getAutoBridgeKeyCode() const   { return KeyNameToCode(autoBridgeKey); }

int StipulerooConfig::getAutoMouseLeftClickKeyCode() const  { return KeyNameToCode(autoMouseLeftClickKey); }
int StipulerooConfig::getAutoMouseRightClickKeyCode() const { return KeyNameToCode(autoMouseRightClickKey); }
int StipulerooConfig::getAutoMouseHoldLeftKeyCode() const   { return KeyNameToCode(autoMouseHoldLeftKey); }

int StipulerooConfig::getAutoMouseLeftClickIntervalMs() const  { return intervalToMs(autoMouseLeftClickInterval); }
int StipulerooConfig::getAutoMouseRightClickIntervalMs() const { return intervalToMs(autoMouseRightClickInterval); }
