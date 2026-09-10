#include "Global.h"

#include <ll/api/mod/NativeMod.h>

// ============================================================
//  伪潜行 — 暂停适配（26.32 起游戏移除挂点，用户决定先不做）
// ============================================================
// 26.10 挂点: SneakMovementSystem::storeSneakStateAndReturnDoSneakMovement
//   (mc/entity/systems/sneak_movement_system/SneakMovementSystem.h)
// 26.32+:     该方法已从游戏移除（只剩 create()/getMaxCollisionVolume()），
//   若日后恢复本功能，候选挂点是:
//   SneakTriggerSystem::doIntentTick(StrictEntityContext const&,
//       MoveInputComponent const&, ActorGameTypeComponent const&,
//       PlayerInputRequestComponent const&, ActorDataFlagComponent const&,
//       PlayerActionComponent&, Optional<WasInWaterFlagComponent const>,
//       Optional<PassengerComponent const>,
//       OptionalGlobal<BaseGameVersionComponent const>, ExternalDataInterface const&)
//   需要 IDA/符号确认其写入 PlayerActionComponent 的潜行意图字段,
//   且验证该路径能让服务端感知潜行。
// 当前实现: /fs 命令与快捷键保留（开关标记 g_FakeSneakEnabled 照常翻转），
//   但不挂任何 Hook、不产生实际效果。

bool g_FakeSneakEnabled = false;

namespace Stipuleroo {

void fakeSneakHook(bool enable) {
    // no-op: 26.32+ 无可用挂点, 等待重新实现(见上注释)
    if (enable) {
        SROO_DEBUG("FakeSneak: suspended (no hook, by design)");
    }
    (void)enable;
}

} // namespace Stipuleroo
