# Changelog

All notable changes to Stipuleroo will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
