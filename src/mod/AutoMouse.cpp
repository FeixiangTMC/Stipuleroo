// ============================================================
//  Stipuleroo —— 自动鼠标操作（替代手动点鼠标）
//
//  三个板块（**互斥**：同一时刻只会有一个模式生效，切换到另一个会自动关掉前一个）：
//    1) 连续左键点击 LeftClick  —— 按配置间隔攻击准星目标（无目标时空挥）
//    2) 连续右键点击 RightClick —— 按配置间隔对目标方块/物品“使用”（放置、交互、右键）
//    3) 持续左键长按 HoldLeft   —— 持续挖掘准星方块（每个客户端 tick 推进破坏进度）
//
//  实现思路（参考 TweakerRo 的 tweakPeriodicAttack / tweakHoldAttack 语义）：
//    * 直接调用游戏自身 GameMode 的
//      attack / useItem / useItemOn / startDestroyBlock / continueDestroyBlock / stopDestroyBlock
//      —— 与玩家真的按下鼠标时游戏调的入口相同，射线检测、手臂动画、发包都交给游戏。
//    * 【关键·服务端同步】放置方块必须调用 `GameMode::buildBlock(pos, face, false)`
//      —— 这是原版"连续建造"的服务端权威入口（服务端在 `pos.neighbor(face)` 落方块）。
//      只调用 `GameMode::useItemOn(...)` 只有本地预测、服务端收不到 → **客户端幽灵方块**。
//      依据：本模组 AutoBridge（26.40 实测可用）用的就是 buildBlock；
//      参考模组 LHolo-26.20.9 的 PlaceHelper/PlacementExecutor 明确写着
//      "GameMode::useItemOn only predicts locally … so neither persists"。
//      非方块物品（打火石/锄头等）仍走 `Player::startItemUseOn` + `useItemOn` 组合。
//    * 目标取自客户端每帧维护的视线命中 `Player::getLevel().getHitResult()`。
//    * 驱动源：ClientLevelTickEvent（客户端 tick，20/s）：
//      - 点击类（左键点击/右键点击）：按 config.json 的间隔（0.05 ~ 1000 秒）触发；
//      - 长按左键（挖掘）：**输入层伪造“鼠标左键按住”**（见下方「输入层模拟」），
//        由原版自己跑完挖掘链路（客户端破坏进度 / 裂纹渲染 / 音效 / 发包）。
//    * 打开任何界面（背包/聊天/表单等，非 hud_screen）时暂停，避免误操作。
//    * 「持续右键长按」曾实现过，但实测没有实际效果（服务端不接受这种长按放置），已删除。
//
//  ⚠️ 「持续左键长按（挖掘）」为什么必须走输入层（26.40 实测，详见
//     docs/migration-26.40.md §9.11~9.16）：
//      只调 GameMode::start/continueDestroyBlock 时，方块确实会被挖掉（那是**服务端**进度），
//      但客户端渲染的破坏裂纹一直闪在最低级 —— 因为客户端裂纹来自渲染侧的
//      `LevelRendererPlayer::mDestroyingBlockList`（updateDestroyBlock/updateDestroyProgress），
//      它只被**原版输入管线**（鼠标左键按住）喂数据。因此本模块给游戏的鼠标设备喂
//      真实的“左键按下/松开”事件，让原版把整条链路跑完。
// ============================================================
#include "Global.h"

#include "Config.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Symbol.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/InputHandler.h"
#include "mc/deps/input/Mouse.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"
#include "mc/entity/components/PlayerActionComponent.h"
#include "mc/entity/components/PlayerBlockActionData.h"
#include "mc/entity/components/PlayerBlockActions.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include <Windows.h>
#include <chrono>
#include <memory>

AutoMouseMode g_AutoMouseMode = AutoMouseMode::Off;

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point      g_lastClick{};     // 上次“点击类”动作的时间
BlockPos               g_destroyPos{};    // 正在挖掘的方块
uchar                  g_destroyFace{};   // 开始挖掘时记下的命中面（continue 必须沿用同一个面）
bool                   g_destroying{};    // 是否已 startDestroyBlock（需要 stopDestroyBlock 收尾）
int                    g_mineSwingTick{}; // 挖掘挥手节奏计数
ll::event::ListenerPtr g_tickListener;

// ---- 输入层（鼠标设备）状态：见下方「输入层模拟」 ----
MouseDevice* g_mouseDevice{};     // 游戏的全局鼠标设备（Mouse::_instance()）
bool         g_lmbHoldInjected{}; // 我们伪造的“左键按住”当前是否已送出

int clickIntervalMs(AutoMouseMode mode) {
    auto& cfg = StipulerooConfig::get();
    return mode == AutoMouseMode::RightClick ? cfg.getAutoMouseRightClickIntervalMs()
                                            : cfg.getAutoMouseLeftClickIntervalMs();
}

// 当前视线目标（客户端每帧维护）
[[nodiscard]] HitResult& currentHit(Player& player) { return player.getLevel().getHitResult(); }

// 面法线（MC 约定：0=-Y, 1=+Y, 2=-Z, 3=+Z, 4=-X, 5=+X）
[[nodiscard]] BlockPos neighborOf(BlockPos const& pos, uchar face) {
    static constexpr int kDx[6] = {0, 0, 0, 0, -1, 1};
    static constexpr int kDy[6] = {-1, 1, 0, 0, 0, 0};
    static constexpr int kDz[6] = {0, 0, -1, 1, 0, 0};
    int const            f      = (face < 6) ? static_cast<int>(face) : 1;
    return BlockPos{pos.x + kDx[f], pos.y + kDy[f], pos.z + kDz[f]};
}

// 结束“长按挖掘”状态：切换模式 / 关闭 / 退出世界时必须调用，
// 客户端“正在破坏方块”的动作状态（PlayerActionComponent，客户端专属接口）。
// 原版输入路径在挖掘时会调 addStartDestroyBlock / addContinueDestroyBlock，
// 客户端自身的破坏进度（裂纹渲染）跟着这套状态走；只调 GameMode 的话
// 方块照样被服务端挖掉，但界面裂纹可能停在最低级闪烁。
[[nodiscard]] PlayerActionComponent* destroyActionComp(Player& player) {
    auto comp = player.getEntityContext().tryGetComponent<PlayerActionComponent>();
    return comp ? &*comp : nullptr;
}

// 组件里是否已经有针对该方块的破坏动作（GameMode 通常会自己记进去，
// 这时就不能再补一条，否则一个 tick 会发出两条相同的动作）
[[nodiscard]] bool hasDestroyActionFor(PlayerActionComponent& comp, BlockPos const& pos) {
    PlayerBlockActions& actions = comp.mPlayerBlockActions;
    for (auto const& action : actions.get()) {
        auto type = action.mPlayerActionType;
        if (type == PlayerActionType::StartDestroyBlock || type == PlayerActionType::ContinueDestroyBlock
            || type == PlayerActionType::PredictDestroyBlock || type == PlayerActionType::CrackBlock) {
            if (action.mPos.get() == pos) return true;
        }
    }
    return false;
}

// 结束“长按挖掘”状态：切换模式 / 关闭 / 退出世界时必须调用，
// 否则游戏侧会残留破坏进度（缺少 stopDestroyBlock + 客户端破坏动作状态）
void stopDestroying(Player& player) {
    g_mineSwingTick = 0;
    if (!g_destroying) return;
    g_destroying = false;
    // 注意：不要手动改 Player::mDestroyingBlock —— 实测它由原版输入管线每 tick 维护，
    // 我们置位会被立刻清掉（日志 destroying=false），而且会让挖掘本身失效。
    if (player.mGameMode) {
        player.mGameMode->stopDestroyBlock(g_destroyPos);
    }
    if (auto* action = destroyActionComp(player)) {
        action->addStopDestroyBlock();
    }
}

// ---- 1) 连续左键点击：挥手臂 + 攻击 ----
void doLeftClick(Player& player) {
    auto& gm = *player.mGameMode;
    auto& hr = currentHit(player);

    // 原版点左键一定先挥手：先触发手部动画（发包由游戏处理），有目标再补攻击
    player.swing(ActorSwingSource::Attack);

    if (hr.mType == HitResultType::Entity) {
        if (auto* target = hr.getEntity(); target && target != &player) {
            gm.attack(*target, hr.mPos);
        }
    }
}

// ---- 2) 连续右键点击：放置 / 使用 ----
void doRightClick(Player& player) {
    auto& gm   = *player.mGameMode;
    auto& item = const_cast<ItemStack&>(player.getSelectedItem()); // 游戏侧接口只暴露 const 访问器
    auto& hr   = currentHit(player);

    if (hr.mType == HitResultType::Tile) {
        // ★ 放置方块必须走游戏自己的“连续建造”入口 GameMode::buildBlock(...)：
        //   它是**服务端权威**路径（服务端会在 hr.mBlock.neighbor(hr.mFacing) 落方块）。
        //   GameMode::useItemOn 只做本地预测、服务端收不到 → 客户端幽灵方块。
        //   依据：
        //     · 本模组 AutoBridge（26.40 实测可用）用的就是 lp->mGameMode->buildBlock(...)；
        //     · 参考模组 LHolo-26.20.9（PlaceHelper.cpp / PlacementExecutor.cpp）明确写着
        //       “GameMode::useItemOn only predicts locally … so neither persists”，
        //       它对右键放置的接管点是 startBuildBlock/buildBlock/stopBuildBlock/useItem，
        //       并且其自建 ItemUseInventoryTransaction 也是直接 buildBlock 同级的事务包。
        if (item.getBlockForRendering()) {
            gm.buildBlock(hr.mBlock, hr.mFacing, false);
            return;
        }

        // 非方块物品（打火石/锄头/桶/食物…）：保持“对方块使用物品”，一次点击=按下-松开一个完整周期
        Block const& block    = player.getDimensionBlockSource().getBlock(hr.mBlock);
        BlockPos     buildPos = neighborOf(hr.mBlock, hr.mFacing);
        player.startItemUseOn(hr.mFacing, hr.mBlock, buildPos, item);
        gm.useItemOn(item, hr.mBlock, hr.mFacing, hr.mPos, &block, true);
        player.stopItemUseOn(hr.mBlock, item);
        return;
    }

    // 对着空气：使用物品（吃/投掷/放水…）
    gm.useItem(item);
}

// 原版挖掘管线是否已经接管（接管后我们一根手指都不碰，全部交给游戏自己跑）
//
// 判定依据（26.40 实测）：
//   · Player::mDestroyingBlock —— 客户端“正在挖掘方块”的状态位，**由原版输入管线每 tick 维护**。
//     只要输入层（鼠标左键按住）生效，它就会是 true；我们自己置位则会被立刻清掉（第九轮实测）。
//   · GameMode::mDestroyProgress > 0 —— 客户端 GameMode 侧的破坏进度（原版接管时才会推进）。
[[nodiscard]] bool vanillaMiningEngaged(Player& player) {
    return player.mDestroyingBlock || (player.mGameMode && player.mGameMode->mDestroyProgress > 0.0f);
}

// ---- 3) 持续左键长按：挖掘 ----

// [诊断] debugLog=true 时，**只在“原版接管状态翻转”时**各打一条
// （不要按 tick/帧周期刷 —— InputHandler::tick 是每帧调用的，周期性日志会刷屏）：
//   vanilla=true                → 原版输入管线接管（裂纹由原版渲染，正常）
//   gm.progress / destroying    → 客户端侧进度与“正在挖掘”状态位
//   devBtn=[0/1]                → 鼠标设备按键状态（哪一位为 1 = 左键状态所在下标）
//   compActions                 → 兜底路径排队在 PlayerActionComponent 里的破坏动作数
void logHoldLeftDiag(Player& player, bool fallbackPath) {
    static int lastVanilla = -1;
    int const  vanilla     = vanillaMiningEngaged(player) ? 1 : 0;
    if (vanilla == lastVanilla) return;
    lastVanilla = vanilla;
    auto*  ac          = destroyActionComp(player);
    size_t actionCount = size_t(-1);
    if (ac) {
        PlayerBlockActions& acts = ac->mPlayerBlockActions;
        actionCount              = acts.get().size();
    }
    int devBtn0 = -1;
    int devBtn1 = -1;
    if (g_mouseDevice) {
        devBtn0 = static_cast<int>(g_mouseDevice->_buttonStates[0]);
        devBtn1 = static_cast<int>(g_mouseDevice->_buttonStates[1]);
    }
    auto& gm = *player.mGameMode;
    SROO_DEBUG(
        "AutoMouse/HoldLeft{}: vanilla={} lmbHold={} devBtn=[{},{}] gm.progress={:.4f} old={:.4f} destroying={} "
        "pos=({},{},{}) compActions={}",
        fallbackPath ? "(兜底)" : "",
        vanillaMiningEngaged(player),
        g_lmbHoldInjected,
        devBtn0,
        devBtn1,
        gm.mDestroyProgress,
        gm.mOldDestroyProgress,
        static_cast<bool>(player.mDestroyingBlock), // 只读诊断：确认它是否被原版管线保持
        g_destroyPos.x,
        g_destroyPos.y,
        g_destroyPos.z,
        actionCount
    );
}

void doHoldLeft(Player& player) {
    // ★★ 输入层注入（见下方「输入层模拟」）已经让游戏以为玩家按住了鼠标左键。
    //    一旦原版管线接管，客户端的挖掘进度 **与裂纹渲染** 都在原版管线里，
    //    我们什么都不要做：任何额外的 start/continue/stop 都可能把裂纹进度重置回 0
    //    （就是之前“裂纹一直闪在最低级”的成因）。
    if (vanillaMiningEngaged(player)) {
        g_destroying    = false; // 原版接管期间不再维护我们自己的挖掘状态
        g_mineSwingTick = 0;
        logHoldLeftDiag(player, /*fallbackPath=*/false);
        return;
    }

    // ---------- 以下为兜底路径：输入层注入不可用（符号没解析到）时才走 ----------
    auto& gm = *player.mGameMode;
    auto& hr = currentHit(player);

    if (hr.mType != HitResultType::Tile) {
        stopDestroying(player);
        player.swing(ActorSwingSource::Attack); // 对着空气：空挥
        return;
    }

    bool destroyed = false;
    if (!g_destroying || hr.mBlock != g_destroyPos) {
        // 目标变化（或刚开始）：先收尾旧目标，再开始挖新目标
        stopDestroying(player);
        g_destroyPos  = hr.mBlock;
        g_destroyFace = hr.mFacing; // 记下起始面：continue 时必须用同一个面，否则会被当成新目标而重置进度
        g_destroying  = true;
        gm.startDestroyBlock(g_destroyPos, g_destroyFace, destroyed);
        if (auto* action = destroyActionComp(player); action && !hasDestroyActionFor(*action, g_destroyPos)) {
            action->addStartDestroyBlock(g_destroyPos, static_cast<int>(g_destroyFace));
        }
        return;
    }

    // 破坏进度逐 tick 推进；手部动画 + 裂纹动作按 ~5 tick 节奏补一次（与原版挖掘观感一致）
    gm.continueDestroyBlock(g_destroyPos, g_destroyFace, player.getPosition(), destroyed);
    if (auto* action = destroyActionComp(player); action && !hasDestroyActionFor(*action, g_destroyPos)) {
        action->addContinueDestroyBlock(g_destroyPos, static_cast<int>(g_destroyFace));
    }
    if ((++g_mineSwingTick % 5) == 0) {
        player.swing(ActorSwingSource::Mine);
        // 把"裂纹阶段"动作也发给服务端（原版客户端挖掘时会周期性发 CrackBlock），
        // 服务端再广播回来驱动方块裂纹贴图 —— 实测只调 GameMode 时裂纹不会推进
        if (auto* action = destroyActionComp(player)) {
            action->addCrackBlock(g_destroyPos, static_cast<int>(g_destroyFace));
        }
    }

    logHoldLeftDiag(player, /*fallbackPath=*/true);

    // ★ 这里刻意**不**因为 destroyed 去 stopDestroyBlock：startDestroyBlock 会把进度清零，
    //   旧实现每 tick “stop → start” 会让裂纹每 tick 归零（= 一直闪在最低级）。
    //   方块真被破坏后，下一 tick 的视线命中会变成别的方块（或空气），按分支自然重新开始。
    //   （输入层注入生效时，本兜底路径根本不会被执行，一切由原版跑。）
}

// ============================================================
//  输入层模拟（26.40）：伪造「鼠标左键按住 / 松开」的设备事件
//
//  为什么必须是输入层（前四轮实测结论，见 docs/migration-26.40.md §9.11~9.15）：
//    · GameMode::mDestroyProgress 恒为 0 —— 我们调 start/continueDestroyBlock 只把
//      挖掘行为送到服务端（方块能正常被挖掉），客户端的“破坏进度/裂纹”链路完全不动；
//    · 周期性 addCrackBlock、置位 Player::mDestroyingBlock 都试过：前者只影响服务端，
//      后者由原版每 tick 清掉（还会让挖掘失效）；
//    · 客户端裂纹真正的数据源是渲染侧的 LevelRendererPlayer::mDestroyingBlockList
//      （updateDestroyBlock(pos, rate) 喂速率、updateDestroyProgress() 每帧推进），
//      而**只有原版挖掘管线会喂它** —— 原版挖掘管线的唯一开关就是“鼠标左键按住”。
//  ⇒ 想让裂纹像原版一样平滑推进，只能让游戏自己以为玩家按住了左键。
//
//  做法：直接给游戏全局的鼠标设备喂一个真实的按键事件（Windows 收到 WM_LBUTTONDOWN
//  时游戏走的就是这个入口，LHolo / Sapphire 也都挂它）：
//      MouseDevice& dev = Mouse::_instance();
//      dev.feed(MouseAction::ActionLeft, MouseAction::DataDown, dev._x, dev._y, 0, 0, false);
//  之后按键绑定、button.attack 的 down handler、每 tick 的挖掘推进、裂纹贴图、
//  音效、发包全部由原版完成 —— 我们不需要知道任何 button id。
//  停止时补一个 DataUp（等价于松开左键）。
//
//  符号用 ll::memory::SymbolView{修饰名}.resolve() 取（失败返回 nullptr 并报错，
//  不会像 delay import 那样在加载期炸掉）；修饰名是 clang-cl 实测得到的：
//      ?_instance@Mouse@@SAAEAVMouseDevice@@XZ
//      ?feed@MouseDevice@@QEAAXDCFFFF_N@Z      → (char, schar, short, short, short, short, bool)
// ============================================================
using MouseInstanceFn = MouseDevice& (*)();
using MouseFeedFn     = void (*)(MouseDevice*, char, schar, short, short, short, short, bool);

MouseInstanceFn g_mouseInstanceFn{}; // Mouse::_instance()
MouseFeedFn     g_mouseFeedFn{};     // MouseDevice::feed（this 走第一个参数）
int             g_mouseResolveTries{};
int             g_mouseInjectCount{};
int             g_mouseReleaseCount{};
int             g_lmbStateIndex{-1}; // “左键”落在 MouseDevice::_buttonStates 的哪个下标（-1=未校准）
int             g_lmbRefeedTicks{};
int             g_lmbRefeedCount{};

// 解析两个函数的地址（只做地址解析，不碰游戏对象 —— 模组启用得很早，游戏静态对象可能还没构造好）
void resolveMouseInputSymbols() {
    if (!g_mouseInstanceFn) {
        g_mouseInstanceFn = reinterpret_cast<MouseInstanceFn>(
            ll::memory::SymbolView{"?_instance@Mouse@@SAAEAVMouseDevice@@XZ"}.resolve()
        );
    }
    if (!g_mouseFeedFn) {
        g_mouseFeedFn = reinterpret_cast<MouseFeedFn>(
            ll::memory::SymbolView{"?feed@MouseDevice@@QEAAXDCFFFF_N@Z"}.resolve()
        );
    }
    SROO_DEBUG(
        "AutoMouse/Input: 输入层符号 instance={} feed={}",
        reinterpret_cast<void*>(g_mouseInstanceFn),
        reinterpret_cast<void*>(g_mouseFeedFn)
    );
}

// 取全局鼠标设备（延迟到输入 tick 里做：那时输入系统一定已经就绪）
bool ensureMouseDevice() {
    if (g_mouseDevice) return true;
    if (!g_mouseInstanceFn) return false;
    g_mouseDevice = std::addressof(g_mouseInstanceFn());
    SROO_DEBUG("AutoMouse/Input: 鼠标设备 device={}", static_cast<void*>(g_mouseDevice));
    return g_mouseDevice != nullptr;
}

void feedLeftMouseButton(schar data) {
    if (!g_mouseDevice || !g_mouseFeedFn) return;
    MouseDevice& dev = *g_mouseDevice;
    g_mouseFeedFn(
        &dev,
        static_cast<char>(MouseAction::ActionLeft),
        data,
        dev._x,
        dev._y,
        0, // 按键事件不需要移动量
        0,
        false
    );
}

// 需要按住时确保设备是“左键按下”，不需要时确保松开（只在状态切换时发事件）
void maintainSyntheticLmbHold(bool wantHold) {
    if (!g_mouseFeedFn || !ensureMouseDevice()) {
        // 设备/符号还没拿到：最多试 ~5 秒，失败就由 GameMode 兜底路径负责挖掘
        if (g_mouseResolveTries >= 100) return;
        if ((g_mouseResolveTries++ % 20) == 0) resolveMouseInputSymbols();
        if (!g_mouseFeedFn || !ensureMouseDevice()) return;
    }

    if (wantHold == g_lmbHoldInjected) {
        // 自愈：我们按下的那次在设备上被别处清掉了（玩家真的点了一下鼠标 / 窗口失焦 /
        // 进过界面）→ 限频补发一次“按下”，否则自动挖掘会无声停住。
        // 注意只在**校准过下标**、且该下标确实回到 0 时才补，绝不盲目重发（重发会重置挖掘进度）。
        if (wantHold && g_lmbHoldInjected && g_lmbStateIndex >= 0
            && g_mouseDevice->_buttonStates[g_lmbStateIndex] == 0 && ++g_lmbRefeedTicks >= 10) {
            g_lmbRefeedTicks = 0;
            feedLeftMouseButton(MouseAction::DataDown);
            // 日志限频：只打第 1 次，之后每 100 次一条（避免自愈反复触发时刷屏）
            if (++g_lmbRefeedCount == 1 || (g_lmbRefeedCount % 100) == 0) {
                SROO_DEBUG("AutoMouse/Input: 补发左键按下（设备状态被清）第 {} 次", g_lmbRefeedCount);
            }
        }
        return;
    }

    char before[5]{};
    for (int i = 0; i < 5; ++i) before[i] = g_mouseDevice->_buttonStates[i];

    feedLeftMouseButton(wantHold ? MouseAction::DataDown : MouseAction::DataUp);
    g_lmbHoldInjected = wantHold;
    if (wantHold) {
        ++g_mouseInjectCount;
        // 校准“左键存储在 _buttonStates 的哪个下标”（按下前后由 0 变 1 的那一位）
        if (g_lmbStateIndex < 0) {
            for (int i = 0; i < 5; ++i) {
                if (before[i] == 0 && g_mouseDevice->_buttonStates[i] != 0) {
                    g_lmbStateIndex = i;
                    break;
                }
            }
            SROO_DEBUG("AutoMouse/Input: 左键在 _buttonStates 的下标 = {}", g_lmbStateIndex);
        }
    } else {
        ++g_mouseReleaseCount;
    }
    SROO_DEBUG(
        "AutoMouse/Input: 伪造鼠标左键 {} (inject={} release={})",
        wantHold ? "按下" : "松开",
        g_mouseInjectCount,
        g_mouseReleaseCount
    );
}

void releaseSyntheticLmbHold() {
    if (g_lmbHoldInjected) maintainSyntheticLmbHold(false);
}

LL_TYPE_INSTANCE_HOOK(
    InputHandlerTickHook,
    ll::memory::HookPriority::Normal,
    InputHandler,
    &InputHandler::tick,
    void,
    IMinecraftGame*                                      mcGame,
    IClientInstance&                                     client,
    Bedrock::NotNullNonOwnerPtr<ControllerIDtoClientMap> const& controllerClientMap,
    bool                                                 allowMultipleClients
) {
    // ★ 在输入管线处理本帧输入**之前**，把“左键按住/松开”喂给鼠标设备：
    //   原版随后会自己完成 按键绑定 → button.attack down handler → 逐 tick 挖掘推进
    //   → 客户端破坏进度 / 裂纹渲染 / 音效 / 发包。
    bool const uiOpen   = client.getScreenName() != "hud_screen";
    bool const wantHold = (g_AutoMouseMode == AutoMouseMode::HoldLeft) && !uiOpen && !g_FreeCamEnabled;
    maintainSyntheticLmbHold(wantHold);

    origin(mcGame, client, controllerClientMap, allowMultipleClients);
}

struct AutoMouseHooks {
    ll::memory::HookRegistrar<InputHandlerTickHook> r;
};
std::unique_ptr<AutoMouseHooks> g_autoMouseHooks;

void onClientTick() {
    if (g_AutoMouseMode == AutoMouseMode::Off) return;

    auto ci = ll::service::getClientInstance();
    if (!ci) return;
    auto* player = ci->getLocalPlayer();
    if (!player || !player->mGameMode) return;
    // 任何界面打开时暂停（背包/聊天/表单…），避免误触
    if (ci->getScreenName() != "hud_screen") return;

    // 「持续左键长按（挖掘）」必须逐 tick 推进破坏进度，不能按间隔节流（否则比原版慢）
    if (g_AutoMouseMode == AutoMouseMode::HoldLeft) {
        doHoldLeft(*player);
        return;
    }

    // 其余两种模式：按各自的配置间隔触发
    auto const now      = Clock::now();
    auto const interval = std::chrono::milliseconds(clickIntervalMs(g_AutoMouseMode));
    if (g_lastClick.time_since_epoch().count() != 0 && now - g_lastClick < interval) return;
    g_lastClick = now;

    switch (g_AutoMouseMode) {
    case AutoMouseMode::LeftClick:
        doLeftClick(*player);
        break;
    case AutoMouseMode::RightClick:
        doRightClick(*player); // 每次点击都是一次新的“按下-松开”
        break;
    default:
        break;
    }
}

[[nodiscard]] char const* modeName(AutoMouseMode mode) {
    switch (mode) {
    case AutoMouseMode::LeftClick:
        return "连续左键点击（攻击）";
    case AutoMouseMode::RightClick:
        return "连续右键点击（放置/使用）";
    case AutoMouseMode::HoldLeft:
        return "持续左键长按（挖掘）";
    default:
        return "已关闭";
    }
}

[[nodiscard]] std::string keyHint(std::string const& key) {
    return key.empty() ? std::string{"（未设置快捷键）"} : fmt::format("（快捷键: {}）", key);
}

} // namespace

// ============================================================
//  公共 API
// ============================================================
namespace Stipuleroo {

// 切换到指定模式（传 Off 表示全关）。
// 三种操作互斥：切到新模式时会把上一个模式的游戏侧状态收尾（停止挖掘）。
void SetAutoMouseMode(AutoMouseMode mode) {
    auto  ci     = ll::service::getClientInstance();
    auto* player = ci ? ci->getLocalPlayer() : nullptr;

    if (g_AutoMouseMode != mode) {
        SROO_DEBUG("AutoMouse: {} -> {}", modeName(g_AutoMouseMode), modeName(mode));
    }
    if (player) {
        stopDestroying(*player); // 结束挖掘状态
    }
    // 交给输入层的“按住左键”也要同步松开，否则切换模式后游戏会一直以为左键按着
    releaseSyntheticLmbHold();

    g_AutoMouseMode = mode;
    g_lastClick     = Clock::now(); // 切换后立即允许第一次动作
}

void ToggleAutoMouseMode(AutoMouseMode mode) {
    SetAutoMouseMode(g_AutoMouseMode == mode ? AutoMouseMode::Off : mode);
}

void ForceStopAutoMouse() {
    auto  ci     = ll::service::getClientInstance();
    auto* player = ci ? ci->getLocalPlayer() : nullptr;
    if (player) {
        stopDestroying(*player);
    }
    releaseSyntheticLmbHold(); // 松开我们伪造的“左键按住”
    // 退出世界 / 死亡时把设备指针作废，下次用到时重新取（避免指向已重建的对象）
    g_mouseDevice     = nullptr;
    g_lmbHoldInjected = false;
    g_AutoMouseMode   = AutoMouseMode::Off;
    g_lastClick       = Clock::time_point{};
}

void showAutoMouseForm(Player& player) {
    auto& cfg = StipulerooConfig::get();

    // 客户端命令的 origin 可能是本地玩家（没有网络连接，发不出表单），
    // 因此优先取同一玩家的“服务端玩家”对象（集成服务器），拿不到才退回 origin 本身。
    Player* target = &player;
    if (auto level = ll::service::getLevel(); level.has_value()) {
        if (auto* serverPlayer = level->getPlayer(player.getUuid()); serverPlayer) {
            target = serverPlayer;
        } else if (auto* byName = level->getPlayer(player.getRealName()); byName) {
            target = byName;
        }
    }

    ll::form::SimpleForm form(
        "Stipuleroo - 自动鼠标操作",
        fmt::format(
            "§7三种操作互斥，同一时刻只会开启一个；再选一次当前项即关闭。\n"
            "当前状态: §a{}\n"
            "§7左键间隔 {:.2f}s   右键间隔 {:.2f}s\n"
            "§7（可在 config.json 调整，范围 0.05 ~ 1000 秒）",
            modeName(g_AutoMouseMode),
            cfg.autoMouseLeftClickInterval,
            cfg.autoMouseRightClickInterval
        )
    );

    auto buttonText = [](AutoMouseMode mode, char const* label, std::string const& key) {
        return g_AutoMouseMode == mode
                 ? fmt::format("§a▶ {} §7[当前开启]§r {}", label, keyHint(key))
                 : fmt::format("§f{} {}", label, keyHint(key));
    };

    form.appendButton(buttonText(AutoMouseMode::LeftClick, "连续左键点击（攻击）", cfg.autoMouseLeftClickKey));
    form.appendButton(buttonText(AutoMouseMode::RightClick, "连续右键点击（放置/使用）", cfg.autoMouseRightClickKey));
    form.appendButton(buttonText(AutoMouseMode::HoldLeft, "持续左键长按（挖掘）", cfg.autoMouseHoldLeftKey));
    form.appendButton(
        g_AutoMouseMode == AutoMouseMode::Off ? "§7关闭自动鼠标操作（当前未开启）" : "§c关闭自动鼠标操作"
    );

    form.sendTo(*target, [](Player& p, int index, ll::form::FormCancelReason) {
        switch (index) {
        case 0:
            ToggleAutoMouseMode(AutoMouseMode::LeftClick);
            break;
        case 1:
            ToggleAutoMouseMode(AutoMouseMode::RightClick);
            break;
        case 2:
            ToggleAutoMouseMode(AutoMouseMode::HoldLeft);
            break;
        case 3:
            SetAutoMouseMode(AutoMouseMode::Off);
            break;
        default:
            return; // 玩家取消表单
        }

        p.sendMessage(fmt::format("§b[Stipuleroo] §r自动鼠标操作: §a{}", modeName(g_AutoMouseMode)));
        // 立刻重开表单，让“▶ 当前开启”状态直接可见
        showAutoMouseForm(p);
    });
}

void autoMouseHook(bool enable) {
    auto& bus = ll::event::EventBus::getInstance();
    if (enable) {
        if (!g_tickListener) {
            g_tickListener = bus.emplaceListener<ll::event::world::ClientLevelTickEvent>(
                [](ll::event::world::ClientLevelTickEvent&) { onClientTick(); }
            );
            SROO_DEBUG("AutoMouse: tick listener installed (26.40)");
        }
        if (!g_autoMouseHooks) {
            g_autoMouseHooks = std::make_unique<AutoMouseHooks>(); // 输入层模拟（挖掘裂纹）
            // 输入层符号（Mouse::_instance / MouseDevice::feed）在这里先解析一次，
            // 解析结果会写进日志；拿不到就走 GameMode 兜底路径。
            resolveMouseInputSymbols();
            SROO_DEBUG("AutoMouse: InputHandler::tick hook installed (26.40)");
        }
    } else {
        ForceStopAutoMouse();
        releaseSyntheticLmbHold();
        bus.removeListener(g_tickListener);
        g_tickListener = nullptr;
        g_autoMouseHooks.reset();
        g_mouseDevice     = nullptr; // 下次启用时重新取（退出世界后设备对象可能重建）
        g_lmbHoldInjected = false;
        g_mouseResolveTries = 0;
    }
}

} // namespace Stipuleroo
