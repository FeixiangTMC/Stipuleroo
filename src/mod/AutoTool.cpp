#include "Global.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/world/level/GameType.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/network/packet/AnimatePacket.h"
#include "mc/network/packet/PlayerHotbarPacket.h"
#include "mc/entity/components/PlayerBlockActions.h"
#include "mc/entity/components/PlayerBlockActionData.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/HitResult.h"

// ============================================================
//  全局状态
// ============================================================
bool g_AutoToolEnabled = false;

namespace Stipuleroo {

// 计算武器附魔伤害加成（锋利等）
// 基岩版 1.21+ 锋利公式: +1.25 × 等级
static float getEnchantAttackBonus(ItemStackBase const& stack) {
    auto enchants = stack.constructItemEnchantsFromUserData();
    float bonus = 0.0f;
    for (auto& inst : enchants.getAllEnchants()) {
        if (inst.mEnchantType == Enchant::Type::Sharpness) {
            bonus += 1.25f * (float)inst.mLevel;
        }
        // Smite / BaneOfArthropods 针对性太强，不纳入通用比较
    }
    return bonus;
}

static int searchBestToolInHotbar(Player* player, BlockPos const& pos) {
    if (player->getPlayerGameType() == GameType::Creative) return -1;
    auto& blockSrc = player->getDimensionBlockSourceConst();
    auto& block    = blockSrc.getBlock(pos);
    auto& inv      = player->getInventory();
    int   curSlot  = player->getSelectedItemSlot();
    int   bestSlot  = -1;
    float bestSpeed = 1.0f;
    for (int slot = 0; slot < 9; ++slot) {
        auto& stack = inv.getItem(slot);
        if (stack.isNull()) continue;
        float speed = stack.getItem()->getDestroySpeed(stack, block);
        short maxDmg = stack.getMaxDamage();
        short curDmg = stack.getDamageValue();
        if (maxDmg > 0 && (maxDmg - curDmg) <= 1) continue;
        if (speed > bestSpeed) {
            bestSpeed = speed;
            bestSlot  = slot;
        } else if (speed == bestSpeed && slot == curSlot && bestSlot >= 0) {
            // 相同挖掘速度 → 当前槽优先（不切）
            bestSlot = curSlot;
        }
    }
    return (bestSlot >= 0 && bestSlot != curSlot) ? bestSlot : -1;
}

static int searchBestWeaponInHotbar(Player* player) {
    auto& inv     = player->getInventory();
    int   curSlot = player->getSelectedItemSlot();
    int   bestSlot   = -1;
    float bestDamage = 1.0f;
    for (int slot = 0; slot < 9; ++slot) {
        auto& stack = inv.getItem(slot);
        if (stack.isNull()) continue;
        // 基础攻击力 + 锋利附魔加成
        float totalDamage = (float)stack.getItem()->getAttackDamage()
                          + getEnchantAttackBonus(stack);
        short maxDmg = stack.getMaxDamage();
        short curDmg = stack.getDamageValue();
        if (maxDmg > 0 && (maxDmg - curDmg) <= 1) continue;
        if (totalDamage > bestDamage) {
            bestDamage = totalDamage;
            bestSlot   = slot;
        } else if (totalDamage == bestDamage && slot == curSlot && bestSlot >= 0) {
            // 相同伤害 → 当前槽优先（不切）
            bestSlot = curSlot;
        }
    }
    return (bestSlot >= 0 && bestSlot != curSlot) ? bestSlot : -1;
}

} // namespace Stipuleroo

// ============================================================
//  Hook: LoopbackPacketSender::$send
//  武器: AnimatePacket (Swing+Attack) → 切武器 → 当次攻击有效
//  工具: PlayerAuthInputPacket 含挖掘动作 → 取 pos 搜索最优工具
//        直接切槽 + 同包告诉服务端 → 充分利用 PlayerHotbarPacket
//        在 origin(packet) 之前发送，服务端顺序处理
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

    auto id = packet.getId();

    // —— 武器: AnimatePacket 攻击挥臂 ——
    if (id == MinecraftPacketIds::Animate) {
        auto& ap = static_cast<AnimatePacket&>(packet);
        if (ap.mAction == AnimatePacketPayload::Action::Swing
            && ap.mSwingSource->has_value()
            && **ap.mSwingSource == ActorSwingSource::Attack
            && lp->traceRay(5.5f, true, false).mType == ::HitResultType::Entity) {
            int best = Stipuleroo::searchBestWeaponInHotbar(lp);
            if (best >= 0) {
                lp->mInventory->selectSlot(best, ::ContainerID::Inventory);
                PlayerHotbarPacket hotbarPkt;
                hotbarPkt.mSelectedSlot     = (uint)best;
                hotbarPkt.mShouldSelectSlot = true;
                hotbarPkt.mContainerId      = ::ContainerID::Inventory;
                origin(hotbarPkt); // 先发换槽，再发 Animate
            }
        }
    }

    // —— 工具: 从 PlayerAuthInputPacket 提取挖掘位置 ——
    if (id == MinecraftPacketIds::PlayerAuthInputPacket) {
        auto& auth    = static_cast<PlayerAuthInputPacket&>(packet);
        auto& actions = *auth.mPlayerBlockActions->mActions;
        BlockPos targetPos;

        bool foundAction = false;
        for (auto& act : actions) {
            if (act.mPlayerActionType == PlayerActionType::StartDestroyBlock
                || act.mPlayerActionType == PlayerActionType::ContinueDestroyBlock) {
                targetPos   = act.mPos;
                foundAction = true;
                break;
            }
        }

        if (foundAction) {
            int best = Stipuleroo::searchBestToolInHotbar(lp, targetPos);
            if (best >= 0) {
                lp->mInventory->selectSlot(best, ::ContainerID::Inventory);

                PlayerHotbarPacket hotbarPkt;
                hotbarPkt.mSelectedSlot     = (uint)best;
                hotbarPkt.mShouldSelectSlot = true;
                hotbarPkt.mContainerId      = ::ContainerID::Inventory;
                origin(hotbarPkt); // 先发换槽，再发 PlayerAuthInputPacket
            }
        }
    }

    origin(packet);
}

// ============================================================
//  Hook RAII
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
