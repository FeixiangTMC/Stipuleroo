# Stipuleroo

[English](README.md) | 中文

托叶工具，Minecraft 基岩版客户端多功能模组。纯客户端工作，可进入任意服务器使用。

## 版本与下载

模组会持续更新，**每个 release 只针对一个具体的游戏版本构建**，适配的游戏版本写在发布资源的文件名里：

```
Stipuleroo-Windows-v<模组版本>-<游戏版本>.zip
         例：Stipuleroo-Windows-v0.0.4-26.40.zip   ← 适用于 Minecraft 26.40
```

- 到 [Releases 页面](https://github.com/FeixiangTMC/Stipuleroo/releases) 下载**游戏版本与你一致**的那个资源；旧版本会一直保留，方便还在老游戏版本上的玩家使用。
- 最新 release 功能最新，但它**不能**用在更老的游戏版本上（反过来，旧 release 也未必能用在更新的游戏版本上）。游戏刚更新时，请等匹配的新 release。
- 每个版本改了什么见 [CHANGELOG.md](CHANGELOG.md)。
- 每个 release 都需要**同一游戏版本**的 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 客户端（例如 Minecraft 26.40 对应 LeviLamina `26.40.*`）。

## 功能

所有功能均支持**命令**和**快捷键**两种开关方式。快捷键默认全部为空，需要在配置文件中自行设置。

### 灵魂出窍 `/fc`

输入 `/fc` 或按快捷键切换灵魂出窍模式：

| 提示 | 含义 |
|---|---|
| `灵魂出窍模式已启用。` | 已开启，自由飞行 |
| `灵魂出窍模式已禁用。` | 已关闭，回到身体 |

- 基于 Debug Camera 实现
- 自由飞行时玩家本体冻结在原地，不会移动/破坏方块
- 死亡、退出世界时自动关闭
- 纯客户端，服务端无感知

### 自动工具 `/at`

输入 `/at` 或按快捷键切换自动工具模式：

| 提示 | 含义 |
|---|---|
| `自动工具切换已启用。` | 已开启 |
| `自动工具切换已禁用。` | 已关闭 |

- 挖掘方块时自动切换到快捷栏中挖掘速度最快的工具
- 攻击实体时自动切换到伤害最高的武器（计算锋利附魔加成）
- 退出世界时自动关闭

### 自动搭路 `/ab`

输入 `/ab` 或按快捷键切换自动搭路：

| 提示 | 含义 |
|---|---|
| `自动搭路已启用。` | 已开启 |
| `自动搭路已禁用。` | 已关闭 |

- 开启后自动在脚下和前方放置方块
- 退出世界时自动关闭

### 夜视 `/rv`

输入 `/rv` 或按快捷键切换夜视：

| 提示 | 含义 |
|---|---|
| `夜视已启用。` | 已开启 |
| `夜视已禁用。` | 已关闭 |

- 永久夜视效果，无需药水
- 退出世界时自动关闭

### 自动鼠标操作 `/am`

`/am` 会弹出模式菜单；三种模式各自也可以绑定快捷键。**三种模式互斥**：开启一个会自动关掉上一个，再选一次当前项即关闭。

| 提示 | 含义 |
|---|---|
| `自动鼠标操作: 连续左键点击（攻击）` | 按间隔攻击准星目标 |
| `自动鼠标操作: 连续右键点击（放置/使用）` | 按间隔放置方块 / 使用物品 |
| `自动鼠标操作: 持续左键长按（挖掘）` | 持续挖掘准星方块 |
| `自动鼠标操作: 已关闭` | 已关闭 |

- **连续左键点击**：攻击准星目标；对着空气时空挥手臂
- **连续右键点击**：按间隔对目标方块放置方块 / 使用物品（放置走服务端权威的 `GameMode::buildBlock`，方块会真正落地）
- **持续左键长按（挖掘）**：在输入层伪造「鼠标左键按住」，让原版挖掘管线原样跑完 —— 破坏进度、裂纹贴图、音效、发包都与真人按住左键完全一致
- 点击间隔可在配置文件中调整（0.05 ~ 1000 秒）
- 打开任何界面（背包 / 聊天 / 表单）时自动暂停，并立刻松开鼠标按键
- 退出世界、死亡时自动关闭

## 配置

模组首次加载后会在 `mods/Stipuleroo/config/config.json` 生成配置文件：

```jsonc
// Stipuleroo 配置（支持 // 注释；游戏内改完约 2 秒自动生效，聊天栏会提示）
// 键名：A~Z 0~9 F1~F12 Space Tab Enter Shift Ctrl Alt（可加 L/R 前缀）；留空 "" = 不绑定
{
    "debugLog": false,                      // 输出诊断日志到 logs/latest.log
    "freecamKey": "",                   // 灵魂出窍      /fc
    "autoToolKey": "",                  // 自动工具      /at
    "fakeSneakKey": "",                 // 伪潜行        /fs（26.32+ 无效果）
    "nightVisionKey": "",               // 夜视          /rv
    "autoBridgeKey": "",                // 自动搭路      /ab
    "autoMouseLeftClickKey": "",       // 自动鼠标      /am：连续左键点击（攻击）
    "autoMouseRightClickKey": "",      // 自动鼠标      /am：连续右键点击（放置/使用）
    "autoMouseHoldLeftKey": "",        // 自动鼠标      /am：持续左键长按（挖掘）
    "autoMouseLeftClickInterval": 0.20,    // 左键点击间隔（秒，0.05~1000）
    "autoMouseRightClickInterval": 0.20    // 右键点击间隔（秒，0.05~1000）
}
```

| 配置项 | 对应功能 | 默认值 |
|---|---|---|
| `debugLog` | 是否把诊断日志写进 `logs/latest.log`（反馈问题时打开） | 正式版 `false` |
| `freecamKey` | 灵魂出窍 `/fc` | 空（无快捷键） |
| `autoToolKey` | 自动工具 `/at` | 空 |
| `fakeSneakKey` | 伪潜行 `/fs` —— 26.32 起游戏移除了挂点，**无实际效果** | 空 |
| `nightVisionKey` | 夜视 `/rv` | 空 |
| `autoBridgeKey` | 自动搭路 `/ab` | 空 |
| `autoMouseLeftClickKey` | 自动鼠标 `/am`：连续左键点击 | 空 |
| `autoMouseRightClickKey` | 自动鼠标 `/am`：连续右键点击 | 空 |
| `autoMouseHoldLeftKey` | 自动鼠标 `/am`：持续左键长按（挖掘） | 空 |
| `autoMouseLeftClickInterval` | 左键点击间隔（秒，0.05 ~ 1000） | `0.20` |
| `autoMouseRightClickInterval` | 右键点击间隔（秒，0.05 ~ 1000） | `0.20` |

支持的键名：`A`~`Z`、`0`~`9`、`F1`~`F12`、`Space`、`Tab`、`Enter`、`Shift`、`Ctrl`、`Alt` 及左右变体（`LShift`/`RShift`、`LCtrl`/`RCtrl`、`LAlt`/`RAlt`）。

配置文件支持 `//` 注释，并且**支持局内热重载**：在游戏里（已进入世界）改完保存，约 2 秒后自动生效，聊天栏会给出提示。间隔超出范围会自动夹紧；文件写坏时忽略本次修改（保留原配置）。

## 安装

1. 安装 [LeviLauncher](https://github.com/LiteLDev/LeviLauncher) 启动器
2. 在 LeviLauncher 中安装与你的游戏版本匹配的 **LeviLamina 客户端**
3. 从 [Releases 页面](https://github.com/FeixiangTMC/Stipuleroo/releases) 下载**游戏版本与你一致**的 zip（见上面「版本与下载」），并导入 LeviLauncher
4. 启动游戏，在聊天栏输入命令以启用功能
5. 初次启动后会生成配置文件 `mods/Stipuleroo/config/config.json`，在里面配置你的快捷键

## 从源码构建

需要 [xmake](https://xmake.io)，以及与目标客户端匹配的 **clang-cl**（LLVM）：从游戏 26.20 起，客户端与 LeviLamina 都由 clang/LLVM 构建，模组也**必须**用 `clang-cl` 编译 —— 用 MSVC 会得到不同的 `entt` 组件类型哈希，导致所有基于 ECS 的功能静默失效。

```powershell
.\build.ps1                            # release 构建（自动把 LLVM 的 bin 加进 PATH，然后配置 + 编译）
.\build.ps1 -Mode debug -Clean         # debug 构建（先清理）
.\deploy.ps1 -Version 1.26.40.05       # 把产物拷进 LeviLauncher 的对应版本
```

模组当前适配的 LeviLamina 版本写在 `xmake.lua` 与 `tooth.json` 里（换游戏版本时两处一起改）。产物：`bin/Stipuleroo/{manifest.json, Stipuleroo.dll}`。

## 许可证

GPL-3.0 © FeixiangTMC
