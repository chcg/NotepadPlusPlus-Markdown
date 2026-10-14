<#
.SYNOPSIS
  Builds Release binaries and creates the release zips in build\dist:

    MarkdownReader-<arch>.zip
      MarkdownReader.dll        <- at the zip root, as Plugin Admin requires
      web\                      <- the preview page
      LICENSE
      THIRD_PARTY_NOTICES.txt

  Asset names carry no version so README links to /releases/latest/download/ keep working.

.EXAMPLE
  scripts\package.ps1                         # arm64 and x64
  scripts\package.ps1 -Platform ARM64,x64,Win32
#>
param(
    [ValidateSet('ARM64', 'x64', 'Win32')] [string[]] $Platform = @('ARM64', 'x64')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\dist'  # not build\release: that is MSBuild's build\Release (paths are case-insensitive)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -and (Test-Path (Join-Path $vs 'MSBuild\Current\Bin\arm64\MSBuild.exe'))) {
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\arm64\MSBuild.exe'
}

# Preview page
$web = Join-Path $root 'build\web'
Push-Location (Join-Path $root 'web')
try {
    if (-not (Test-Path node_modules)) { npm ci --no-fund --no-audit }
    npx vitest run
    if ($LASTEXITCODE -ne 0) { throw 'Web tests failed.' }
    node build.mjs --out $web
    if ($LASTEXITCODE -ne 0) { throw 'Web build failed.' }
} finally { Pop-Location }

# Notices: the web bundle's plus the native libraries compiled into the DLL.
$packages = Join-Path $root 'packages'
$nativeNotices = @(
    @{ Name = 'Microsoft Edge WebView2 SDK (static loader)'; Files = @(Get-ChildItem "$packages\Microsoft.Web.WebView2.*\LICENSE.txt", "$packages\Microsoft.Web.WebView2.*\NOTICE.txt") },
    @{ Name = 'Windows Implementation Library (WIL)'; Files = @(Get-ChildItem "$packages\Microsoft.Windows.ImplementationLibrary.*\LICENSE") },
    @{ Name = 'nlohmann/json'; Files = @(Get-Item (Join-Path $root 'third_party\nlohmann\LICENSE.MIT')) }
)
$rule = '=' * 78
$notices = "MarkdownReader is licensed under the GNU General Public License v3.0 or later (see LICENSE).`r`n"
$notices += "It includes Notepad++ plugin interface files (GPL-3.0-or-later) and the third-party software below.`r`n`r`n"
foreach ($n in $nativeNotices) {
    $notices += "$rule`r`n$($n.Name)`r`n$rule`r`n`r`n"
    foreach ($f in $n.Files) { $notices += (Get-Content -Raw $f.FullName).Trim() + "`r`n`r`n" }
}
$notices += Get-Content -Raw (Join-Path $web 'THIRD_PARTY_NOTICES.txt')

# Refuse to ship anything that mentions this machine: the build tree or the
# user profile path, in ASCII or UTF-16, in any file (binaries included).
$latin1 = [Text.Encoding]::GetEncoding(28591)
$needles = @($root, $env:USERPROFILE) | Where-Object { $_ } | ForEach-Object { $_; $_ -replace '\\', '/' } | Select-Object -Unique
function Assert-NoMachineData([string] $folder) {
    foreach ($file in Get-ChildItem -Recurse -File $folder) {
        $text = $latin1.GetString([IO.File]::ReadAllBytes($file.FullName))
        foreach ($needle in $needles) {
            foreach ($form in @($needle, (($needle.ToCharArray() | ForEach-Object { "$_`0" }) -join ''))) {
                if ($text.IndexOf($form, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    throw "$($file.FullName) contains a local path ($needle). Not packaging."
                }
            }
        }
    }
}

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $msbuild (Join-Path $root 'MarkdownReader.sln') -t:restore -p:RestorePackagesConfig=true -nologo -v:quiet
$hashes = @()
foreach ($p in $Platform) {
    & $msbuild (Join-Path $root 'MarkdownReader.sln') -p:Configuration=Release "-p:Platform=$(if ($p -eq 'Win32') { 'x86' } else { $p })" -nologo -v:minimal -m
    if ($LASTEXITCODE -ne 0) { throw "Native build failed for $p." }

    $arch = @{ ARM64 = 'arm64'; x64 = 'x64'; Win32 = 'x86' }[$p]
    $stage = Join-Path $out "stage-$arch"
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item (Join-Path $root "build\Release\$p\MarkdownReader.dll") $stage
    Copy-Item -Recurse $web (Join-Path $stage 'web')
    Remove-Item (Join-Path $stage 'web\THIRD_PARTY_NOTICES.txt')
    Copy-Item (Join-Path $root 'LICENSE') $stage
    Set-Content -Path (Join-Path $stage 'THIRD_PARTY_NOTICES.txt') -Value $notices -Encoding utf8
    Assert-NoMachineData $stage

    $zip = Join-Path $out "MarkdownReader-$arch.zip"
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    Remove-Item -Recurse -Force $stage
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    $hashes += "$hash  $(Split-Path -Leaf $zip)"
}

$hashes | Set-Content -Path (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
$hashes
