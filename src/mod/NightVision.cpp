#include "Global.h"

#include <ll/api/memory/Hook.h>
#include <ll/api/mod/NativeMod.h>
#include <ll/api/utils/SystemUtils.h>
#include <mc/client/renderer/ptexture/BaseLightData.h>
#include <mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h>
#include <mc/deps/minecraft_renderer/framebuilder/BlitFlipbookTextureDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/EditorHighlightConfiguration.h>
#include <mc/deps/minecraft_renderer/framebuilder/FadeToBlackDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/FrameBuilder.h>
#include <mc/deps/minecraft_renderer/framebuilder/FullscreenEffectDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderCameraAimAssistHighlightDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderFlameBillboardDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderParticleDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderPlayerVisionDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderShadowDescription.h>

#include <memory>

class IClientInstance;
class ScreenContext;

bool g_NightVisionEnabled = false;

// ============================================================
//  Hook 1: 光照数据 — 简约 / 高品质图形模式
// ============================================================
// 26.10 挂点: BaseLightTextureImageBuilder::_updateDarknessLightData(...)
//   → 26.32 起该方法与 Options 类型从游戏/头文件移除。
// 26.40 替代: createBaseLightTextureData(IClientInstance*, BaseLightData const&)
//   (virtual, MCAPI $-thunk 可用) → 拦截返回值强制夜视标志。
LL_TYPE_INSTANCE_HOOK(
    CreateLightDataHook,
    ll::memory::HookPriority::Normal,
    BaseLightTextureImageBuilder,
    &BaseLightTextureImageBuilder::$createBaseLightTextureData,
    std::unique_ptr<BaseLightData>,
    IClientInstance* client,
    BaseLightData const& currentData
) {
    auto result = origin(client, currentData);
    if (g_NightVisionEnabled && result) {
        result->mNightvisionActive = true;
        result->mNightvisionScale  = 1.0f;
    }
    return result;
}

// ============================================================
//  Hook 2: 灵动视效 (Deferred) — 每帧注入 RenderPlayerVisionDescription
// ============================================================
// 参考: iInfiniteNightVision — ServiceLocator<FrameBuilder>::mService 定位 +
//   frameBuilder->_insert(RenderPlayerVisionDescription{夜视全开})
// 26.40.05 定位结论 (IDA): mService 存储于 imagebase+0x11A6AD58
//   (0x151A6AD58, 函数内断言串 NonOwnerPointer<FrameBuilder>::access() 佐证);
//   另在 +0x10 (0x151A6AD68) 有直接 FrameBuilder* 缓存(qword), 用作 vtable 调用候选。
namespace {

// 镜像范围 (用于校验 vtable/指针归属)
uintptr_t gImageBase{};
size_t    gImageSize{};

bool ptrInImage(uintptr_t p) { return p >= gImageBase && p < gImageBase + gImageSize; }

mce::framebuilder::FrameBuilder* gFrameBuilder{};
void*                            gControlBlock{};
uintptr_t                        gFn1Target{};
bool                             gFbInitTried{};
bool                             gFbValid{};

void resolveFrameBuilderService() {
    auto& logger = ll::mod::NativeMod::current()->getLogger();
    if (gFbInitTried) return;
    gFbInitTried = true;

    auto range = ll::sys_utils::getImageRange();
    if (range.empty()) {
        logger.warn("FBService: getImageRange failed");
        return;
    }
    gImageBase = reinterpret_cast<uintptr_t>(range.data());
    gImageSize = range.size_bytes();
    SROO_DEBUG("FBService: image base={:x} size={:x}", gImageBase, gImageSize);

    // 原版每帧 vision-描述插入函数 (IDA: 0x143224870; 调用方不消费其返回值)
    gFn1Target = gImageBase + 0x3224870;
    SROO_DEBUG("FBService: fn1 vision-insert target = {:x}", gFn1Target);

    auto service = gImageBase + 0x11A6AD58;
    auto ctl     = *reinterpret_cast<void**>(service);
    auto fb1     = *reinterpret_cast<void**>(service + 8);
    auto fb2     = *reinterpret_cast<void**>(service + 0x10); // qword_151A6AD68
    SROO_DEBUG(
        "FBService: ctl={:x} fb1={:x} fb2={:x}",
        reinterpret_cast<uintptr_t>(ctl),
        reinterpret_cast<uintptr_t>(fb1),
        reinterpret_cast<uintptr_t>(fb2)
    );

    auto pickValid = [&](void* fb) -> mce::framebuilder::FrameBuilder* {
        if (!fb) return nullptr;
        auto vp = *reinterpret_cast<void**>(fb);
        if (!ptrInImage(reinterpret_cast<uintptr_t>(vp))) return nullptr; // vtable 须在镜像内
        return static_cast<mce::framebuilder::FrameBuilder*>(fb);
    };

    if (auto* fb = pickValid(fb2); fb) {
        gFrameBuilder = fb;
        gControlBlock = ctl;
    } else if (auto* fb = pickValid(fb1); fb) {
        gFrameBuilder = fb;
        gControlBlock = ctl;
    }
    if (!gFrameBuilder) {
        logger.warn("FBService: no valid FrameBuilder candidate (vtable check failed)");
        return;
    }
    if (!gControlBlock || *(unsigned char*)gControlBlock != 1) {
        logger.warn("FBService: control block invalid (mIsValid!=1)");
        gFrameBuilder = nullptr;
        return;
    }
    gFbValid = true;
    SROO_DEBUG(
        "FBService: VALID frameBuilder={:x} vtable={:x}",
        reinterpret_cast<uintptr_t>(gFrameBuilder),
        reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(gFrameBuilder))
    );
}

bool gInjectDisabledByCrash{};

} // namespace

uintptr_t gFn1HookId{}; // 占位（见 hook 定义处）
//  原版每帧 vision-insert 函数替换 hook
// ============================================================
// IDA 26.40.05: 0x143224870 (file VA), runtime = imageBase + 0x3224870
// 调用点(0x1431adb39/0x1431ae0b3)均不消费返回值 → 替换安全
LL_STATIC_HOOK(
    Fn1VisionHook,
    ll::memory::HookPriority::Normal,
    ll::memory::unchecked(gFn1Target),
    void,
    __int64 obj
) {
    if (g_NightVisionEnabled && gFbValid && !gInjectDisabledByCrash) {
        try {
            mce::framebuilder::RenderPlayerVisionDescription desc;
            desc.mNightVisionEnabled     = true;
            desc.mNightVisionScale       = 1.0f;
            desc.mMobEffectFogLevel      = 1.0f;
            desc.mSkyAmbientContribution = 1.0f;
            desc.mDarknessScale          = 0.0f;
            gFrameBuilder->_insert(desc);
        } catch (...) {
            gInjectDisabledByCrash = true;
            SROO_ERROR("NightVision fn1 hook: insert crashed, disabled");
            return;
        }
        static int replaceCount = 0;
        if ((++replaceCount) <= 3 || (replaceCount % 600) == 0) {
            SROO_DEBUG(
                "NightVision fn1 hook: replaced vanilla insert #{} (nv on)",
                replaceCount
            );
        }
        return; // 不调用 origin: 整体替换原版插入
    }
    origin(obj);
}

// ============================================================
//  Hook RAII
// ============================================================
namespace Stipuleroo {

struct NightVisionImpl {
    ll::memory::HookRegistrar<CreateLightDataHook, Fn1VisionHook> r;
};

std::unique_ptr<NightVisionImpl> nvImpl;

void nightVisionHook(bool enable) {
    if (enable) {
        resolveFrameBuilderService();
        if (!nvImpl) {
            nvImpl = std::make_unique<NightVisionImpl>();
            SROO_DEBUG(
                "NightVision: light-data + deferred hooks installed (26.40)"
            );
        }
    } else {
        nvImpl.reset();
    }
}

} // namespace Stipuleroo
