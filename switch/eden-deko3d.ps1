# Runs the native-renderer build in Eden with screenshots (the picture gate, CLAUDE.md "Eden before
# the console"): stages out/build/switch/stage/ (NRO, shaders, the deko3d marker) into Eden's SD,
# runs eden-shot.ps1, backs the logs up and prints the backend's lines.
#   powershell -File switch/eden-deko3d.ps1 [-Seconds 90] [-Every 10]
param([int]$Seconds = 90, [int]$Every = 10)
$ErrorActionPreference = 'Stop'
$dir = 'E:\Switch\Eden\user\sdmc\switch\cvhd'
$stage = '/root/cvhd/src/out/build/switch/stage'
$sd = '/mnt/e/Switch/Eden/user/sdmc/switch/cvhd'
$version = wsl -u root -e bash -lc "set -e; rm -rf $sd/shaders; cp -r $stage/shaders $sd/shaders; cp $stage/cvhd.nro $sd/cvhd.nro; touch $sd/deko3d; mkdir -p $sd/shaders/missing; grep -o '0[.][0-9]*[.][0-9a-f+]*' /root/cvhd/src/out/build/switch/cvhd_version.h | head -1"
$version = ($version | Select-Object -Last 1).Trim()
if (-not $version) { throw 'staging failed (no version)' }
Remove-Item "$dir\cvhd_log.txt", "$dir\cvhd_rex_log.txt", 'E:\Switch\Eden\user\log\eden_log.txt' -ErrorAction SilentlyContinue
$out = "E:\conker-data\eden-runs\cvhd-$version-deko3d"
Write-Output "Eden deko3d: cvhd $version for $Seconds s, screenshots every $Every s -> $out"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\eden-shot.ps1" -Nro "$dir\cvhd.nro" -Out $out -Seconds $Seconds -Every $Every
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($f in @("$dir\cvhd_log.txt", "$dir\cvhd_rex_log.txt", 'E:\Switch\Eden\user\log\eden_log.txt')) {
    if (Test-Path $f) { Copy-Item $f $out -Force }
}
if (Test-Path "$dir\cvhd_log.txt") {
    Write-Output '--- backend lines (last 40)'
    Select-String -Path "$dir\cvhd_log.txt" -Pattern '\[dk\]|gpu: swap|crash|exception' | Select-Object -Last 40 | ForEach-Object { $_.Line }
    $missing = Get-ChildItem "$dir\shaders\missing" -ErrorAction SilentlyContinue
    if ($missing) { Write-Output ("missing shaders dumped: " + ($missing.Name -join ' ')) }
}
Write-Output "logs and screenshots: $out"
