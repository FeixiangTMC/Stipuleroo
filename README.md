# Stipuleroo

English | [中文](README.zh.md)

**Stipuleroo** is a multi-purpose **client-side** mod for Minecraft Bedrock Edition. Everything runs purely on the client, so it can be used on any server.

## Versions & downloads

Stipuleroo keeps being updated, and **each release is built for one specific game version**. The supported game version is written into the release file name:

```
Stipuleroo-Windows-v<mod version>-<game version>.zip
         e.g.  Stipuleroo-Windows-v0.0.4-26.40.zip   ← for Minecraft 26.40
```

- Grab the asset from the [Releases page](https://github.com/FeixiangTMC/Stipuleroo/releases) whose **game version matches yours** — old releases are kept, so people on older game versions can still download them.
- The newest release contains the newest features, but it will **not** work on older game versions (and an older release may not work on a newer game). If your game just updated, wait for a release matching it.
- What changed in each release: [CHANGELOG.md](CHANGELOG.md).
- Each release needs a matching **[LeviLamina](https://github.com/LiteLDev/LeviLamina) client** for that same game version (e.g. LeviLamina `26.40.*` for Minecraft 26.40).

## Features

All features can be toggled both by **commands** and by **hotkeys**. Hotkeys are all unbound by default — configure them in the config file.

> In-game chat feedback is currently in Chinese. The tables below show the messages you will see and what they mean.

### Free Camera (Out-of-Body) `/fc`

Toggle free camera mode by typing `/fc` or pressing your hotkey:

| Message | Meaning |
|---|---|
| `灵魂出窍模式已启用。` | Free camera enabled — free flight |
| `灵魂出窍模式已禁用。` | Free camera disabled — back in your body |

- Implemented on top of the game's Debug Camera
- While free-flying, your body stays frozen in place — it will not move or break blocks
- Automatically disabled on death or when leaving the world
- Purely client-side — the server has no way of knowing

### Auto Tool `/at`

Toggle auto tool by typing `/at` or pressing your hotkey:

| Message | Meaning |
|---|---|
| `自动工具切换已启用。` | Auto tool switching enabled |
| `自动工具切换已禁用。` | Auto tool switching disabled |

- Automatically switches to the fastest tool in your hotbar when mining blocks
- Automatically switches to the highest-damage weapon when attacking entities (accounts for the Sharpness enchantment)
- Automatically disabled when leaving the world

### Auto Bridge `/ab`

Toggle auto bridge by typing `/ab` or pressing your hotkey:

| Message | Meaning |
|---|---|
| `自动搭路已启用。` | Auto bridge enabled |
| `自动搭路已禁用。` | Auto bridge disabled |

- When enabled, automatically places blocks beneath and in front of you
- Automatically disabled when leaving the world

### Night Vision `/rv`

Toggle night vision by typing `/rv` or pressing your hotkey:

| Message | Meaning |
|---|---|
| `夜视已启用。` | Night vision enabled |
| `夜视已禁用。` | Night vision disabled |

- Permanent night vision effect — no potions needed
- Automatically disabled when leaving the world

### Auto Mouse `/am`

`/am` opens a menu with all three modes; each mode also has its own hotkey. **The three modes are mutually exclusive** — turning one on turns the previous one off, and picking the current one again turns it off.

| Message | Meaning |
|---|---|
| `自动鼠标操作: 连续左键点击（攻击）` | Repeating left click (attack) |
| `自动鼠标操作: 连续右键点击（放置/使用）` | Repeating right click (place/use) |
| `自动鼠标操作: 持续左键长按（挖掘）` | Holding left click (mining) |
| `自动鼠标操作: 已关闭` | Off |

- **Repeating left click** — attacks whatever is under the crosshair, and swings the arm when aiming at nothing
- **Repeating right click** — places blocks / uses the held item at the configured interval (placing goes through the server-authoritative `GameMode::buildBlock`, so blocks actually persist)
- **Holding left click (mining)** — fakes a real "left mouse button held" event at the input layer, so the vanilla mining pipeline runs unchanged: breaking progress, the crack overlay, sounds and packets all behave exactly like a human holding the button
- Click intervals are configurable (`0.05`–`1000` seconds in the config file)
- Pauses automatically while any UI (inventory / chat / form) is open, and releases the mouse button immediately
- Automatically disabled when leaving the world or on death

## Configuration

On first load the mod generates `mods/Stipuleroo/config/config.json`:

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

| Setting | Feature | Default |
|---|---|---|
| `debugLog` | Write diagnostic logs to `logs/latest.log` (useful when reporting issues) | `false` in release builds |
| `freecamKey` | Free Camera `/fc` | empty (no hotkey) |
| `autoToolKey` | Auto Tool `/at` | empty |
| `fakeSneakKey` | Fake Sneak `/fs` — no effect since 26.32 (the game removed the hook target) | empty |
| `nightVisionKey` | Night Vision `/rv` | empty |
| `autoBridgeKey` | Auto Bridge `/ab` | empty |
| `autoMouseLeftClickKey` | Auto Mouse `/am` — repeating left click | empty |
| `autoMouseRightClickKey` | Auto Mouse `/am` — repeating right click | empty |
| `autoMouseHoldLeftKey` | Auto Mouse `/am` — holding left click (mining) | empty |
| `autoMouseLeftClickInterval` | Interval of repeating left click, in seconds (`0.05`–`1000`) | `0.20` |
| `autoMouseRightClickInterval` | Interval of repeating right click, in seconds (`0.05`–`1000`) | `0.20` |

Supported key names: `A`–`Z`, `0`–`9`, `F1`–`F12`, `Space`, `Tab`, `Enter`, `Shift`, `Ctrl`, `Alt` and their left/right variants (`LShift`/`RShift`, `LCtrl`/`RCtrl`, `LAlt`/`RAlt`).

The file supports `//` comments and is **hot-reloaded**: save it while the game is running (in a world) and the changes apply in about 2 seconds — a chat message confirms it. Out-of-range intervals are clamped, and a malformed file is ignored (the previous config stays active).

## Installation

1. Install the [LeviLauncher](https://github.com/LiteLDev/LeviLauncher) launcher
2. Install a **LeviLamina client** matching your game version through LeviLauncher
3. Download the release zip whose game version matches yours (see [Versions & downloads](#versions--downloads)) from the [Releases page](https://github.com/FeixiangTMC/Stipuleroo/releases) and import it into LeviLauncher
4. Launch the game and type the commands in chat to enable features
5. On first launch, a config file is generated at `mods/Stipuleroo/config/config.json` — configure your hotkeys there

## Building from source

Requirements: [xmake](https://xmake.io), and a **clang-cl** (LLVM) matching the client you build against — from game version 26.20 on, the client and LeviLamina are built with clang/LLVM, so the mod must be compiled with `clang-cl` too. Building with MSVC produces a different `entt` component type hash and silently breaks every ECS-based feature.

```powershell
.\build.ps1                            # release build (puts LLVM's bin on PATH, then configures + builds)
.\build.ps1 -Mode debug -Clean         # debug build from scratch
.\deploy.ps1 -Version 1.26.40.05       # copy the build output into a LeviLauncher version
```

`xmake.lua` and `tooth.json` declare the LeviLamina version the mod is built against (keep them in sync when targeting a new game version). Output: `bin/Stipuleroo/{manifest.json, Stipuleroo.dll}`.

## License

GPL-3.0 © FeixiangTMC
