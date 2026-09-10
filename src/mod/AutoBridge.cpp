#include "Global.h"

#include <cmath>

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/actor/player/Player.h"

bool g_AutoBridgeEnabled = false;

LL_TYPE_INSTANCE_HOOK(
    AutoBridgePacketHook,
    ll::memory::HookPriority::Highest,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    class Packet& packet
) {
    if (!g_AutoBridgeEnabled) return origin(packet);
    if (packet.getId() != MinecraftPacketIds::PlayerAuthInputPacket)
        return origin(packet);

    auto client = ll::service::getClientInstance();
    auto* lp    = client ? client->getLocalPlayer() : nullptr;
    if (!lp) return origin(packet);

    auto& held = lp->getSelectedItem();
    if (held.isNull()) return origin(packet);
    if (!held.getBlockForRendering()) return origin(packet); // 非方块不放置

    auto& auth = static_cast<PlayerAuthInputPacket&>(packet);

    float yaw = auth.mRot->y;
    float rad = yaw * 3.14159265f / 180.0f;
    int dirX  = (int)std::round(-std::sin(rad));
    int dirZ  = (int)std::round( std::cos(rad));
    int footX = (int)std::floor(auth.mPos->x);
    int footY = (int)std::floor(auth.mPos->y - 2.0f);
    int footZ = (int)std::floor(auth.mPos->z);

    BlockPos self(footX, footY, footZ);                 // 脚下
    BlockPos front(footX + dirX, footY, footZ + dirZ);   // 前方脚下

    auto& bs = lp->getDimensionBlockSourceConst();

    // 26.32+: BlockSource::isEmptyBlock 已移除 → getBlock().isAir()
    if (bs.getBlock(self).isAir())  lp->mGameMode->buildBlock(self,  1, false);
    if (bs.getBlock(front).isAir()) lp->mGameMode->buildBlock(front, 1, false);

    origin(packet);
}

namespace Stipuleroo {

struct AutoBridgeImpl {
    ll::memory::HookRegistrar<AutoBridgePacketHook> r;
};

std::unique_ptr<AutoBridgeImpl> impl;

void autoBridgeHook(bool enable) {
    if (enable) {
        if (!impl) {
            impl = std::make_unique<AutoBridgeImpl>();
            SROO_DEBUG("AutoBridge: hook installed (26.40)");
        }
    } else {
        impl.reset();
    }
}

} // namespace Stipuleroo
