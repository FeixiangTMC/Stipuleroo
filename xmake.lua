add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("server", "client")
option_end()

add_requires("levilamina 26.40.*", {configs = {target_type = get_config("target_type")}})

add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

-- ============================================================
-- 26.40 起：必须用 clang-cl 编译本模组（关键！）
--   26.40 客户端与 LeviLamina 均由 clang/LLVM 构建（游戏 exe 内含
--   "const char *entt::internal::pretty_function() [Type = X]" 形式的类型名串）。
--   entt 的组件类型 ID = FNV-1a32(去修饰后的类型名)，而类型名来自编译器内置宏：
--     clang : __PRETTY_FUNCTION__ -> "MinecraftCamera::CameraComponent"
--     MSVC  : __FUNCSIG__         -> "struct MinecraftCamera::CameraComponent"（多 "struct "）
--   两者哈希不同 => 模组看不到游戏创建的任何组件（相机实体 CameraComponent /
--   玩家 StateVectorComponent 等），灵魂出窍等 ECS 相关功能全部静默失效。
--   （26.10 客户端为 MSVC 构建，所以当时 MSVC 编译的模组是匹配的。）
-- ============================================================
if is_plat("windows") then
    -- 注意：clang-cl 必须在 PATH 中（仓库根目录的 build.ps1 会自动把 LLVM 的 bin 目录加进去）
    set_toolchains("clang-cl")
end

target("Stipuleroo")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_defines("NOMINMAX", "UNICODE")
    -- debug 构建默认输出诊断日志；release（正式版）默认静默，
    -- 需要排查时把 config/config.json 的 "debugLog" 设为 true（见 src/mod/Log.h）
    if is_mode("debug") then
        add_defines("STIPULEROO_DEBUG_BUILD")
    end
    add_packages("levilamina", "nlohmann_json")
    add_syslinks("user32") -- GetAsyncKeyState（自动鼠标输入层模拟需要）
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_files("src/**.cpp")
    add_includedirs("src")
