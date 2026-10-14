<#
.SYNOPSIS
  Builds the plugin and copies it into a portable Notepad++ under .npp-dev
  (see setup-dev.ps1).

.EXAMPLE
  scripts\deploy-dev.ps1                         # Debug, ARM64
  scripts\deploy-dev.ps1 -Platform x64 -Configuration Release
  scripts\deploy-dev.ps1 -Launch -File tests\manual\sample.md
#>
param(
    [ValidateSet('ARM64', 'x64', 'Win32')] [string] $Platform = 'ARM64',
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $Launch,
    [string] $File
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$arch = @{ ARM64 = 'arm64'; x64 = 'x64'; Win32 = 'x86' }[$Platform]
$npp = Join-Path $root ".npp-dev\$arch"
if (-not (Test-Path (Join-Path $npp 'notepad++.exe'))) { throw "No portable Notepad++ in $npp. Run scripts\setup-dev.ps1 -Arch $arch." }

# Native build
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -and (Test-Path (Join-Path $vs 'MSBuild\Current\Bin\arm64\MSBuild.exe'))) {
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\arm64\MSBuild.exe'
}
& $msbuild (Join-Path $root 'MarkdownReader.sln') -t:restore -p:RestorePackagesConfig=true -nologo -v:quiet
& $msbuild (Join-Path $root 'vs.proj\MarkdownReader.vcxproj') "-p:Configuration=$Configuration" "-p:Platform=$Platform" -nologo -v:minimal -m
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }

# Preview page (production build: WebView2 origins and strict CSP)
$webOut = Join-Path $root 'build\web'
Push-Location (Join-Path $root 'web')
try {
    if (-not (Test-Path node_modules)) { npm ci --no-fund --no-audit }
    node build.mjs --out $webOut
    if ($LASTEXITCODE -ne 0) { throw 'Web build failed.' }
} finally { Pop-Location }

# Notepad++ must not be running while its plugin DLL is replaced.
$running = Get-Process notepad++ -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$npp*" }
if ($running) { throw "The portable Notepad++ in $npp is running; close it first." }

$pluginDir = Join-Path $npp 'plugins\MarkdownReader'
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item (Join-Path $root "build\$Configuration\$Platform\MarkdownReader.dll") $pluginDir -Force
$pdb = Join-Path $root "build\$Configuration\$Platform\MarkdownReader.pdb"
if (Test-Path $pdb) { Copy-Item $pdb $pluginDir -Force }
if (Test-Path (Join-Path $pluginDir 'web')) { Remove-Item -Recurse -Force (Join-Path $pluginDir 'web') }
Copy-Item -Recurse $webOut (Join-Path $pluginDir 'web')
Write-Host "Deployed $Configuration|$Platform to $pluginDir"

if ($Launch) {
    $arguments = @('-multiInst', '-nosession')
    if ($File) { $arguments += '"' + (Resolve-Path $File) + '"' }
    Start-Process (Join-Path $npp 'notepad++.exe') -ArgumentList $arguments
}
