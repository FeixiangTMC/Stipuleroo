# Stipuleroo

English | [中文](README.zh.md)

**Stipuleroo** is a multi-purpose **client-side** mod for Minecraft Bedrock Edition. Everything runs purely on the client, so it can be used on any server.

## Features

All features can be toggled both by **commands** and by **hotkeys**. Hotkeys are all unbound by default — configure them in the config file.

> In-game chat feedback is currently in Chinese. The tables below show the messages you will see and what they mean.

### Free Camera (Out-of-Body) `/fc`

Toggle free camera mode by typing `/fc` or pressing your hotkey:

| Message | Meaning |
|---|---|
| `灵魂出窍模式已启用。` | Free camera enabled — free flight |
| `灵魂出窍模式已禁用。` | Free camera disabled — back in your body |

- Implemented on top of the Debug Camera
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

## Configuration

On first load, the mod generates its config file at `config/config.json`:

```json
{
    "freecamKey": "",
    "autoToolKey": "",
    "fakeSneakKey": "",
    "nightVisionKey": "",
    "autoBridgeKey": "",
    "_comment": "键名支持: A~Z, 0~9, F1~F12, Space, Tab, Enter, Shift, Ctrl, Alt, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt\n默认全部为空（无快捷键），需要快捷键时自行填入键名即可"
}
```

| Setting | Feature | Default |
|---|---|---|
| `freecamKey` | Free Camera `/fc` | empty (no hotkey) |
| `autoToolKey` | Auto Tool `/at` | empty |
| `fakeSneakKey` | Fake Sneak `/fs` | empty |
| `nightVisionKey` | Night Vision `/rv` | empty |
| `autoBridgeKey` | Auto Bridge `/ab` | empty |

Supported key names: `A`–`Z`, `0`–`9`, `F1`–`F12`, `Space`, `Tab`, `Enter`, `Shift`, `Ctrl`, `Alt` and their left/right variants (`LShift`/`RShift`, `LCtrl`/`RCtrl`, `LAlt`/`RAlt`).

## Installation

1. Install the [LeviLauncher](https://github.com/LiteLDev/LeviLauncher) launcher
2. Install a LeviLamina client through LeviLauncher
3. Download the mod zip (e.g. `Stipuleroo-Windows-v0.0.3-26.10.zip`) from the [Releases page](https://github.com/FeixiangTMC/Stipuleroo/releases) and import it into LeviLauncher
4. Launch the game and type the commands in chat to enable features
5. On first launch, a config file is generated at `mod/Stipuleroo/config/config.json` — configure your hotkeys there

## License

GPL-3.0 © FeixiangTMC
