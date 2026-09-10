#include "Entry.h"
#include "Config.h"
#include "Global.h"

#include "ll/api/io/Logger.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/command/ClientCommandRegisterEvent.h"
#include "ll/api/event/command/ServerCommandRegisterEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/input/KeyRegistry.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/packet/AvailableCommandsPacket.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandRegistry.h"
#include "mc/world/actor/player/Player.h"

#include <atomic>
#include <memory>

namespace Stipuleroo {
namespace {

std::atomic_bool gCmdRegistered{false};
CommandRegistry* gRegisteredRegistry{}; // 上次成功注册所用的客户端命令注册表实例（仅作比较，不解引用）

// 统一注册全部命令。仅在客户端命令注册表可用时调用。
void registerClientCommands() {
    auto& registrar = ll::command::CommandRegistrar::getClientInstance();

    // 注册 /fc 命令
    {
        auto& fcKey = StipulerooConfig::get().freecamKey;
        auto& fcCmd = registrar.getOrCreateCommand(
            "fc",
            fcKey.empty() ? "开启或关闭灵魂出窍模式。"
                          : fmt::format("开启或关闭灵魂出窍模式（快捷键: {}）。", fcKey),
            CommandPermissionLevel::Any
        );

        fcCmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
            auto* entity = origin.getEntity();
            if (entity && entity->isPlayer()) {
                auto* pl = static_cast<Player*>(entity);
                if (!g_FreeCamEnabled) {
                    Stipuleroo::EnableFreeCamera(pl);
                    return output.success("灵魂出窍模式已启用。");
                } else {
                    Stipuleroo::DisableFreeCamera(pl);
                    return output.success("灵魂出窍模式已禁用。");
                }
            }
            return output.error("该命令只能由玩家使用");
        });
    }

    // 注册 /at 命令
    {
        auto& atKey = StipulerooConfig::get().autoToolKey;
        auto& atCmd = registrar.getOrCreateCommand(
            "at",
            atKey.empty() ? "开启或关闭自动切换工具。"
                          : fmt::format("开启或关闭自动切换工具（快捷键: {}）。", atKey),
            CommandPermissionLevel::Any
        );

        atCmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
            g_AutoToolEnabled = !g_AutoToolEnabled;
            if (g_AutoToolEnabled) {
                return output.success("自动工具切换已启用。");
            } else {
                return output.success("自动工具切换已禁用。");
            }
        });
    }

    // 注册 /fs 命令 (隐藏)
    {
        auto& fsKey = StipulerooConfig::get().fakeSneakKey;
        auto& fsCmd = registrar.getOrCreateCommand(
            "fs",
            fsKey.empty() ? "开启或关闭伪潜行。"
                          : fmt::format("开启或关闭伪潜行（快捷键: {}）。", fsKey),
            CommandPermissionLevel::Internal
        );

        fsCmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
            g_FakeSneakEnabled = !g_FakeSneakEnabled;
            if (g_FakeSneakEnabled) {
                return output.success("伪潜行已启用。");
            } else {
                return output.success("伪潜行已禁用。");
            }
        });
    }

    // 注册 /rv 命令
    {
        auto& rvKey = StipulerooConfig::get().nightVisionKey;
        auto& rvCmd = registrar.getOrCreateCommand(
            "rv",
            rvKey.empty() ? "开启或关闭夜视。"
                          : fmt::format("开启或关闭夜视（快捷键: {}）。", rvKey),
            CommandPermissionLevel::Any
        );

        rvCmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
            g_NightVisionEnabled = !g_NightVisionEnabled;
            if (g_NightVisionEnabled) {
                return output.success("夜视已启用。");
            } else {
                return output.success("夜视已禁用。");
            }
        });
    }

    // 注册 /ab 命令
    {
        auto& abKey = StipulerooConfig::get().autoBridgeKey;
        auto& abCmd = registrar.getOrCreateCommand(
            "ab",
            abKey.empty() ? "开启或关闭自动搭路。"
                          : fmt::format("开启或关闭自动搭路（快捷键: {}）。", abKey),
            CommandPermissionLevel::Any
        );

        abCmd.overload().execute([](CommandOrigin const&, CommandOutput& output) {
            g_AutoBridgeEnabled = !g_AutoBridgeEnabled;
            if (g_AutoBridgeEnabled) {
                return output.success("自动搭路已启用。");
            } else {
                return output.success("自动搭路已禁用。");
            }
        });
    }

    // 注册 /am 命令（自动鼠标操作 → 弹出 4 选项表单，按钮里带各自快捷键）
    {
        auto& amCmd = registrar.getOrCreateCommand(
            "am",
            "自动鼠标操作：连续左键/右键点击、持续左键/右键长按。",
            CommandPermissionLevel::Any
        );

        amCmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
            auto* entity = origin.getEntity();
            if (!entity || !entity->isPlayer()) {
                return output.error("该命令只能由玩家使用");
            }
            Stipuleroo::showAutoMouseForm(*static_cast<Player*>(entity));
            return output.success("已打开自动鼠标操作菜单。");
        });
    }
}

// 为“当前”客户端命令注册表注册全部命令。
//
// ★ 为什么只能在 LL 的 ClientCommandRegisterEvent 里调用（且用默认优先级）：
//   CommandRegistrar 会缓存 CommandHandle，而每个 handle 内部持有指向游戏
//   CommandRegistry::Signature 的**引用**（CommandHandle::Impl::signature）。
//   离开世界 / 重进世界时游戏会重建或重哈希客户端命令注册表，旧 handle 的
//   引用随即悬空；此时再 getOrCreateCommand() 会命中缓存里的旧 handle，
//   接着 overload().execute() → registerOverload() 就在已释放内存上崩溃
//   （2026-09-10 20:05 的 0xC0000005 崩溃栈：
//    CommandHandle::registerOverload ← OverloadData::setFactory ← 本模组注册命令）。
//   LL 内置命令的监听器（BuiltinCommands.cpp）在每个事件开头都会
//   `registrar.clear()`，把上一代的 handle 缓存清空；我们的监听器注册得更晚、
//   优先级相同 → 一定在 LL 的 clear() 之后执行，因此拿到的是绑定当前注册表的
//   全新 handle。（这也是为什么绝不能自己另开一条更早的注册路径。）
void registerClientCommandsForCurrentRegistry() {
    auto registryRef = ll::service::getCommandRegistry(true);
    if (!registryRef) {
        gCmdRegistered      = false;
        gRegisteredRegistry = nullptr;
        return;
    }
    CommandRegistry* registry = std::addressof(*registryRef);
    if (gRegisteredRegistry == registry) {
        return; // 同一个注册表实例只注册一次（游戏侧保存的是闭包副本，命令不会失效）
    }
    registerClientCommands();
    gRegisteredRegistry = registry;
    gCmdRegistered      = true;
    SROO_DEBUG("ClientCommandRegister: 6 commands registered (registry={})", static_cast<void*>(registry));
}

} // namespace (anon)

// ============================================================
//  退出/进入世界时的状态复位 + 自持 Hook（旁路 EventBus 兜底）
// ============================================================
// 背景: 早期实测本客户端上部分 EventBus 事件不投递（命令注册因此改为直接 hook
//   ClientInstance::onPlayerLoaded / onLevelExit 作为兜底）。命令注册现由 LL 的
//   ClientCommandRegisterEvent 负责（见 enable()），tick 兜底仍在。
namespace {

// 退出世界 / 进入世界：把所有功能恢复为关闭，避免残留状态影响下一个世界。
void resetAllStates() {
    Stipuleroo::ForceDisableFreeCameraNow(); // 同步还原相机（不等渲染帧），内部自带状态清理
    Stipuleroo::ForceStopAutoMouse();        // 同步结束自动鼠标操作（含取消正在进行的挖掘）
    g_FreeCamEnabled     = false;
    g_AutoToolEnabled    = false;
    g_FakeSneakEnabled   = false;
    g_NightVisionEnabled = false;
    g_AutoBridgeEnabled  = false;
    // 注意: 不清 gCmdRegistered/gRegisteredRegistry —— 同一个注册表实例只注册一次，
    // 换新实例（重进世界时游戏重建注册表）由 ClientCommandRegisterEvent 再注册一次。
}

} // namespace (anon2)

// ============================================================
//  配置热重载（局内修改 config.json 即时生效）
// ============================================================
// 每 ~2 秒检查一次配置文件的时间戳/大小；变化则重新解析并应用。
// 快捷键 / 鼠标间隔都是"每次使用时现读配置"，所以重载后立刻生效；
// debugLog 也是重载后立即生效。反馈走客户端聊天栏，不需要看日志。
namespace {

std::filesystem::file_time_type gCfgWriteTime{};
std::uintmax_t                  gCfgSize{};
bool                            gCfgWatchInit{};

void notifyClient(std::string const& msg) {
    auto  ci     = ll::service::getClientInstance();
    auto* player = ci ? ci->getLocalPlayer() : nullptr;
    if (!player) return;
    // 客户端本地玩家没有网络连接，displayClientMessage 不一定能进聊天栏；
    // 优先用同一玩家的服务端对象（集成服务器），拿不到再退回本地玩家。
    Player* target = player;
    if (auto level = ll::service::getLevel(); level.has_value()) {
        if (auto* serverPlayer = level->getPlayer(player->getUuid()); serverPlayer) {
            target = serverPlayer;
        } else if (auto* byName = level->getPlayer(player->getRealName()); byName) {
            target = byName;
        }
    }
    target->displayClientMessage(msg, std::nullopt);
}

void checkConfigHotReload(std::filesystem::path const& path) {
    std::error_code ec;
    auto            writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) return;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return;

    if (!gCfgWatchInit) { // 首次只记录基线，避免启动时误报
        gCfgWatchInit = true;
        gCfgWriteTime = writeTime;
        gCfgSize      = size;
        return;
    }
    if (writeTime == gCfgWriteTime && size == gCfgSize) return;

    gCfgWriteTime = writeTime;
    gCfgSize      = size;

    if (StipulerooConfig::get().reload(path)) {
        Stipuleroo::g_debugLog = StipulerooConfig::get().debugLog;
        SROO_DEBUG("Config: hot reloaded from {}", path.string());
        notifyClient("§b[Stipuleroo] §rconfig.json 已热重载：快捷键 / 鼠标间隔 / debugLog 立即生效");
    } else {
        SROO_DEBUG("Config: hot reload failed (JSON 解析失败或文件不可读)");
        notifyClient("§c[Stipuleroo] §rconfig.json 热重载失败（JSON 格式有误，已保留原配置）");
    }
}

} // namespace (anon3)

LL_TYPE_INSTANCE_HOOK(
    StipulerooJoinHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$onPlayerLoaded,
    void,
    Player& player
) {
    resetAllStates();
    SROO_DEBUG("JoinHook: onPlayerLoaded fired, states reset");
    origin(player);
}

// 退出世界 → 复位 (旁路 EventBus: ClientExitLevelEvent 收不到)
LL_TYPE_INSTANCE_HOOK(
    StipulerooExitHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$onLevelExit,
    void
) {
    resetAllStates();
    SROO_DEBUG("ExitHook: onLevelExit fired, states reset");
    origin();
}

struct EntryHooksImpl {
    ll::memory::HookRegistrar<StipulerooJoinHook, StipulerooExitHook> r;
};

std::unique_ptr<EntryHooksImpl> gEntryHooks;

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

bool Entry::load() { return true; }

bool Entry::enable() {
    // 0. 加载配置文件
    auto configPath = mSelf.getModDir() / "config" / "config.json";
    StipulerooConfig::get().load(configPath);

    // 0.0 诊断日志开关：正式版默认静默（config.json 里把 "debugLog" 设为 true 可临时打开）
    Stipuleroo::g_debugLog = StipulerooConfig::get().debugLog;
    SROO_DEBUG("Stipuleroo: 诊断日志已启用 (config debugLog=true)");

    // 0.1 注册快捷键（仅已配置的才注册）
    {
        auto& keyReg = ll::input::KeyRegistry::getInstance();
        auto  fcCode = StipulerooConfig::get().getFreecamKeyCode();
        auto  atCode = StipulerooConfig::get().getAutoToolKeyCode();
        auto  fsCode = StipulerooConfig::get().getFakeSneakKeyCode();
        auto  nvCode = StipulerooConfig::get().getNightVisionKeyCode();
        auto  abCode = StipulerooConfig::get().getAutoBridgeKeyCode();
        auto  amLc   = StipulerooConfig::get().getAutoMouseLeftClickKeyCode();
        auto  amRc   = StipulerooConfig::get().getAutoMouseRightClickKeyCode();
        auto  amHl   = StipulerooConfig::get().getAutoMouseHoldLeftKeyCode();
        if (fcCode > 0) keyReg.getOrCreateKey("Stipuleroo Freecam",     {fcCode});
        if (atCode > 0) keyReg.getOrCreateKey("Stipuleroo AutoTool",    {atCode});
        if (fsCode > 0) keyReg.getOrCreateKey("Stipuleroo FakeSneak",   {fsCode});
        if (nvCode > 0) keyReg.getOrCreateKey("Stipuleroo NightVision", {nvCode});
        if (abCode > 0) keyReg.getOrCreateKey("Stipuleroo AutoBridge",  {abCode});
        if (amLc > 0) keyReg.getOrCreateKey("Stipuleroo AutoMouseLeftClick",  {amLc});
        if (amRc > 0) keyReg.getOrCreateKey("Stipuleroo AutoMouseRightClick", {amRc});
        if (amHl > 0) keyReg.getOrCreateKey("Stipuleroo AutoMouseHoldLeft",   {amHl});
    }

    // 0.2 灵魂出窍快捷键 — 和 /fc 命令平等，直接开关（有 UI 时不触发）
    mFreecamKeyListener = ll::event::EventBus::getInstance()
        .emplaceListener<ll::event::input::KeyInputEvent>(
            [](ll::event::input::KeyInputEvent& ev) {
                if (!ev.isDown()) return;
                if (ev.keyCode() != StipulerooConfig::get().getFreecamKeyCode()) return;
                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                if (ci->getScreenName() != "hud_screen") return;
                auto* lp = ci->getLocalPlayer();
                if (!lp) return;
                if (!g_FreeCamEnabled) {
                    Stipuleroo::EnableFreeCamera(lp);
                } else {
                    Stipuleroo::DisableFreeCamera(lp);
                }
            }
        );

    // 0.3 自动工具快捷键 — 和 /at 命令平等，直接开关（有 UI 时不触发）
    mAutoToolKeyListener = ll::event::EventBus::getInstance()
        .emplaceListener<ll::event::input::KeyInputEvent>(
            [](ll::event::input::KeyInputEvent& ev) {
                if (!ev.isDown()) return;
                if (ev.keyCode() != StipulerooConfig::get().getAutoToolKeyCode()) return;
                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                if (ci->getScreenName() != "hud_screen") return;
                auto* lp = ci->getLocalPlayer();
                if (!lp) return;
                g_AutoToolEnabled = !g_AutoToolEnabled;
            }
        );

    // 0.4 伪潜行快捷键 — 和 /fs 命令平等，直接开关（有 UI 时不触发）
    mFakeSneakKeyListener = ll::event::EventBus::getInstance()
        .emplaceListener<ll::event::input::KeyInputEvent>(
            [](ll::event::input::KeyInputEvent& ev) {
                if (!ev.isDown()) return;
                if (ev.keyCode() != StipulerooConfig::get().getFakeSneakKeyCode()) return;
                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                if (ci->getScreenName() != "hud_screen") return;
                auto* lp = ci->getLocalPlayer();
                if (!lp) return;
                g_FakeSneakEnabled = !g_FakeSneakEnabled;
            }
        );

    // 0.5 夜视快捷键 — 和 /rv 命令平等，直接开关（有 UI 时不触发）
    mNightVisionKeyListener = ll::event::EventBus::getInstance()
        .emplaceListener<ll::event::input::KeyInputEvent>(
            [](ll::event::input::KeyInputEvent& ev) {
                if (!ev.isDown()) return;
                if (ev.keyCode() != StipulerooConfig::get().getNightVisionKeyCode()) return;
                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                if (ci->getScreenName() != "hud_screen") return;
                auto* lp = ci->getLocalPlayer();
                if (!lp) return;
                g_NightVisionEnabled = !g_NightVisionEnabled;
            }
        );

    // 0.6 自动搭路快捷键 — 和 /ab 命令平等，直接开关（有 UI 时不触发）
    mAutoBridgeKeyListener = ll::event::EventBus::getInstance()
        .emplaceListener<ll::event::input::KeyInputEvent>(
            [](ll::event::input::KeyInputEvent& ev) {
                if (!ev.isDown()) return;
                if (ev.keyCode() != StipulerooConfig::get().getAutoBridgeKeyCode()) return;
                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                if (ci->getScreenName() != "hud_screen") return;
                auto* lp = ci->getLocalPlayer();
                if (!lp) return;
                g_AutoBridgeEnabled = !g_AutoBridgeEnabled;
            }
        );

    // 0.7 自动鼠标操作快捷键 — 4 个独立快捷键，和 /am 菜单平等（有 UI 时不触发）
    mAutoMouseKeyListener = ll::event::EventBus::getInstance()
        .emplaceListener<ll::event::input::KeyInputEvent>(
            [](ll::event::input::KeyInputEvent& ev) {
                if (!ev.isDown()) return;
                auto& cfg  = StipulerooConfig::get();
                int   code = ev.keyCode();

                AutoMouseMode mode = AutoMouseMode::Off;
                if (code == cfg.getAutoMouseLeftClickKeyCode()) {
                    mode = AutoMouseMode::LeftClick;
                } else if (code == cfg.getAutoMouseRightClickKeyCode()) {
                    mode = AutoMouseMode::RightClick;
                } else if (code == cfg.getAutoMouseHoldLeftKeyCode()) {
                    mode = AutoMouseMode::HoldLeft;
                } else {
                    return;
                }

                auto ci = ll::service::getClientInstance();
                if (!ci) return;
                if (ci->getScreenName() != "hud_screen") return;
                if (!ci->getLocalPlayer()) return;
                Stipuleroo::ToggleAutoMouseMode(mode);
            }
        );

    // 1. 安装 Hook
    Stipuleroo::freecameraHook(true);
    Stipuleroo::autoToolHook(true);
    Stipuleroo::fakeSneakHook(true);
    Stipuleroo::nightVisionHook(true);
    Stipuleroo::autoBridgeHook(true);
    Stipuleroo::autoMouseHook(true);

    auto& bus = ll::event::EventBus::getInstance();
    if (!gEntryHooks) {
        gEntryHooks = std::make_unique<EntryHooksImpl>();
    }

    // 2. 客户端加入世界 → 自动退出灵魂出窍 + 关闭所有功能（状态复位）
    mJoinLevelListener = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
        [](ll::event::client::ClientJoinLevelEvent&) {
            resetAllStates();
            SROO_DEBUG("ClientJoinLevelEvent: state reset");
        }
    );

    // 2.1 客户端命令注册（唯一路径）— LL 在 CommandRegistry::loadRemoteCommands hook 中发布。
    //     必须用默认优先级（Normal）：LL 内置命令的监听器注册得更早、同为 Normal，
    //     会先执行并把上一代 CommandHandle 缓存 clear() 掉，我们随后拿到的是新 handle。
    mClientCmdListener = bus.emplaceListener<ll::event::command::ClientCommandRegisterEvent>(
        [](ll::event::command::ClientCommandRegisterEvent&) {
            SROO_DEBUG("ClientCommandRegisterEvent fired");
            registerClientCommandsForCurrentRegistry();
        }
    );

    // 2.2 客户端世界 tick —— 配置热重载 + 命令注册兜底
    //     仅在“从未注册成功过”（事件始终没投递）时兜底一次；已注册则完全不介入，
    //     避免复用可能悬空的 handle。
    mTickListener = bus.emplaceListener<ll::event::world::ClientLevelTickEvent>(
        [configPath](ll::event::world::ClientLevelTickEvent&) {
            static int tickCount = 0;
            ++tickCount;
            if ((tickCount % 40) == 0) { // 每 ~2 秒检查一次配置是否被改动（局内热重载）
                checkConfigHotReload(configPath);
            }
            if ((tickCount % 20) != 0) return;
            if (gCmdRegistered.load()) return;
            if (!ll::service::getCommandRegistry(true)) return;
            SROO_DEBUG("ClientLevelTick: 命令注册兜底触发");
            registerClientCommandsForCurrentRegistry();
        }
    );

    // 2.3 服务端命令注册事件（诊断：判断集成服务器/命令源是否在跑）
    mServerCmdListener = bus.emplaceListener<ll::event::command::ServerCommandRegisterEvent>(
        [](ll::event::command::ServerCommandRegisterEvent&) {
            SROO_DEBUG("ServerCommandRegisterEvent fired");
        }
    );

    // 3. 死亡 → 自动退出（灵魂出窍 + 自动鼠标操作）
    mDieListener = bus.emplaceListener<ll::event::player::PlayerDieEvent>(
        [](ll::event::player::PlayerDieEvent& ev) {
            Stipuleroo::ForceStopAutoMouse();
            if (g_FreeCamEnabled) {
                Stipuleroo::DisableFreeCamera(&ev.self());
            }
        }
    );

    // 4. 退出世界 → 所有功能复位（同步还原相机，防止残留状态卡住下个世界）
    mExitLevelListener = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>(
        [](ll::event::client::ClientExitLevelEvent&) {
            resetAllStates();
            SROO_DEBUG("ClientExitLevelEvent: state reset");
        }
    );

    return true;
}

bool Entry::disable() {
    resetAllStates(); // 关闭全部功能（同步还原灵魂出窍相机 + 结束自动鼠标操作）
    gEntryHooks.reset();
    Stipuleroo::autoToolHook(false);
    Stipuleroo::fakeSneakHook(false);
    Stipuleroo::nightVisionHook(false);
    Stipuleroo::autoBridgeHook(false);
    Stipuleroo::autoMouseHook(false);
    return true;
}

bool Entry::unload() {
    resetAllStates(); // 关闭全部功能（同步还原灵魂出窍相机 + 结束自动鼠标操作）
    gEntryHooks.reset();
    Stipuleroo::autoToolHook(false);
    Stipuleroo::fakeSneakHook(false);
    Stipuleroo::nightVisionHook(false);
    Stipuleroo::autoBridgeHook(false);
    Stipuleroo::autoMouseHook(false);
    auto& bus = ll::event::EventBus::getInstance();
    bus.removeListener(mJoinLevelListener);
    bus.removeListener(mClientCmdListener);
    bus.removeListener(mTickListener);
    bus.removeListener(mServerCmdListener);
    bus.removeListener(mDieListener);
    bus.removeListener(mExitLevelListener);
    bus.removeListener(mFreecamKeyListener);
    bus.removeListener(mAutoToolKeyListener);
    bus.removeListener(mFakeSneakKeyListener);
    bus.removeListener(mNightVisionKeyListener);
    bus.removeListener(mAutoBridgeKeyListener);
    bus.removeListener(mAutoMouseKeyListener);
    Stipuleroo::freecameraHook(false);
    return true;
}

} // namespace Stipuleroo

LL_REGISTER_MOD(Stipuleroo::Entry, Stipuleroo::Entry::getInstance());
