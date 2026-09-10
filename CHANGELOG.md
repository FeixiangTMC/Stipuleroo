# Changelog

All notable changes to Stipuleroo will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.4] - 2026-09-11

适配 **LeviLamina 26.40（Minecraft 基岩版 26.40.5 / 26.40.8）**，并新增「自动鼠标操作」。

### Added
- **自动鼠标操作 `/am`**（三种模式互斥，可用 `/am` 表单或各自快捷键切换）：
  - 连续左键点击（攻击）：按配置间隔攻击准星目标，无目标时空挥手臂；
  - 连续右键点击（放置/使用）：按间隔放置方块 / 使用物品（放置走服务端权威的 `GameMode::buildBlock`）；
  - 持续左键长按（挖掘）：在输入层伪造「鼠标左键按住」，由原版跑完挖掘链路（破坏进度、裂纹贴图、
    音效、发包全部与原版一致）；
  - 间隔可在 `config.json` 配置（0.05 ~ 1000 秒）；打开任何界面时自动暂停并立刻松开鼠标。
- 构建 / 部署脚本 `build.ps1`、`deploy.ps1`。
- 统一的模组日志入口 `Log.h`：诊断日志（`debugLog` 控制，正式版默认关闭）、警告、错误三级。

### Changed
- **编译工具链改为 clang-cl**：26.40 客户端与 LeviLamina 均由 clang/LLVM 构建，用 MSVC 编译会导致
  entt 组件类型哈希不一致，ECS 相关功能（灵魂出窍等）静默失效。
- 灵魂出窍回归「游戏原生 Debug Camera」方案（搬运相机 ECS 组件标记，渲染与飞行交给游戏自身系统）。
- 夜视重写挂点：简约/花式图形走 `createBaseLightTextureData`，灵动视效（Deferred）走 FrameBuilder 注入。
- 配置文件改为带 `//` 注释的 JSONC，并支持**局内热重载**（改完约 2 秒自动生效，聊天栏有提示）；
  注释精简为「文件头 2 行 + 每字段行尾一句」，移除 `configVersion` 字段与自动重写逻辑。
- 客户端命令注册改用 LeviLamina 的 `ClientCommandRegisterEvent`（26.32+ 客户端命令必须在此注册）。
- 退出世界 / 死亡 / 禁用模组时，同步关闭全部功能并还原相机状态。

### Fixed
- 修复「持续左键长按（挖掘）」时客户端破坏裂纹一直闪在最低级的问题：客户端裂纹由渲染侧
  `LevelRendererPlayer::mDestroyingBlockList` 驱动，只吃原版输入管线，因此改为在输入层伪造鼠标左键事件。
- 修复重进世界崩溃（客户端命令注册复用了上一代命令注册表的悬空 `CommandHandle`）。
- 修复右键放置只产生「客户端幽灵方块」的问题（改用 `GameMode::buildBlock` 这一服务端权威入口）。
- 自动工具适配 26.40 的 `ItemStackBase::mItem` / `getDestroySpeed` 接口变更。

### Removed
- 「持续右键长按」模式（实测服务端不接受模拟的长按放置，与「连续右键点击」等价）。
- 伪潜行 `/fs` 的挂点：26.32+ 游戏已移除对应系统，命令与开关保留但无实际效果。

## [0.0.3] - 2026-08-10

### Added
- 完整快捷键系统：全部功能均可用快捷键开关，键名写在 `config/config.json` 里（支持 A~Z、0~9、
  F1~F12、Space、Tab、Enter、Shift/Ctrl/Alt 及左右变体）。
- 灵魂出窍改用游戏 Debug Camera 实现（身体冻结在原地，视角自由飞行）。
- 夜视双实现：简约/花式图形与灵动视效（Deferred）各一套挂点。

### Changed
- 自动工具：攻击实体时也会切换到伤害最高的武器（计算锋利附魔加成）。
- 打开任何界面（背包/聊天/表单）时自动暂停相关功能，避免误操作。

## [0.0.2] - 2026-07-04

### Added
- 灵魂出窍快捷键（默认 `C` 键，可配置），与 `/fc` 命令平级切换
- 自动搭路 `/ab` + 快捷键（默认 `R` 键），按配置键开关
- 配置文件 `config/config.json`，支持自定义快捷键键名
- 伪潜行 `/fs`、夜视 `/rv`（隐藏命令，内部使用）

### Changed
- 自动搭路交互改为 `/ab` 进入准备状态 + 快捷键开关

## [0.0.1] - 2026-07-01

### Added
- 灵魂出窍 `/fc` — 旁观模式自由飞行，客户端拦截发包
- 自动工具 `/at` — 挖掘方块时自动切换最优工具
