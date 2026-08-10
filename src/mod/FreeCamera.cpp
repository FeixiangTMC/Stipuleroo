#include "Global.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/strict/StrictEntityContext.h"
#include "mc/deps/minecraft_camera/CameraRegistry.h"
#include "mc/deps/minecraft_camera/components/ActiveCameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraRenderFirstPersonObjectsComponent.h"
#include "mc/deps/minecraft_camera/components/CameraRenderPlayerModelComponent.h"
#include "mc/deps/minecraft_camera/components/CurrentInputCameraComponent.h"
#include "mc/deps/minecraft_camera/components/DebugCameraComponent.h"
#include "mc/deps/minecraft_camera/components/DefaultInputCameraComponent.h"
#include "mc/deps/minecraft_camera/components/PlayerStateAffectsRenderingComponent.h"
#include "mc/deps/minecraft_camera/components/RenderCameraComponent.h"
#include "mc/deps/vanilla_components/DebugCameraIsActiveComponent.h"
#include "mc/deps/vanilla_components/MoveRequestComponent.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/entity/components/LocalMoveVelocityComponent.h"
#include "mc/entity/components/MobTravelComponent.h"
#include "mc/entity/components/MobRotationComponent.h"
#include "mc/entity/components/PostTickPositionDeltaComponent.h"
#include "mc/entity/components/RenderPositionComponent.h"
#include "mc/entity/systems/DefaultMoveSystems.h"
#include "mc/entity/systems/FinalizeMoveSystemImpl.h"
#include "mc/entity/systems/MobTravelIntentSystemImpl.h"
#include "mc/entity/systems/RotateAndSetVelocitySystem.h"
#include "mc/entity/systems/UpdateRenderPosSystem.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/entity/components/PlayerBlockActions.h"
#include "mc/entity/components/PlayerBlockActionData.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"

#include <atomic>
#include <cmath>

// ============================================================
//  全局状态
// ============================================================
bool g_FreeCamEnabled = false;

namespace {

// ---- 内部状态 ----
IClientInstance* g_client{};
uint             g_playerEntity{static_cast<uint>(entt::null)};
uintptr_t        g_localVelocityPtr{};
Vec3             g_anchorPos{};
Vec2             g_anchorRot{};    // 玩家锚点旋转 (yaw, pitch)
bool             g_anchorValid{};
bool             g_anchorRotValid{};

// ---- 异步开关（KeyInputEvent 设标记，SetupCameraHook 在渲染帧执行） ----
std::atomic_bool g_toggleRequested{false};
std::atomic_bool g_toggleTarget{false};   // true=enable, false=disable

// ---- 工具 ----
Vec3 zeroVec3() { return {0.0f, 0.0f, 0.0f}; }

EntityContext* ownerEntity(OwnerPtr<EntityId>& owner) {
    if (!owner) return nullptr;
    return &*owner;
}

bool isFrozenPlayer(StrictEntityContext const& ctx) {
    auto id = g_playerEntity;
    if (id == static_cast<uint>(entt::null)) return false;
    return static_cast<uint>(ctx.mEntity.get()) == id;
}

bool isLocalVelocity(LocalMoveVelocityComponent& v) {
    auto ptr = g_localVelocityPtr;
    return ptr != 0 && reinterpret_cast<uintptr_t>(&v) == ptr;
}

void freezeStateVector(StateVectorComponent& s) {
    s.mPosPrev  = s.mPos.get();
    s.mPosDelta = zeroVec3();
}

void clearMobTravel(MobTravelComponent& t) {
    t.mLocalMovementVelocity = zeroVec3();
    t.mMovementSpeed         = 0.0f;
}

void copyCameraFields(
    MinecraftCamera::CameraComponent&       dst,
    MinecraftCamera::CameraComponent const& src
) {
    dst.mOrientation                         = src.mOrientation.get();
    dst.mPosition                            = src.mPosition.get();
    dst.mAspectRatio                         = src.mAspectRatio;
    dst.mFieldOfView                         = src.mFieldOfView;
    dst.mNearPlane                           = src.mNearPlane;
    dst.mFarPlane                            = src.mFarPlane;
    dst.mPostViewTransform.get()._m.get()    = src.mPostViewTransform.get()._m.get();
    dst.mSavedProjection.get()._m.get()      = src.mSavedProjection.get()._m.get();
    dst.mSavedModelView.get()._m.get()       = src.mSavedModelView.get()._m.get();
}

template <class Component>
void moveMarker(entt::basic_registry<EntityId>& reg, EntityId from, EntityId to) {
    if (!from.isNull()) reg.remove<Component>(from);
    if (!to.isNull())   reg.get_or_emplace<Component>(to);
}

MinecraftCamera::CameraComponent* cameraComp(EntityContext& entity) {
    auto comp = entity.tryGetComponent<MinecraftCamera::CameraComponent>();
    return comp ? &*comp : nullptr;
}

void copyGameCameraToDebug(EntityContext& game, EntityContext& debug) {
    auto* gc = cameraComp(game);
    auto* dc = cameraComp(debug);
    if (!gc || !dc) return;
    copyCameraFields(*dc, *gc);
    if (auto ds = debug.tryGetComponent<MinecraftCamera::DebugCameraComponent>()) {
        copyCameraFields(ds->mFrozenCamera.get(), *gc);
        ds->mInputMode = MinecraftCamera::DebugCameraComponent::InputMode::DebugCamera;
    }
}

// 实际执行镜头切换（必须在渲染帧内调用，由 SetupCameraHook 触发）
bool setVanillaDebugCamera(IClientInstance& ci, bool enabled) {
    auto cr = ci.getCameraRegistry();
    if (!cr) return !enabled;

    auto* gameEntity  = ownerEntity(cr->mGameCamera);
    auto* debugEntity = ownerEntity(cr->mDebugCamera);
    if (!gameEntity || !debugEntity) return !enabled;

    auto& reg     = debugEntity->getRegistry();
    auto  gameId  = gameEntity->mEntity;
    auto  debugId = debugEntity->mEntity;
    auto  active  = enabled ? debugId : gameId;
    auto  old     = enabled ? gameId : debugId;

    // ---- 1. 冻结 / 解冻 ----
    if (enabled) {
        auto* player = ci.getLocalPlayer();
        if (!player) return false;

        auto& pctx = player->getEntityContext();
        if (auto sv = pctx.tryGetComponent<StateVectorComponent>()) {
            g_anchorPos   = sv->mPos.get();
            g_anchorValid = true;
        } else {
            return false;
        }

        // 记录锚点旋转（防止玩家身体跟着摄像头转）
        if (auto rot = pctx.tryGetComponent<ActorRotationComponent>()) {
            auto r          = rot->mRot.get();
            g_anchorRot      = {r.x, r.y};
            g_anchorRotValid = true;
        }

        auto& ctx = reg.ctx();
        if (!ctx.contains<DebugCameraIsActiveComponent>()) {
            ctx.emplace<DebugCameraIsActiveComponent>();
        }
        g_playerEntity = static_cast<uint>(pctx.mEntity);
        if (auto lv = pctx.tryGetComponent<LocalMoveVelocityComponent>()) {
            g_localVelocityPtr = reinterpret_cast<uintptr_t>(&*lv);
        }
    } else {
        auto& ctx = reg.ctx();
        if (ctx.contains<DebugCameraIsActiveComponent>()) {
            ctx.erase<DebugCameraIsActiveComponent>();
        }
        g_playerEntity     = static_cast<uint>(entt::null);
        g_localVelocityPtr = 0;
        g_anchorValid      = false;
        g_anchorRotValid   = false;
    }

    // ---- 2. 复制镜头 + 切换标记 ----
    if (enabled) {
        copyGameCameraToDebug(*gameEntity, *debugEntity);
    }

    moveMarker<MinecraftCamera::ActiveCameraComponent>(reg, old, active);
    moveMarker<MinecraftCamera::CurrentInputCameraComponent>(reg, old, active);
    moveMarker<MinecraftCamera::RenderCameraComponent>(reg, old, active);
    moveMarker<MinecraftCamera::CameraRenderPlayerModelComponent>(reg, old, active);

    reg.remove<MinecraftCamera::CameraRenderFirstPersonObjectsComponent>(debugId);
    reg.remove<MinecraftCamera::PlayerStateAffectsRenderingComponent>(debugId);
    reg.get_or_emplace<MinecraftCamera::CameraRenderFirstPersonObjectsComponent>(gameId);
    reg.get_or_emplace<MinecraftCamera::PlayerStateAffectsRenderingComponent>(gameId);

    if (enabled) {
        reg.remove<MinecraftCamera::DefaultInputCameraComponent>(gameId);
    } else {
        reg.get_or_emplace<MinecraftCamera::DefaultInputCameraComponent>(gameId);
    }

    g_client = enabled ? &ci : nullptr;
    return true;
}

// 无条件清空内部状态
void resetFreeCamState() {
    g_client           = nullptr;
    g_playerEntity      = static_cast<uint>(entt::null);
    g_localVelocityPtr  = 0;
    g_anchorValid       = false;
    g_anchorRotValid    = false;
    g_FreeCamEnabled    = false;
    g_toggleRequested.store(false);
}

} // namespace

// ============================================================
//  公共 API（只设标记，不操作相机组件——由 SetupCameraHook 在渲染帧执行）
// ============================================================
namespace Stipuleroo {

void EnableFreeCamera(Player* pl) {
    if (!pl || g_FreeCamEnabled) return;
    g_toggleTarget.store(true, std::memory_order_release);
    g_toggleRequested.store(true, std::memory_order_release);
}

void DisableFreeCamera(Player* pl) {
    if (!g_FreeCamEnabled) return;
    g_toggleTarget.store(false, std::memory_order_release);
    g_toggleRequested.store(true, std::memory_order_release);
}

} // namespace Stipuleroo

// ============================================================
//  Hook 1: 冻结玩家移动 & 旋转
// ============================================================

LL_STATIC_HOOK(
    RotateAndSetVelocityHook,
    ll::memory::HookPriority::High,
    &RotateAndSetVelocitySystem::doTick,
    void,
    MoveInputComponent const&       input,
    PlayerInputModeComponent const& inputMode,
    LocalMoveVelocityComponent&     localVelocity
) {
    if (isLocalVelocity(localVelocity)) { localVelocity.mValue = zeroVec3(); return; }
    origin(input, inputMode, localVelocity);
    if (isLocalVelocity(localVelocity)) { localVelocity.mValue = zeroVec3(); }
}

LL_STATIC_HOOK(
    MobTravelIntentHook,
    ll::memory::HookPriority::High,
    &MobTravelIntentSystemImpl::updatedMoveVelocity,
    void,
    StrictEntityContext const&    ctx,
    LocalMoveVelocityComponent&   lv,
    MobRotationComponent&         rot,
    MobTravelComponent&           travel
) {
    if (isFrozenPlayer(ctx)) { lv.mValue = zeroVec3(); clearMobTravel(travel); return; }
    origin(ctx, lv, rot, travel);
    if (isFrozenPlayer(ctx)) { lv.mValue = zeroVec3(); clearMobTravel(travel); }
}

LL_STATIC_HOOK(
    DefaultMoveHook,
    ll::memory::HookPriority::High,
    &DefaultMoveSystems::doDefaultMoveSystems,
    void,
    StrictEntityContext const&                        ctx,
    Optional<OnGroundFlagComponent const>              onGround,
    Optional<CanStandOnSnowFlagComponent const>        canStandOnSnow,
    Optional<HasLightweightFamilyFlagComponent const>  hasLightweight,
    Optional<MoveInputComponent const>                 moveInput,
    AABBShapeComponent const&                          aabb,
    ActorRotationComponent const&                      rot,
    ActorDataFlagComponent const&                      dataFlag,
    FallDistanceComponent&                             fallDist,
    MobTravelComponent&                                travel,
    StateVectorComponent&                              state,
    IConstBlockSource const&                           region
) {
    if (isFrozenPlayer(ctx)) { clearMobTravel(travel); freezeStateVector(state); return; }
    origin(ctx, onGround, canStandOnSnow, hasLightweight, moveInput, aabb, rot, dataFlag, fallDist, travel, state, region);
}

LL_STATIC_HOOK(
    FlyingMoveHook,
    ll::memory::HookPriority::High,
    &DefaultMoveSystems::doFlyingPlayerMoveSystems,
    void,
    StrictEntityContext const&            ctx,
    Optional<OnGroundFlagComponent const> onGround,
    AABBShapeComponent const&             aabb,
    ActorRotationComponent const&         rot,
    MobTravelComponent&                   travel,
    StateVectorComponent&                 state,
    IConstBlockSource const&              region
) {
    if (isFrozenPlayer(ctx)) { clearMobTravel(travel); freezeStateVector(state); return; }
    origin(ctx, onGround, aabb, rot, travel, state, region);
}

LL_STATIC_HOOK(
    FinalizeMoveHook,
    ll::memory::HookPriority::High,
    &FinalizeMoveSystemImpl::tickFinalizeMoveSystem,
    void,
    StrictEntityContext&                                                          ctx,
    AABBShapeComponent const&                                                     aabb,
    MoveRequestComponent const&                                                   moveReq,
    OffsetsComponent const&                                                       offsets,
    StateVectorComponent&                                                         state,
    Optional<OnGroundFlagComponent const>                                         onGround,
    Optional<MovementAbilitiesComponent const>                                    abilities,
    EntityModifier<OnGroundFlagComponent, CollisionFlagComponent,
                   HorizontalCollisionFlagComponent, VerticalCollisionFlagComponent,
                   CollidableMobNearFlagComponent>&                               mod
) {
    if (isFrozenPlayer(ctx)) {
        state.mPos     = g_anchorValid ? g_anchorPos : state.mPos.get();
        state.mPosPrev = state.mPos.get();
        state.mPosDelta = zeroVec3();
        return;
    }
    origin(ctx, aabb, moveReq, offsets, state, onGround, abilities, mod);
}

LL_STATIC_HOOK(
    UpdateRenderPosHook,
    ll::memory::HookPriority::High,
    &UpdateRenderPosSystem::_doUpdateRenderPosSystem,
    void,
    StrictEntityContext const&  ctx,
    StateVectorComponent const& stateVec,
    RenderPositionComponent&    renderPos
) {
    if (isFrozenPlayer(ctx)) { renderPos.mValue = stateVec.mPos.get(); return; }
    origin(ctx, stateVec, renderPos);
}

// 每帧冻结玩家输出（位置 & 旋转），从 SetupCameraHook 的 origin 前后各调一次
void freezeLocalPlayerOutputs() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player) return;

    auto& ctx = player->getEntityContext();
    if (g_playerEntity != static_cast<uint>(ctx.mEntity)) return;

    if (auto lv = ctx.tryGetComponent<LocalMoveVelocityComponent>()) {
        lv->mValue = zeroVec3();
    }
    if (auto delta = ctx.tryGetComponent<PostTickPositionDeltaComponent>()) {
        delta->mValue = zeroVec3();
    }
    if (auto travel = ctx.tryGetComponent<MobTravelComponent>()) {
        clearMobTravel(*travel);
    }
    if (auto state = ctx.tryGetComponent<StateVectorComponent>()) {
        freezeStateVector(*state);
        if (auto rp = ctx.tryGetComponent<RenderPositionComponent>()) {
            rp->mValue = state->mPos.get();
        }
    }
    if (g_anchorRotValid) {
        if (auto rot = ctx.tryGetComponent<ActorRotationComponent>()) {
            rot->mRot.get() = g_anchorRot;
        }
    }
}

// ============================================================
//  Hook 2: 渲染帧 — 执行相机切换 + 每帧冻结
// ============================================================
LL_TYPE_INSTANCE_HOOK(
    SetupCameraHook,
    ll::memory::HookPriority::Low,
    LevelRendererPlayer,
    &LevelRendererPlayer::setupCamera,
    void,
    mce::Camera& camera,
    float        tickDelta
) {
    // ★ 在渲染帧内执行异步切换（保证相机状态完整一致）
    if (g_toggleRequested.exchange(false, std::memory_order_acq_rel)) {
        bool target = g_toggleTarget.load(std::memory_order_acquire);
        auto ci     = ll::service::getClientInstance();
        if (ci) {
            if (setVanillaDebugCamera(*ci, target)) {
                g_FreeCamEnabled = target;
            } else if (!target) {
                resetFreeCamState();
            }
        }
    }

    // 冻结 pass 1：origin 前（setupCamera 可能读取玩家旋转来初始化相机）
    if (g_FreeCamEnabled) {
        freezeLocalPlayerOutputs();
    }

    origin(camera, tickDelta);

    // 冻结 pass 2：origin 后（setupCamera 内部可能把调试相机旋转写回玩家 ActorRotationComponent）
    if (g_FreeCamEnabled) {
        freezeLocalPlayerOutputs();
    }
}

// ============================================================
//  Hook 3: 发包拦截
// ============================================================
bool filterPacket(Packet& packet) {
    auto id = packet.getId();
    if (id == MinecraftPacketIds::PlayerAction
        || id == MinecraftPacketIds::InventoryTransaction) {
        return false;
    }
    if (id == MinecraftPacketIds::PlayerAuthInputPacket) {
        auto& auth = static_cast<PlayerAuthInputPacket&>(packet);
        auth.mAnalogMoveVector.get() = {0.0f, 0.0f};
        auth.mMove.get()             = {0.0f, 0.0f};
        auth.mRawMoveVector.get()    = {0.0f, 0.0f};
        auth.mPosDelta.get()         = {0.0f, 0.0f, 0.0f};
        auth.mInputData.get().reset();
        auth.mPlayerBlockActions.get().mActions.get().clear();
        if (g_anchorValid) {
            auth.mPos.get() = g_anchorPos;
        }
        // 锁定旋转发包
        if (g_anchorRotValid) {
            auth.mRot.get() = g_anchorRot;
        }
    }
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    PacketSendToServerHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToServer,
    void,
    Packet& packet
) {
    if (g_FreeCamEnabled && !filterPacket(packet)) return;
    origin(packet);
}

LL_TYPE_INSTANCE_HOOK(
    PacketSendHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    Packet& packet
) {
    if (g_FreeCamEnabled && !filterPacket(packet)) return;
    origin(packet);
}

// ============================================================
//  Hook 4: GameMode 交互拦截
// ============================================================
LL_TYPE_INSTANCE_HOOK(
    GMStartDestroyHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$startDestroyBlock, bool,
    BlockPos const& pos, uchar face, bool& destroyed
) { if (g_FreeCamEnabled) return false; return origin(pos, face, destroyed); }

LL_TYPE_INSTANCE_HOOK(
    GMDestroyHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$destroyBlock, bool,
    BlockPos const& pos, uchar face
) { if (g_FreeCamEnabled) return false; return origin(pos, face); }

LL_TYPE_INSTANCE_HOOK(
    GMContinueDestroyHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$continueDestroyBlock, bool,
    BlockPos const& pos, uchar face, Vec3 const& playerPos, bool& destroyed
) { if (g_FreeCamEnabled) return false; return origin(pos, face, playerPos, destroyed); }

LL_TYPE_INSTANCE_HOOK(
    GMUseItemHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$useItem, bool,
    ItemStack& item
) { if (g_FreeCamEnabled) return false; return origin(item); }

LL_TYPE_INSTANCE_HOOK(
    GMUseItemOnHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$useItemOn, InteractionResult,
    ItemStack& item, BlockPos const& pos, uchar face, Vec3 const& hit, Block const* block, bool firstEvent
) { if (g_FreeCamEnabled) return {false, false}; return origin(item, pos, face, hit, block, firstEvent); }

LL_TYPE_INSTANCE_HOOK(
    GMInteractHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$interact, bool,
    Actor& entity, Vec3 const& loc
) { if (g_FreeCamEnabled) return false; return origin(entity, loc); }

LL_TYPE_INSTANCE_HOOK(
    GMAttackHook, ll::memory::HookPriority::Normal,
    GameMode, &GameMode::$attack, bool,
    Actor& entity
) { if (g_FreeCamEnabled) return false; return origin(entity); }

// ============================================================
//  Hook RAII
// ============================================================
namespace Stipuleroo {

struct FreeCamImpl {
    ll::memory::HookRegistrar<
        RotateAndSetVelocityHook,
        MobTravelIntentHook,
        DefaultMoveHook,
        FlyingMoveHook,
        FinalizeMoveHook,
        UpdateRenderPosHook,
        SetupCameraHook,
        PacketSendToServerHook,
        PacketSendHook,
        GMStartDestroyHook,
        GMDestroyHook,
        GMContinueDestroyHook,
        GMUseItemHook,
        GMUseItemOnHook,
        GMInteractHook,
        GMAttackHook
    > r;
};

std::unique_ptr<FreeCamImpl> fcImpl;

void freecameraHook(bool enable) {
    if (enable) {
        if (!fcImpl) fcImpl = std::make_unique<FreeCamImpl>();
    } else {
        if (g_FreeCamEnabled) {
            DisableFreeCamera(nullptr);
        }
        fcImpl.reset();
    }
}

} // namespace Stipuleroo
