// ============================================================
//  Stipuleroo —— 灵魂出窍（自由相机 / Vanilla Debug Camera）
//
//  【26.40 适配关键结论 · 必读】
//  1) 必须用 clang-cl 编译本模组（见 xmake.lua 顶部注释）。
//     26.40 客户端与 LeviLamina 都由 clang/LLVM 构建；entt 的组件类型 ID 是
//     FNV-1a32(去修饰的类型名字符串)：
//        clang : __PRETTY_FUNCTION__ -> "MinecraftCamera::CameraComponent"
//        MSVC  : __FUNCSIG__         -> "struct MinecraftCamera::CameraComponent"
//     两者哈希不同 => 模组看不到游戏创建的任何组件（相机实体的 CameraComponent、
//     玩家的 StateVectorComponent 等），相机切换/冻结等 ECS 逻辑会**静默失效**
//     （旧版用 MSVC 编译时：人物能冻住但视角无法脱离身体，就是这个原因）。
//     （26.10 客户端是 MSVC 构建，所以当时 MSVC 模组是匹配的。）
//  2) 26.40 本地玩家的位置/旋转位于 Actor::mBuiltInComponents，
//     用 Actor::getPosition()/getRotation() 取锚点最可靠，ECS 路径作兜底。
//  3) 相机切换沿用 26.10 已验证的方案：把 game 相机实体上的标记组件搬到
//     debug 相机实体，并在相机注册表 ctx 上放置 DebugCameraIsActiveComponent；
//     之后由游戏自身的调试相机系统负责渲染与飞行输入，模组只负责
//     「冻结身体 + 清空发包输入」。
//
//  历史实现（v15 每帧改写 mce::Camera 的“手动自由镜头”）已移除，
//  备份见 docs/reference/FreeCamera.cpp.v15-camera-override.bak（仅在原生调试相机
//  在 26.40 上不能飞行时，才需要把它作为兜底方案恢复）。
// ============================================================
#include "Global.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
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
#include "mc/entity/components/PostTickPositionDeltaComponent.h"
#include "mc/entity/components/RenderPositionComponent.h"
#include "mc/entity/systems/DefaultMoveSystems.h"
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

// ============================================================
//  全局状态
// ============================================================
bool g_FreeCamEnabled = false;

namespace {

// ---- 内部状态 ----
uint      g_playerEntity{static_cast<uint>(entt::null)};
uintptr_t g_localVelocityPtr{};
Vec3      g_anchorPos{};      // 玩家锚点位置
Vec2      g_anchorRot{};      // 玩家锚点旋转 (yaw, pitch)
bool      g_anchorValid{};
bool      g_anchorRotValid{};

// ---- 异步开关（KeyInputEvent 设标记，SetupCameraHook 在渲染帧执行） ----
std::atomic_bool g_toggleRequested{false};
std::atomic_bool g_toggleTarget{false}; // true=enable, false=disable
int              g_toggleRetry{};       // 关闭失败时的重试计数（渲染帧）
bool             g_cameraRestorePending{}; // 上次想还原相机但环境不可用 → 待下个世界重试

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

void copyCameraFields(MinecraftCamera::CameraComponent& dst, MinecraftCamera::CameraComponent const& src) {
    dst.mOrientation                      = src.mOrientation.get();
    dst.mPosition                         = src.mPosition.get();
    dst.mAspectRatio                      = src.mAspectRatio;
    dst.mFieldOfView                      = src.mFieldOfView;
    dst.mNearPlane                        = src.mNearPlane;
    dst.mFarPlane                         = src.mFarPlane;
    dst.mPostViewTransform.get()._m.get() = src.mPostViewTransform.get()._m.get();
    dst.mSavedProjection.get()._m.get()   = src.mSavedProjection.get()._m.get();
    dst.mSavedModelView.get()._m.get()    = src.mSavedModelView.get()._m.get();
}

template <class Component>
void moveMarker(entt::basic_registry<EntityId>& reg, EntityId from, EntityId to) {
    if (!from.isNull()) reg.remove<Component>(from);
    if (!to.isNull()) reg.get_or_emplace<Component>(to);
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

// ---- 诊断：相机实体组件存在性（26.40 验证用，确认工作正常后可删） ----
// 注意：all_of 只查池、不创建池，作为诊断无副作用。
void logCameraEntityDiag(EntityContext& gameEntity, EntityContext& debugEntity) {
    auto& reg     = debugEntity.getRegistry();
    auto  gameId  = gameEntity.mEntity;
    auto  debugId = debugEntity.mEntity;
#define FC_HAS(C, E) (reg.all_of<C>(E) ? 1 : 0)
    SROO_DEBUG(
        "[fc] game#{}:  Cam={} Active={} CurIn={} DefIn={} Render={} Model={} 1stPerson={} PlayerState={} DebugCam={}",
        static_cast<uint>(gameId),
        FC_HAS(MinecraftCamera::CameraComponent, gameId),
        FC_HAS(MinecraftCamera::ActiveCameraComponent, gameId),
        FC_HAS(MinecraftCamera::CurrentInputCameraComponent, gameId),
        FC_HAS(MinecraftCamera::DefaultInputCameraComponent, gameId),
        FC_HAS(MinecraftCamera::RenderCameraComponent, gameId),
        FC_HAS(MinecraftCamera::CameraRenderPlayerModelComponent, gameId),
        FC_HAS(MinecraftCamera::CameraRenderFirstPersonObjectsComponent, gameId),
        FC_HAS(MinecraftCamera::PlayerStateAffectsRenderingComponent, gameId),
        FC_HAS(MinecraftCamera::DebugCameraComponent, gameId)
    );
    SROO_DEBUG(
        "[fc] debug#{}: Cam={} Active={} CurIn={} DefIn={} Render={} Model={} 1stPerson={} PlayerState={} DebugCam={}",
        static_cast<uint>(debugId),
        FC_HAS(MinecraftCamera::CameraComponent, debugId),
        FC_HAS(MinecraftCamera::ActiveCameraComponent, debugId),
        FC_HAS(MinecraftCamera::CurrentInputCameraComponent, debugId),
        FC_HAS(MinecraftCamera::DefaultInputCameraComponent, debugId),
        FC_HAS(MinecraftCamera::RenderCameraComponent, debugId),
        FC_HAS(MinecraftCamera::CameraRenderPlayerModelComponent, debugId),
        FC_HAS(MinecraftCamera::CameraRenderFirstPersonObjectsComponent, debugId),
        FC_HAS(MinecraftCamera::PlayerStateAffectsRenderingComponent, debugId),
        FC_HAS(MinecraftCamera::DebugCameraComponent, debugId)
    );
#undef FC_HAS
}

// 实际执行镜头切换（必须在渲染帧内调用，由 SetupCameraHook 触发）
// 返回值：true = 切换成功；false = 环境不可用（注册表/实体/玩家缺失）
bool setVanillaDebugCamera(IClientInstance& ci, bool enabled) {
    auto cr = ci.getCameraRegistry();
    if (!cr) {
        SROO_DEBUG("[fc] setVanillaDebugCamera: no camera registry");
        return false;
    }

    auto* gameEntity  = ownerEntity(cr->mGameCamera);
    auto* debugEntity = ownerEntity(cr->mDebugCamera);
    if (!gameEntity || !debugEntity) {
        SROO_DEBUG(
            "[fc] setVanillaDebugCamera: entity missing game={} debug={}",
            gameEntity != nullptr,
            debugEntity != nullptr
        );
        return false;
    }

    auto& reg     = debugEntity->getRegistry();
    auto  gameId  = gameEntity->mEntity;
    auto  debugId = debugEntity->mEntity;
    auto  active  = enabled ? debugId : gameId;
    auto  old     = enabled ? gameId : debugId;

    // ---- 1. 冻结 / 解冻 ----
    if (enabled) {
        auto* player = ci.getLocalPlayer();
        if (!player) {
            SROO_DEBUG("[fc] setVanillaDebugCamera: no local player");
            return false;
        }

        // 26.40: 本地玩家位置/旋转在 Actor::mBuiltInComponents（getPosition/getRotation 内联读取）
        g_anchorPos   = player->getPosition();
        g_anchorValid = true;
        auto pr       = player->getRotation();
        g_anchorRot      = {pr.x, pr.y};
        g_anchorRotValid = true;
        SROO_DEBUG(
            "[fc] anchors pos=({:.1f},{:.1f},{:.1f}) rot=({:.1f},{:.1f})",
            g_anchorPos.x,
            g_anchorPos.y,
            g_anchorPos.z,
            g_anchorRot.x,
            g_anchorRot.y
        );

        // ECS 侧记录（clang-cl 构建后哈希与游戏一致，此路径已可用；仍以内建路径为主）
        auto& pctx     = player->getEntityContext();
        g_playerEntity = static_cast<uint>(pctx.mEntity);
        g_localVelocityPtr = 0;
        if (auto lv = pctx.tryGetComponent<LocalMoveVelocityComponent>()) {
            g_localVelocityPtr = reinterpret_cast<uintptr_t>(&*lv);
        }

        auto& ctx = reg.ctx();
        if (!ctx.contains<DebugCameraIsActiveComponent>()) {
            ctx.emplace<DebugCameraIsActiveComponent>();
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

    // ---- 3. 诊断 ----
    logCameraEntityDiag(*gameEntity, *debugEntity);
    SROO_DEBUG(
        "[fc] switch done enabled={} gameId={} debugId={} playerEntity={}",
        enabled,
        static_cast<uint>(gameId),
        static_cast<uint>(debugId),
        g_playerEntity
    );
    return true;
}

// 无条件清空内部状态
void resetFreeCamState() {
    g_playerEntity     = static_cast<uint>(entt::null);
    g_localVelocityPtr = 0;
    g_anchorValid      = false;
    g_anchorRotValid   = false;
    g_FreeCamEnabled   = false;
    g_toggleRetry      = 0;
    g_toggleRequested.store(false);
}

// 每帧冻结玩家输出（位置 & 旋转），从 SetupCameraHook 的 origin 前后各调一次
void freezeLocalPlayerOutputs() {
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player) return;

    // [26.40 主路径] 本地玩家的 StateVector/ActorRotation 位于 Actor::mBuiltInComponents，
    // 直接冻结（清 delta + 钉位置/旋转），不依赖 EntityContext/系统 hook
    auto& bic = player->mBuiltInComponents.get();
    if (g_anchorValid) {
        if (auto* sv = bic.mStateVectorComponent.get()) {
            sv->mPosPrev   = sv->mPos.get();
            sv->mPosDelta  = zeroVec3();
            sv->mPos.get() = g_anchorPos;
        }
    }
    if (g_anchorRotValid) {
        if (auto* rot = bic.mActorRotationComponent.get()) {
            rot->mRot.get() = g_anchorRot;
        }
    }

    // ECS 路径（clang-cl 构建后可用；实体不在注册表时全部 no-op）
    auto& ctx = player->getEntityContext();
    if (g_playerEntity == static_cast<uint>(entt::null)) return;
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

// 诊断：采样 game/debug 相机实体与玩家位置（确认原生调试相机是否真的接管渲染与飞行）
void probeCameras() {
    static int probeN = 0;
    if ((++probeN) % 240 != 1) return; // 约每 2~4 秒一条
    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto cr = ci->getCameraRegistry();
    if (!cr) return;

    glm::vec3 g3{0.f, 0.f, 0.f};
    glm::vec3 d3{0.f, 0.f, 0.f};
    if (auto* gEnt = ownerEntity(cr->mGameCamera)) {
        if (auto* c = cameraComp(*gEnt)) g3 = c->mPosition.get();
    }
    if (auto* dEnt = ownerEntity(cr->mDebugCamera)) {
        if (auto* c = cameraComp(*dEnt)) d3 = c->mPosition.get();
    }
    Vec3 p3{0.f, 0.f, 0.f};
    if (auto* pl = ci->getLocalPlayer()) p3 = pl->getPosition();

    SROO_DEBUG(
        "[fc] probe game=({:.1f},{:.1f},{:.1f}) debug=({:.1f},{:.1f},{:.1f}) player=({:.1f},{:.1f},{:.1f})",
        g3.x,
        g3.y,
        g3.z,
        d3.x,
        d3.y,
        d3.z,
        p3.x,
        p3.y,
        p3.z
    );
}

} // namespace

// ============================================================
//  公共 API（只设标记，不操作相机组件——由 SetupCameraHook 在渲染帧执行）
// ============================================================
namespace Stipuleroo {

void EnableFreeCamera(Player* pl) {
    if (!pl || g_FreeCamEnabled) return;
    SROO_DEBUG("[fc] EnableFreeCamera requested");
    g_toggleTarget.store(true, std::memory_order_release);
    g_toggleRequested.store(true, std::memory_order_release);
}

void DisableFreeCamera(Player* pl) {
    (void)pl;
    if (!g_FreeCamEnabled) return;
    SROO_DEBUG("[fc] DisableFreeCamera requested");
    g_toggleTarget.store(false, std::memory_order_release);
    g_toggleRequested.store(true, std::memory_order_release);
}

// 立即（同步）关闭灵魂出窍。
// 用于：退出世界 / 玩家加入世界 / 模组被禁用或卸载 —— 这些时刻不会再等到渲染帧，
// 必须当场把调试相机标记还回去，否则重进世界会残留"相机还在 debug 实体上"的状态。
// 会一并清掉挂起的异步请求，避免它在相机注册表失效之后再执行。
// 若此刻相机注册表/实体不可用（正处在世界销毁过程中），会记下待重试标记：
// 下次 resetAllStates()（玩家加入新世界时）会再试一次，确保新世界开局状态干净。
void ForceDisableFreeCameraNow() {
    g_toggleRequested.store(false, std::memory_order_release);
    g_toggleTarget.store(false, std::memory_order_release);

    bool const needRestore = g_FreeCamEnabled || g_cameraRestorePending;
    g_FreeCamEnabled       = false; // 先停掉发包拦截与每帧冻结

    if (needRestore) {
        bool ok = false;
        if (auto ci = ll::service::getClientInstance()) {
            ok = setVanillaDebugCamera(*ci, false);
        }
        if (ok) {
            g_cameraRestorePending = false;
        } else {
            g_cameraRestorePending = true;
            SROO_WARN("FreeCam: 相机还原暂时不可用（注册表/实体缺失），已挂起，进入下个世界时会重试");
        }
    }
    resetFreeCamState();
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
    if (isLocalVelocity(localVelocity)) {
        localVelocity.mValue = zeroVec3();
        return;
    }
    origin(input, inputMode, localVelocity);
    if (isLocalVelocity(localVelocity)) {
        localVelocity.mValue = zeroVec3();
    }
}

// 26.32+: MobTravelIntentSystemImpl::updatedMoveVelocity 已从游戏移除, hook 删除。

LL_STATIC_HOOK(
    DefaultMoveHook,
    ll::memory::HookPriority::High,
    &DefaultMoveSystems::doDefaultMoveSystems,
    void,
    StrictEntityContext const&                       ctx,
    Optional<OnGroundFlagComponent const>            onGround,
    Optional<CanStandOnSnowFlagComponent const>      canStandOnSnow,
    Optional<HasLightweightFamilyFlagComponent const> hasLightweight,
    Optional<MoveInputComponent const>               moveInput,
    AABBShapeComponent const&                        aabb,
    ActorRotationComponent const&                    rot,
    ActorDataFlagComponent const&                    dataFlag,
    FallDistanceComponent&                           fallDist,
    MobTravelComponent&                              travel,
    StateVectorComponent&                            state,
    IConstBlockSource const&                         region
) {
    if (isFrozenPlayer(ctx)) {
        clearMobTravel(travel);
        freezeStateVector(state);
        return;
    }
    origin(
        ctx,
        onGround,
        canStandOnSnow,
        hasLightweight,
        moveInput,
        aabb,
        rot,
        dataFlag,
        fallDist,
        travel,
        state,
        region
    );
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
    if (isFrozenPlayer(ctx)) {
        clearMobTravel(travel);
        freezeStateVector(state);
        return;
    }
    origin(ctx, onGround, aabb, rot, travel, state, region);
}

// 26.32+: FinalizeMoveSystemImpl::tickFinalizeMoveSystem 已从游戏移除, hook 删除。

LL_STATIC_HOOK(
    UpdateRenderPosHook,
    ll::memory::HookPriority::High,
    &UpdateRenderPosSystem::_doUpdateRenderPosSystem,
    void,
    StrictEntityContext const&  ctx,
    StateVectorComponent const& stateVec,
    RenderPositionComponent&    renderPos
) {
    if (isFrozenPlayer(ctx)) {
        renderPos.mValue = stateVec.mPos.get();
        return;
    }
    origin(ctx, stateVec, renderPos);
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
        SROO_DEBUG("[fc] SetupCameraHook: executing toggle target={} ci={}", target, ci != nullptr);
        bool ok = ci && setVanillaDebugCamera(*ci, target);
        if (ok) {
            g_toggleRetry    = 0;
            g_FreeCamEnabled = target;
            SROO_DEBUG("[fc] SetupCameraHook: toggle ok, enabled={}", g_FreeCamEnabled);
        } else if (!target) {
            // 关闭失败（相机注册表/实体暂不可用）：保留请求、下一帧重试，最多 ~5 秒。
            // 不能直接放弃：否则调试相机标记会残留在游戏侧，重进世界时表现异常。
            if (++g_toggleRetry <= 300) {
                g_toggleTarget.store(false, std::memory_order_release);
                g_toggleRequested.store(true, std::memory_order_release);
            } else {
                SROO_WARN("FreeCam: 连续 300 帧未能还原调试相机，放弃（内部状态已清理）");
                g_toggleRetry = 0;
                resetFreeCamState();
            }
        } else {
            g_toggleRetry = 0;
            SROO_DEBUG("[fc] SetupCameraHook: enable failed (see setVanillaDebugCamera)");
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
        probeCameras();
    }
}

// ============================================================
//  Hook 3: 发包拦截（灵魂出窍期间不把移动/交互发给服务端）
// ============================================================
bool filterPacket(Packet& packet) {
    auto id = packet.getId();
    if (id == MinecraftPacketIds::PlayerAction || id == MinecraftPacketIds::InventoryTransaction) {
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
        // 锁定旋转发包：身体不跟随相机转头
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
//  Hook 4: GameMode 交互拦截（灵魂出窍时不破坏/不使用方块）
// ============================================================
LL_TYPE_INSTANCE_HOOK(
    GMStartDestroyHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$startDestroyBlock,
    bool,
    BlockPos const& pos,
    uchar           face,
    bool&           destroyed
) {
    if (g_FreeCamEnabled) return false;
    return origin(pos, face, destroyed);
}

LL_TYPE_INSTANCE_HOOK(
    GMDestroyHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$destroyBlock,
    bool,
    BlockPos const& pos,
    uchar           face
) {
    if (g_FreeCamEnabled) return false;
    return origin(pos, face);
}

LL_TYPE_INSTANCE_HOOK(
    GMContinueDestroyHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$continueDestroyBlock,
    bool,
    BlockPos const& pos,
    uchar           face,
    Vec3 const&     playerPos,
    bool&           destroyed
) {
    if (g_FreeCamEnabled) return false;
    return origin(pos, face, playerPos, destroyed);
}

LL_TYPE_INSTANCE_HOOK(
    GMUseItemHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$useItem,
    bool,
    ItemStack& item
) {
    if (g_FreeCamEnabled) return false;
    return origin(item);
}

LL_TYPE_INSTANCE_HOOK(
    GMUseItemOnHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$useItemOn,
    InteractionResult,
    ItemStack&      item,
    BlockPos const& pos,
    uchar           face,
    Vec3 const&     hit,
    Block const*    block,
    bool            firstEvent
) {
    if (g_FreeCamEnabled) return {false, false};
    return origin(item, pos, face, hit, block, firstEvent);
}

LL_TYPE_INSTANCE_HOOK(
    GMInteractHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    Actor&      entity,
    Vec3 const& loc
) {
    if (g_FreeCamEnabled) return false;
    return origin(entity, loc);
}

LL_TYPE_INSTANCE_HOOK(
    GMAttackHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$attack,
    bool,
    Actor&      entity,
    Vec3 const& hitPosition
) {
    if (g_FreeCamEnabled) return false;
    return origin(entity, hitPosition);
}

// ============================================================
//  死亡 → 自动退出灵魂出窍
//  (旁路 EventBus: 本客户端上 PlayerDieEvent 不投递, 直接 hook Player::$die)
// ============================================================
class ActorDamageSource; // 引用传参, 前置声明即可

LL_TYPE_INSTANCE_HOOK(
    PlayerDieHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$die,
    void,
    ActorDamageSource const& source
) {
    // 说明: LL 实例 hook 的 detour 以原实例作为 this 调用 (类型上伪装为 hook 类, 运行时为 Player*)
    if (g_FreeCamEnabled) {
        if (auto ci = ll::service::getClientInstance();
            ci && ci->getLocalPlayer() == reinterpret_cast<LocalPlayer*>(this)) {
            Stipuleroo::DisableFreeCamera(reinterpret_cast<Player*>(this));
        }
    }
    origin(source);
}

// ============================================================
//  Hook RAII
// ============================================================
namespace Stipuleroo {

struct FreeCamImpl {
    ll::memory::HookRegistrar<
        RotateAndSetVelocityHook,
        DefaultMoveHook,
        FlyingMoveHook,
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
        GMAttackHook,
        PlayerDieHook>
        r;
};

std::unique_ptr<FreeCamImpl> fcImpl;

void freecameraHook(bool enable) {
    if (enable) {
        if (!fcImpl) {
            fcImpl = std::make_unique<FreeCamImpl>();
            SROO_DEBUG("FreeCam: 15 hooks installed (26.40, clang-cl)");
        }
    } else {
        if (g_FreeCamEnabled) {
            DisableFreeCamera(nullptr);
        }
        fcImpl.reset();
    }
}

} // namespace Stipuleroo
