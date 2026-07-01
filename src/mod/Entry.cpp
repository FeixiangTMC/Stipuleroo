#include "Entry.h"
#include "Global.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
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
    // 1. 安装 Hook
    Stipuleroo::freecameraHook(true);
    Stipuleroo::autoToolHook(true);

    auto& bus = ll::event::EventBus::getInstance();

    // 2. 客户端加入世界时重置状态并注册命令
    mCommandRegisterListener =
        bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
            [](ll::event::client::ClientJoinLevelEvent&) {
                // 新世界 → 自动退出灵魂出窍 + 关闭自动工具
                if (g_FreeCamEnabled) {
                    Stipuleroo::DisableFreeCamera(nullptr);
                }
                g_FreeCamEnabled   = false;
                g_FreeCamPlayer    = nullptr;
                g_OriginalGameType = GameType::Survival;
                g_AutoToolEnabled  = false;

                // 注册 /fc 命令
                auto& fcCmd = ll::command::CommandRegistrar::getInstance(true)
                                  .getOrCreateCommand(
                                      "fc",
                                      "开启或关闭灵魂出窍模式。",
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

                // 注册 /at 命令
                auto& atCmd = ll::command::CommandRegistrar::getInstance(true)
                                  .getOrCreateCommand(
                                      "at",
                                      "开启或关闭自动切换工具。",
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
            g_FreeCamEnabled   = false;
            g_FreeCamPlayer    = nullptr;
            g_OriginalGameType = GameType::Survival;
            g_AutoToolEnabled  = false;
        }
    );

    return true;
}

bool Entry::disable() {
    if (g_FreeCamEnabled) {
        Stipuleroo::DisableFreeCamera(nullptr);
    }
    g_AutoToolEnabled = false;
    Stipuleroo::autoToolHook(false);
    return true;
}

bool Entry::unload() {
    if (g_FreeCamEnabled) {
        Stipuleroo::DisableFreeCamera(nullptr);
    }
    g_AutoToolEnabled = false;
    Stipuleroo::autoToolHook(false);
    auto& bus = ll::event::EventBus::getInstance();
    bus.removeListener(mCommandRegisterListener);
    bus.removeListener(mDieListener);
    bus.removeListener(mExitLevelListener);
    Stipuleroo::freecameraHook(false);
    return true;
}

} // namespace Stipuleroo

LL_REGISTER_MOD(Stipuleroo::Entry, Stipuleroo::Entry::getInstance());
