#include "Global.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/vanilla_components/MoveRequestComponent.h"
#include "mc/deps/vanilla_components/OnGroundFlagComponent.h"
#include "mc/entity/systems/sneak_movement_system/SneakMovementSystem.h"

bool g_FakeSneakEnabled = false;

// ============================================================
//  仿子沐伪潜行: 判断 onGround 有值 → mSneaking=true, return true
// ============================================================
LL_STATIC_HOOK(
    FakeSneakEdgeHook,
    HookPriority::Normal,
    &SneakMovementSystem::storeSneakStateAndReturnDoSneakMovement,
    bool,
    ActorDataFlagComponent const&         actorData,
    Optional<MoveInputComponent const>    moveInputComponent,
    Optional<OnGroundFlagComponent const> onGround,
    MoveRequestComponent&                 moveRequest
) {
    if (g_FakeSneakEnabled) {
        if (onGround.mEnTTStorage) {
            using NonConstSt = entt::basic_storage<
                OnGroundFlagComponent, EntityId,
                std::allocator<OnGroundFlagComponent>, void>;
            if (reinterpret_cast<NonConstSt*>(
                    const_cast<void*>(
                        static_cast<const void*>(onGround.mEnTTStorage)))
                    ->contains(onGround.mEntity)) {
                moveRequest.mSneaking = true;
            }
        }
        return true;
    }
    return origin(actorData, moveInputComponent, onGround, moveRequest);
}

// ============================================================
//  Hook RAII
// ============================================================
namespace Stipuleroo {

struct FakeSneakImpl {
    ll::memory::HookRegistrar<FakeSneakEdgeHook> r;
};

std::unique_ptr<FakeSneakImpl> impl;

void fakeSneakHook(bool enable) {
    if (enable) {
        if (!impl) impl = std::make_unique<FakeSneakImpl>();
    } else {
        impl.reset();
    }
}

} // namespace Stipuleroo