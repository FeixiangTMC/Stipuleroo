# ============================================================
#  Stipuleroo 部署脚本：把 build 产物装进游戏 mods 目录
#
#  用法（在项目根目录执行）：
#      .\deploy.ps1                  # release 构建 → 26.40 客户端
#      .\deploy.ps1 -Version 1.26.10.04
#
#  注意：游戏运行时 Stipuleroo.dll 被占用，脚本会检测并给出提示；
#        请先退出游戏再执行。
# ============================================================
param(
    [ValidateSet("release", "debug")] [string]$Mode = "release",
    [string]$Version = "1.26.40.05"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src  = Join-Path $root "bin\Stipuleroo"
$dst  = Join-Path $env:APPDATA "levilauncher.exe\versions\$Version\mods\Stipuleroo"

if (-not (Test-Path (Join-Path $src "Stipuleroo.dll"))) {
    throw "找不到产物 $src\Stipuleroo.dll，请先运行 .\build.ps1"
}

$running = Get-Process -Name "Minecraft.Windows" -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "[deploy] 游戏正在运行（PID: $($running.Id -join ', ')），Stipuleroo.dll 被占用。" -ForegroundColor Yellow
    Write-Host "[deploy] 已把新构建暂存为 Stipuleroo.dll.new；退出游戏后重新执行本脚本即可生效。"
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item (Join-Path $src "Stipuleroo.dll") (Join-Path $dst "Stipuleroo.dll.new") -Force
    Copy-Item (Join-Path $src "manifest.json")  (Join-Path $dst "manifest.json")      -Force
    exit 2
}

New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item (Join-Path $src "Stipuleroo.dll")  (Join-Path $dst "Stipuleroo.dll")  -Force
Copy-Item (Join-Path $src "manifest.json")   (Join-Path $dst "manifest.json")   -Force
Copy-Item (Join-Path $src "Stipuleroo.pdb")  (Join-Path $dst "Stipuleroo.pdb")  -Force -ErrorAction SilentlyContinue
$stale = Join-Path $dst "Stipuleroo.dll.new"
if (Test-Path $stale) { Remove-Item $stale -Force }

$h1 = (Get-FileHash (Join-Path $src "Stipuleroo.dll")).Hash
$h2 = (Get-FileHash (Join-Path $dst "Stipuleroo.dll")).Hash
Write-Host "[deploy] 已部署到 $dst"
Write-Host ("[deploy] SHA256 一致 = {0}" -f ($h1 -eq $h2))
