# Runs an NRO in Eden and takes screenshots of its window (the picture gate of the native renderer).
#   powershell -File switch/eden-shot.ps1 -Nro <path.nro> -Out <dir> [-Seconds 30] [-Every 10]
# Starts eden.exe -g <nro>, captures the Eden window every -Every seconds until -Seconds, writes
# <dir>\shot-NN.png, then closes Eden politely (CloseMainWindow, force after 15 s). Audio stays muted
# in Eden's config. Never start a second Eden while one runs (CLAUDE.md "Eden before the console").
param([Parameter(Mandatory = $true)][string]$Nro, [Parameter(Mandatory = $true)][string]$Out, [int]$Seconds = 30, [int]$Every = 10)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public struct RECT { public int Left, Top, Right, Bottom; }
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
'@
$eden = 'E:\Switch\Eden\eden.exe'
if (Get-Process eden -ErrorAction SilentlyContinue | Where-Object Path -eq $eden) { throw "Eden ($eden) is already running: not starting a second one" }
New-Item -ItemType Directory -Force $Out | Out-Null
$p = Start-Process -FilePath $eden -ArgumentList '-g', $Nro -PassThru
$n = 0
for ($t = $Every; $t -le $Seconds; $t += $Every) {
    Start-Sleep -Seconds $Every
    if ($p.HasExited) { Write-Output "Eden exited at ${t}s"; break }
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { Write-Output "${t}s: no main window yet"; continue }
    $null = [Win32]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 300
    $r = New-Object RECT
    $null = [Win32]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    if ($w -le 0 -or $hh -le 0) { Write-Output "${t}s: window has no size"; continue }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $file = Join-Path $Out ("shot-{0:D2}.png" -f $n); $n++
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output ("{0}s: {1} ({2}x{3}, title '{4}')" -f $t, $file, $w, $hh, $p.MainWindowTitle)
}
if (-not $p.HasExited) {
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit(15000)) { Write-Output 'Eden did not close in 15 s: killing it'; $p.Kill() }
}
Write-Output "done: $n screenshots in $Out"
