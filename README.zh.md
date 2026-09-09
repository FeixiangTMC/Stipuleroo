# Stipuleroo

[English](README.md) | 中文

托叶工具，Minecraft 基岩版客户端多功能模组。纯客户端工作，可进入任意服务器使用。

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

## 配置

模组首次加载后会在 `config/config.json` 生成配置文件：

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

| 配置项 | 对应功能 | 默认值 |
|---|---|---|
| `freecamKey` | 灵魂出窍 `/fc` | 空（无快捷键） |
| `autoToolKey` | 自动工具 `/at` | 空 |
| `fakeSneakKey` | 伪潜行 `/fs` | 空 |
| `nightVisionKey` | 夜视 `/rv` | 空 |
| `autoBridgeKey` | 自动搭路 `/ab` | 空 |

支持的键名：`A`~`Z`、`0`~`9`、`F1`~`F12`、`Space`、`Tab`、`Enter`、`Shift`、`Ctrl`、`Alt` 及左右变体（`LShift`/`RShift` 等）。

## 安装

1. 安装 [LeviLauncher](https://github.com/LiteLDev/LeviLauncher) 启动器
2. 在 LeviLauncher 中安装 LeviLamina 客户端
3. 将 `Stipuleroo-windows.zip` 导入 LeviLauncher 启动器
4. 启动游戏，在聊天栏输入命令以启用功能
5. 初次启动后会生成配置文件mod/Stipuleroo/config/config.json，在里面配置你的快捷键

## 许可证

GPL-3.0 © FeixiangTMC
