#include "Global.h"

#include "ll/api/memory/Hook.h"

#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/Mob.h"

// ============================================================
//  全局状态
// ============================================================
bool     g_FreeCamEnabled   = false;
GameType g_OriginalGameType = GameType::Survival;
Player*  g_FreeCamPlayer    = nullptr;

namespace Stipuleroo {

// ============================================================
//  进入 / 退出
// ============================================================
void EnableFreeCamera(Player* pl) {
    if (!pl) return;
    g_FreeCamPlayer    = pl;
    g_OriginalGameType = pl->getPlayerGameType();
    g_FreeCamEnabled   = true;
    pl->setPlayerGameType(GameType::Spectator);
}

void DisableFreeCamera(Player* pl) {
    if (!pl) pl = g_FreeCamPlayer;
    if (pl) {
        // 先恢复游戏模式，此时 g_FreeCamEnabled 仍为 true，
        // Hook 会拦截恢复时产生的 SetPlayerGameType 包，
        // 保证服务器从始至终不知道客户端的模式变化
        pl->setPlayerGameType(g_OriginalGameType);
    }
    g_FreeCamEnabled = false;
    g_FreeCamPlayer  = nullptr;
}

} // namespace Stipuleroo

// ============================================================
//  ★ 核心 Hook：拦截发包
//    1. PlayerAuthInputPacket → 服务器不知道玩家动了
//    2. SetPlayerGameType     → 服务器不知道游戏模式变了
// ============================================================

LL_TYPE_INSTANCE_HOOK(
    ClientPacketSendEvent,
    ll::memory::HookPriority::Highest,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    class Packet& packet
) {
    if (g_FreeCamEnabled) {
        auto id = packet.getId();
        if (id == MinecraftPacketIds::PlayerAuthInputPacket
            || id == MinecraftPacketIds::SetPlayerGameType) {
            return;
        }
    }
    return origin(packet);
}

// ============================================================
//  Hook 2: 玩家受伤 → 自动退出灵魂出窍
//    res != 0 确保只有实际造成伤害才触发
//    创造模式的普通攻击 res 为 0，不会误触发
//    /kill 和虚空伤害对所有模式都生效
// ============================================================

LL_TYPE_INSTANCE_HOOK(
    PlayerHurtEvent,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::getDamageAfterResistanceEffect,
    float,
    class ActorDamageSource const& a1,
    float                          a2
) {
    auto res = origin(a1, a2);
    if (g_FreeCamEnabled && res != 0 && this->isLocalPlayer()) {
        Stipuleroo::DisableFreeCamera((Player*)this);
    }
    return res;
}

// ============================================================
//  Hook RAII 管理
// ============================================================
namespace Stipuleroo {

struct Impl {
    ll::memory::HookRegistrar<
        ClientPacketSendEvent,
        PlayerHurtEvent>
        r;
};

std::unique_ptr<Impl> impl;

void freecameraHook(bool enable) {
    if (enable) {
        if (!impl) impl = std::make_unique<Impl>();
    } else {
        impl.reset();
    }
}

} // namespace Stipuleroo
