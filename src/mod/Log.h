#pragma once

// ============================================================
//  统一日志入口（正式版静默）
//
//  · 正式（release）版默认 **不输出任何诊断日志**，只保留真正的 warn/error；
//  · 需要排查问题时：把 `config/config.json` 里的 `"debugLog"` 改成 `true`
//    即可临时打开（无需重新编译）；
//  · debug 构建（`xmake f -m debug`）默认打开诊断日志，方便开发期观察。
//
//  用法：诊断信息用 SROO_DEBUG(...)，真正的异常用 SROO_WARN/SROO_ERROR(...)。
// ============================================================
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"

namespace Stipuleroo {

#ifdef STIPULEROO_DEBUG_BUILD
inline bool g_debugLog = true;
#else
inline bool g_debugLog = false;
#endif

inline ll::io::Logger& log() { return ll::mod::NativeMod::current()->getLogger(); }

} // namespace Stipuleroo

// 诊断日志：正式版静默（运行期开关；关闭时只做一次 bool 判断，参数不会被格式化）
#define SROO_DEBUG(...)                            \
    do {                                           \
        if (::Stipuleroo::g_debugLog) {            \
            ::Stipuleroo::log().info(__VA_ARGS__); \
        }                                          \
    } while (0)

// 真正的异常情况：始终输出
#define SROO_WARN(...)  ::Stipuleroo::log().warn(__VA_ARGS__)
#define SROO_ERROR(...) ::Stipuleroo::log().error(__VA_ARGS__)
