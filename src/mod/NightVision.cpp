#include "Global.h"

#include <ll/api/memory/Hook.h>
#include <mc/deps/minecraft_renderer/framebuilder/BgfxFrameBuilder.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderFlameBillboardDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/BlitFlipbookTextureDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderParticleDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/FadeToBlackDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/FullscreenEffectDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderCameraAimAssistHighlightDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/RenderShadowDescription.h>
#include <mc/deps/minecraft_renderer/framebuilder/gamecomponents/mfc/EditorHighlightConfiguration.h>
using namespace ll::memory_literals; // 提供 _sig 签名字面量

bool g_NightVisionEnabled = false;

// ============================================================
//  SDK 中 RenderPlayerVisionDescription 为空壳
//  以下字段根据二进制逆向手动声明 (未公开)
// ============================================================
namespace mce::framebuilder {

struct RenderPlayerVisionDescription {
    bool  mNightVisionEnabled;     // 夜视开关
    float mNightVisionScale;       // 夜视亮度倍率 (1=正常)
    float mMobEffectFogLevel;      // 雾浓度 (1=完全去雾)
    float mSkyAmbientContribution; // 天空环境光贡献 (1=最亮)
    float mDarknessScale;          // 黑暗效果强度 (0=完全移除)
};

} // namespace mce::framebuilder

// ============================================================
//  Hook: BgfxFrameBuilder::_insert
//  _insert 在 SDK 中未公开，通过字节签名 (_sig) 定位
//  const_cast: 渲染管线传入的是 const&，仅修改视觉字段 (安全)
// ============================================================
LL_AUTO_TYPE_INSTANCE_HOOK(
    NightVisionHook,
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
            vi.mNightVisionScale       = 1;
            vi.mMobEffectFogLevel      = 1;
            vi.mSkyAmbientContribution = 1;
            vi.mDarknessScale          = 0;
        }
    }
    return origin(std::move(description));
}

namespace Stipuleroo {
void nightVisionHook(bool enable) { (void)enable; }
} // namespace Stipuleroo