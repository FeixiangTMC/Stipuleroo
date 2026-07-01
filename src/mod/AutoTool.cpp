#include "Global.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/PlayerActionPacket.h"
#include "mc/network/packet/PlayerActionType.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/network/packet/PlayerHotbarPacket.h"
#include "mc/entity/components/PlayerBlockActions.h"
#include "mc/entity/components/PlayerBlockActionData.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

// ============================================================
//  全局状态
// ============================================================
bool g_AutoToolEnabled = false;

namespace Stipuleroo {

// ============================================================
//  搜索快捷栏中最优工具 (参照 CoralFans searchBestToolInInv)
// ============================================================
static int searchBestToolInHotbar(Player* player, BlockPos const& pos) {
    auto& blockSrc = player->getDimensionBlockSourceConst();
    auto& block    = blockSrc.getBlock(pos);
    auto& inv      = player->getInventory();

    int   bestSlot  = -1;
    float bestSpeed = 1.0f; // 空手基准速度

    for (int slot = 0; slot < 9; ++slot) {
        auto& stack = inv.getItem(slot);
        if (stack.isNull()) continue;

        float speed = stack.getItem()->getDestroySpeed(stack, block);
        // 跳过耐久即将耗尽的工具 (参照 CoralFans minDamage)
        short maxDamage = stack.getMaxDamage();
        short curDamage = stack.getDamageValue();
        if (maxDamage > 0 && (maxDamage - curDamage) <= 1) continue;

        if (speed > bestSpeed) {
            bestSpeed = speed;
            bestSlot  = slot;
        }
    }

    return bestSlot;
}

// ============================================================
//  切换到最优工具
//  PlayerInventory::selectSlot 本地切 + PlayerHotbarPacket 通知服务端
// ============================================================
static void switchToBestTool(Player* player, BlockPos const& pos) {
    int curSlot  = player->getSelectedItemSlot();
    int bestSlot = searchBestToolInHotbar(player, pos);
    if (bestSlot >= 0 && bestSlot != curSlot) {
        player->mInventory->selectSlot(bestSlot, ::ContainerID::Inventory);
    }
}

} // namespace Stipuleroo

// ============================================================
//  Hook: LoopbackPacketSender::$send
//  拦截 StartDestroyBlock / ContinueDestroyBlock 发包前切工具
// ============================================================

LL_TYPE_INSTANCE_HOOK(
    AutoToolPacketHook,
    ll::memory::HookPriority::Highest,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    class Packet& packet
) {
    if (!g_AutoToolEnabled) return origin(packet);

    auto client = ll::service::getClientInstance();
    auto* lp    = client ? client->getLocalPlayer() : nullptr;
    if (!lp) return origin(packet);

    auto id     = packet.getId();
    int  curSlot = lp->getSelectedItemSlot();
    bool switched = false;

    // PlayerAction 包 (ID 36)
    if (id == MinecraftPacketIds::PlayerAction) {
        auto& ap = static_cast<PlayerActionPacket&>(packet);
        if (ap.mAction == PlayerActionType::StartDestroyBlock
            || ap.mAction == PlayerActionType::ContinueDestroyBlock) {
            Stipuleroo::switchToBestTool(lp, ap.mPos);
            switched = true;
        }
    }
    // PlayerAuthInputPacket (ID 144)
    else if (id == MinecraftPacketIds::PlayerAuthInputPacket) {
        auto& auth    = static_cast<PlayerAuthInputPacket&>(packet);
        auto& actions = *auth.mPlayerBlockActions->mActions;
        if (!actions.empty()) {
            for (auto& act : actions) {
                if (act.mPlayerActionType == PlayerActionType::StartDestroyBlock
                    || act.mPlayerActionType == PlayerActionType::ContinueDestroyBlock) {
                    Stipuleroo::switchToBestTool(lp, act.mPos);
                    switched = true;
                    break;
                }
            }
        }
    }

    // 如果切了槽位，先发 PlayerHotbarPacket 通知服务端
    if (switched && lp->getSelectedItemSlot() != curSlot) {
        PlayerHotbarPacket hotbarPkt;
        hotbarPkt.mSelectedSlot     = (uint)lp->getSelectedItemSlot();
        hotbarPkt.mShouldSelectSlot = true;
        hotbarPkt.mContainerId      = ::ContainerID::Inventory;
        origin(hotbarPkt);
    }

    origin(packet);
}

// ============================================================
//  Hook RAII 管理 (沿用 FreeCamera.cpp 模式)
// ============================================================
namespace Stipuleroo {

struct AutoToolImpl {
    ll::memory::HookRegistrar<AutoToolPacketHook> r;
};

std::unique_ptr<AutoToolImpl> impl;

void autoToolHook(bool enable) {
    if (enable) {
        if (!impl) impl = std::make_unique<AutoToolImpl>();
    } else {
        impl.reset();
    }
}

} // namespace Stipuleroo
