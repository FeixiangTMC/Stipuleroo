#include "Entry.h"
#include "Config.h"
#include "Global.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/input/KeyRegistry.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"

namespace Stipuleroo {

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

bool Entry::load() { return true; }

bool Entry::enable() {
    // 0. 加载配置文件
    auto configPath = mSelf.getModDir() / "config" / "config.json";
    StipulerooConfig::get().load(configPath);

    // 0.1 注册快捷键（仅已配置的才注册）
    {
        auto& keyReg = ll::input::KeyRegistry::getInstance();
        auto  fcCode = StipulerooConfig::get().getFreecamKeyCode();
        auto  atCode = StipulerooConfig::get().getAutoToolKeyCode();
        auto  fsCode = StipulerooConfig::get().getFakeSneakKeyCode();
        auto  nvCode = StipulerooConfig::get().getNightVisionKeyCode();
        auto  abCode = StipulerooConfig::get().getAutoBridgeKeyCode();
        if (fcCode > 0) keyReg.getOrCreateKey("Stipuleroo Freecam",     {fcCode});
        if (atCode > 0) keyReg.getOrCreateKey("Stipuleroo AutoTool",    {atCode});
        if (fsCode > 0) keyReg.getOrCreateKey("Stipuleroo FakeSneak",   {fsCode});
        if (nvCode > 0) keyReg.getOrCreateKey("Stipuleroo NightVision", {nvCode});
        if (abCode > 0) keyReg.getOrCreateKey("Stipuleroo AutoBridge",  {abCode});
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

    // 1. 安装 Hook
    Stipuleroo::freecameraHook(true);
    Stipuleroo::autoToolHook(true);
    Stipuleroo::fakeSneakHook(true);
    Stipuleroo::nightVisionHook(true);
    Stipuleroo::autoBridgeHook(true);

    auto& bus = ll::event::EventBus::getInstance();

    // 2. 客户端加入世界时重置状态并注册命令
    mCommandRegisterListener =
        bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
            [](ll::event::client::ClientJoinLevelEvent&) {
                // 新世界 → 自动退出灵魂出窍 + 关闭所有功能
                if (g_FreeCamEnabled) {
                    Stipuleroo::DisableFreeCamera(nullptr);
                }
                g_FreeCamEnabled    = false;
                g_FreeCamPlayer     = nullptr;
                g_OriginalGameType  = GameType::Survival;
                g_AutoToolEnabled   = false;
                g_FakeSneakEnabled  = false;
                g_NightVisionEnabled = false;
                g_AutoBridgeEnabled = false;

                // 注册 /fc 命令
                {
                    auto& fcKey = StipulerooConfig::get().freecamKey;
                    auto& fcCmd = ll::command::CommandRegistrar::getInstance(true)
                                      .getOrCreateCommand(
                                          "fc",
                                          fcKey.empty()
                                              ? "开启或关闭灵魂出窍模式。"
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
                    auto& atCmd = ll::command::CommandRegistrar::getInstance(true)
                                      .getOrCreateCommand(
                                          "at",
                                          atKey.empty()
                                              ? "开启或关闭自动切换工具。"
                                              : fmt::format("开启或关闭自动切换工具（快捷键: {}）。", atKey),
                                          CommandPermissionLevel::Any
                                      );

                    atCmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
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
                    auto& fsCmd = ll::command::CommandRegistrar::getInstance(true)
                                      .getOrCreateCommand(
                                          "fs",
                                          fsKey.empty()
                                              ? "开启或关闭伪潜行。"
                                              : fmt::format("开启或关闭伪潜行（快捷键: {}）。", fsKey),
                                          CommandPermissionLevel::Internal
                                      );

                    fsCmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
                        g_FakeSneakEnabled = !g_FakeSneakEnabled;
                        if (g_FakeSneakEnabled) {
                            return output.success("伪潜行已启用。");
                        } else {
                            return output.success("伪潜行已禁用。");
                        }
                    });
                }

                // 注册 /rv 命令 (隐藏)
                {
                    auto& rvKey = StipulerooConfig::get().nightVisionKey;
                    auto& rvCmd = ll::command::CommandRegistrar::getInstance(true)
                                      .getOrCreateCommand(
                                          "rv",
                                          rvKey.empty()
                                              ? "开启或关闭夜视。"
                                              : fmt::format("开启或关闭夜视（快捷键: {}）。", rvKey),
                                          CommandPermissionLevel::Internal
                                      );

                    rvCmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
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
                    auto& abCmd = ll::command::CommandRegistrar::getInstance(true)
                                      .getOrCreateCommand(
                                          "ab",
                                          abKey.empty()
                                              ? "开启或关闭自动搭路。"
                                              : fmt::format("开启或关闭自动搭路（快捷键: {}）。", abKey),
                                          CommandPermissionLevel::Any
                                      );

                    abCmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
                        g_AutoBridgeEnabled = !g_AutoBridgeEnabled;
                        if (g_AutoBridgeEnabled) {
                            return output.success("自动搭路已启用。");
                        } else {
                            return output.success("自动搭路已禁用。");
                        }
                    });
                }
            }
        );

    // 3. 死亡 → 自动退出
    mDieListener = bus.emplaceListener<ll::event::player::PlayerDieEvent>(
        [](ll::event::player::PlayerDieEvent& ev) {
            if (g_FreeCamEnabled) {
                Stipuleroo::DisableFreeCamera(&ev.self());
            }
        }
    );

    // 4. 退出世界 → 重置状态 (不调用 DisableFreeCamera，避免操作已销毁的 Player)
    mExitLevelListener = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>(
        [](ll::event::client::ClientExitLevelEvent&) {
            g_FreeCamEnabled    = false;
            g_FreeCamPlayer     = nullptr;
            g_OriginalGameType  = GameType::Survival;
            g_AutoToolEnabled   = false;
            g_FakeSneakEnabled  = false;
            g_NightVisionEnabled = false;
            g_AutoBridgeEnabled = false;
        }
    );

    return true;
}

bool Entry::disable() {
    if (g_FreeCamEnabled) {
        Stipuleroo::DisableFreeCamera(nullptr);
    }
    g_AutoToolEnabled    = false;
    g_FakeSneakEnabled   = false;
    g_NightVisionEnabled = false;
    g_AutoBridgeEnabled  = false;
    Stipuleroo::autoToolHook(false);
    Stipuleroo::fakeSneakHook(false);
    Stipuleroo::nightVisionHook(false);
    Stipuleroo::autoBridgeHook(false);
    return true;
}

bool Entry::unload() {
    if (g_FreeCamEnabled) {
        Stipuleroo::DisableFreeCamera(nullptr);
    }
    g_AutoToolEnabled    = false;
    g_FakeSneakEnabled   = false;
    g_NightVisionEnabled = false;
    g_AutoBridgeEnabled  = false;
    Stipuleroo::autoToolHook(false);
    Stipuleroo::fakeSneakHook(false);
    Stipuleroo::nightVisionHook(false);
    Stipuleroo::autoBridgeHook(false);
    auto& bus = ll::event::EventBus::getInstance();
    bus.removeListener(mCommandRegisterListener);
    bus.removeListener(mDieListener);
    bus.removeListener(mExitLevelListener);
    bus.removeListener(mFreecamKeyListener);
    bus.removeListener(mAutoToolKeyListener);
    bus.removeListener(mFakeSneakKeyListener);
    bus.removeListener(mNightVisionKeyListener);
    bus.removeListener(mAutoBridgeKeyListener);
    Stipuleroo::freecameraHook(false);
    return true;
}

} // namespace Stipuleroo

LL_REGISTER_MOD(Stipuleroo::Entry, Stipuleroo::Entry::getInstance());
