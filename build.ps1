# ============================================================
#  Stipuleroo 构建脚本（26.40 / client）
#
#  26.40 客户端由 clang/LLVM 构建，模组必须使用 clang-cl 编译，
#  否则 entt 组件类型哈希与游戏不一致，ECS 相关功能（灵魂出窍等）会静默失效。
#  详见 xmake.lua 顶部注释。
#
#  用法（在项目根目录执行）：
#      .\build.ps1                 # release
#      .\build.ps1 -Mode debug     # debug（默认输出诊断日志）
#      .\build.ps1 -Clean          # 先清理再配置构建
#
#  clang-cl 查找顺序：
#      1) PATH 里已有的 clang-cl
#      2) 环境变量 LLVM_HOME / LLVM_ROOT 下的 bin
#      3) 常见安装位置（%LOCALAPPDATA%\Programs\LLVM*\bin、Program Files\LLVM\bin 等）
# ============================================================
param(
    [ValidateSet("release", "debug")] [string]$Mode = "release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# 1. 找到 clang-cl 并把它所在目录加进 PATH
function Find-ClangClDir {
    $onPath = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
    if ($onPath) { return (Split-Path -Parent $onPath.Source) }

    $candidates = @()
    foreach ($envName in @("LLVM_HOME", "LLVM_ROOT")) {
        $root = [Environment]::GetEnvironmentVariable($envName)
        if ($root) { $candidates += (Join-Path $root "bin") }
    }
    $candidates += @(
        (Join-Path $env:LOCALAPPDATA "Programs\LLVM\bin"),
        (Join-Path $env:ProgramFiles "LLVM\bin"),
        (Join-Path ${env:ProgramFiles(x86)} "LLVM\bin")
    )
    if ($env:LOCALAPPDATA -and (Test-Path (Join-Path $env:LOCALAPPDATA "Programs"))) {
        $candidates += (Get-ChildItem (Join-Path $env:LOCALAPPDATA "Programs") -Directory -Filter "LLVM*" -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "bin" })
    }
    foreach ($dir in $candidates) {
        if ($dir -and (Test-Path (Join-Path $dir "clang-cl.exe"))) { return $dir }
    }
    return $null
}

$llvmBin = Find-ClangClDir
if (-not $llvmBin) {
    throw "未找到 clang-cl.exe。请安装 LLVM（https://github.com/llvm/llvm-project/releases），或设置环境变量 LLVM_HOME 指向 LLVM 安装目录，或把它加进 PATH。"
}
if (-not ($env:PATH -split ';' | Where-Object { $_ -eq $llvmBin })) {
    $env:PATH = "$llvmBin;$env:PATH"
}
Write-Host "[build] clang-cl = $((Get-Command clang-cl).Source)"

# 2. 配置 + 构建
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    if ($Clean) { xmake f -c -y }
    xmake f -y -p windows -a x64 -m $Mode --target_type=client
    if ($LASTEXITCODE -ne 0) { throw "xmake configure failed" }
    xmake -y -v
    if ($LASTEXITCODE -ne 0) { throw "xmake build failed" }
    Write-Host "[build] 产物: build\windows\x64\$Mode\Stipuleroo.dll"
} finally {
    Pop-Location
}
