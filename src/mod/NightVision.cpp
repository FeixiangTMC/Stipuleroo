#include "Global.h"

#include <ll/api/memory/Hook.h>
#include <mc/client/options/Options.h>
#include <mc/client/renderer/ptexture/BaseLightTextureImageBuilder.h>
#include <mc/deps/minecraft_renderer/framebuilder/BgfxFrameBuilder.h>
#include <mc/deps/minecraft_renderer/framebuilder/BlitFlipbookTextureDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/FadeToBlackDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/FullscreenEffectDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderCameraAimAssistHighlightDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderFlameBillboardDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderParticleDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderShadowDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/gamecomponents/mfc/EditorHighlightConfiguration.h>
#include <mc/world/actor/player/Player.h>
using namespace ll::memory_literals;

bool g_NightVisionEnabled = false;

// ============================================================
//  BaseLightData — SDK 未公开，手动声明
// ============================================================
class BaseLightData {
public:
    mce::Color     mSunriseColor;
    float          mGamma;
    float          mSkyDarken;
    DimensionType  mDimensionType;
    float          mDarkenWorldAmount;
    float          mPreviousDarkenWorldAmount;
    bool           mNightvisionActive;
    float          mNightvisionScale;
    bool           mUnderwaterVision;
    float          mUnderwaterScale;
    int            mSkyFlashTime;
    float          mDarknessFactor;
    float          mDarknessFactorPreviousFrame;
};

// ============================================================
//  RenderPlayerVisionDescription — SDK 未公开，手动声明
// ============================================================
namespace mce::framebuilder {

struct RenderPlayerVisionDescription {
    bool  mNightVisionEnabled;
    float mNightVisionScale;
    float mMobEffectFogLevel;
    float mSkyAmbientContribution;
    float mDarknessScale;
};

} // namespace mce::framebuilder

// ============================================================
//  Hook 1: 光照数据 — 简约 / 花式图形模式
//  强制 mNightvisionActive/mNightvisionScale，让光照系统按夜视渲染
// ============================================================
LL_AUTO_STATIC_HOOK(
    LightDataHook,
    HookPriority::Normal,
    ll::memory::unchecked(&BaseLightTextureImageBuilder::_updateDarknessLightData),
    void,
    BaseLightData& baseLightData,
    Player const&  player,
    Options const& options
) {
    if (g_NightVisionEnabled) {
        baseLightData.mNightvisionActive = true;
        baseLightData.mNightvisionScale  = 1.0f;
    }
    origin(baseLightData, player, options);
}

// ============================================================
//  Hook 2: FrameBuilder::_insert — 灵动视效图形模式
//  灵动视效每帧固定产生一份 RenderPlayerVisionDescription（mNightVisionEnabled=false）
//  在此直接修改路过描述，无需主动插入
// ============================================================
LL_AUTO_TYPE_INSTANCE_HOOK(
    FrameBuilderInsertHook,
    HookPriority::Normal,
    mce::framebuilder::BgfxFrameBuilder,
    "48 81 EC C8 00 00 00 44 8B 05 32 19 3C 03"_sig,
    void,
    std::variant<
        std::reference_wrapper<mce::framebuilder::RenderFlameBillboardDescription const>,
        std::reference_wrapper<mce::framebuilder::BlitFlipbookTextureDescription const>,
        std::reference_wrapper<mce::framebuilder::RenderParticleDescription const>,
        std::reference_wrapper<mce::framebuilder::RenderPlayerVisionDescription const>,
        std::reference_wrapper<mce::framebuilder::RenderShadowDescription const>,
        std::reference_wrapper<mce::framebuilder::FadeToBlackDescription const>,
        std::reference_wrapper<mce::framebuilder::RenderCameraAimAssistHighlightDescription const>,
        std::reference_wrapper<mce::framebuilder::FullscreenEffectDescription const>,
        std::reference_wrapper<MFC::EditorHighlightConfiguration const>> description
) {
    if (g_NightVisionEnabled) {
        if (auto* vision =
                std::get_if<std::reference_wrapper<
                    mce::framebuilder::RenderPlayerVisionDescription const>>(
                    &description);
            vision) {
            auto& vi = const_cast<mce::framebuilder::RenderPlayerVisionDescription&>(
                vision->get());
            vi.mNightVisionEnabled     = true;
            vi.mNightVisionScale       = 1.0f;
            vi.mMobEffectFogLevel      = 1.0f;
            vi.mSkyAmbientContribution = 1.0f;
            vi.mDarknessScale          = 0.0f;
        }
    }
    return origin(std::move(description));
}

// ============================================================
//  Hook RAII
// ============================================================
namespace Stipuleroo {

struct NightVisionImpl {
    ll::memory::HookRegistrar<LightDataHook, FrameBuilderInsertHook> r;
};

std::unique_ptr<NightVisionImpl> nvImpl;

void nightVisionHook(bool enable) {
    if (enable) {
        if (!nvImpl) nvImpl = std::make_unique<NightVisionImpl>();
    } else {
        nvImpl.reset();
    }
}

} // namespace Stipuleroo
