<#
.SYNOPSIS
  Installs the locally built plugin into an installed Notepad++
  (default C:\Program Files\Notepad++). Run from an elevated PowerShell with
  Notepad++ closed. Picks the build matching Notepad++'s architecture.

.EXAMPLE
  scripts\install-local.ps1
#>
param(
    [string] $NotepadPlusPlus = 'C:\Program Files\Notepad++',
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $NotepadPlusPlus 'notepad++.exe'
if (-not (Test-Path $exe)) { throw "Notepad++ not found at $NotepadPlusPlus" }
if (Get-Process notepad++ -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
    throw 'Notepad++ is running; close it first.'
}

# The plugin DLL must match Notepad++'s own architecture.
$bytes = [IO.File]::ReadAllBytes($exe)
$machine = [BitConverter]::ToUInt16($bytes, [BitConverter]::ToInt32($bytes, 60) + 4)
$platform = @{ 0xAA64 = 'ARM64'; 0x8664 = 'x64'; 0x14C = 'Win32' }[[int]$machine]
if (-not $platform) { throw ('Unknown Notepad++ architecture 0x{0:X}' -f $machine) }

$dll = Join-Path $root "build\$Configuration\$platform\MarkdownReader.dll"
$web = Join-Path $root 'build\web'
if (-not (Test-Path $dll)) { throw "Missing $dll - build $Configuration|$platform first." }
if (-not (Test-Path (Join-Path $web 'index.html'))) { throw "Missing $web - run: node web\build.mjs --out build\web" }

$target = Join-Path $NotepadPlusPlus 'plugins\MarkdownReader'
New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item $dll $target -Force
if (Test-Path (Join-Path $target 'web')) { Remove-Item -Recurse -Force (Join-Path $target 'web') }
Copy-Item -Recurse $web (Join-Path $target 'web')
Write-Host "Installed $Configuration|$platform MarkdownReader into $target"
