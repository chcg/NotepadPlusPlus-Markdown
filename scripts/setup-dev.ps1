<#
.SYNOPSIS
  Downloads portable Notepad++ builds into .npp-dev\<arch> for testing the plugin
  without touching the installed Notepad++ (no admin rights needed).

.EXAMPLE
  scripts\setup-dev.ps1                 # arm64 and x64 (x64 runs emulated on ARM64 Windows)
  scripts\setup-dev.ps1 -Arch arm64
#>
param(
    [string[]] $Arch = @('arm64', 'x64'),
    [string] $Version = '8.9.8.1'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$devRoot = Join-Path $root '.npp-dev'

foreach ($a in $Arch) {
    $target = Join-Path $devRoot $a
    if (Test-Path (Join-Path $target 'notepad++.exe')) {
        Write-Host "${a}: already present in $target"
        continue
    }

    $asset = "npp.$Version.portable.$a.zip"
    $url = "https://github.com/notepad-plus-plus/notepad-plus-plus/releases/download/v$Version/$asset"
    $zip = Join-Path ([IO.Path]::GetTempPath()) $asset
    Write-Host "${a}: downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Expand-Archive -Path $zip -DestinationPath $target -Force
    Remove-Item $zip

    $exe = Join-Path $target 'notepad++.exe'
    $signature = Get-AuthenticodeSignature $exe
    if ($signature.Status -ne 'Valid') {
        Remove-Item -Recurse -Force $target
        throw "${a}: notepad++.exe signature is $($signature.Status); removed the download."
    }
    Write-Host "${a}: ready ($($signature.SignerCertificate.Subject))"
}
