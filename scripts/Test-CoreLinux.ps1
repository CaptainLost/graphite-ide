#Requires -Version 5.1
<#
.SYNOPSIS
    Runs CoreLinuxTests, the Linux build of Core's doctest cases, inside WSL.

.DESCRIPTION
    The binary is built by `vendor\bin\zig\zig.exe build -Dconfiguration=<Configuration>` (or by
    MSBuild through the CoreLinux project). Building needs no WSL; running does. Without WSL, or
    with no distro installed, this prints "Linux tests skipped: WSL is not available" and exits
    0, so a machine without WSL still passes. Otherwise it exits with the test binary's code.

.PARAMETER Configuration
    Debug (default) or Release.

.PARAMETER Distro
    The distro to run in; the default distro when empty.

.PARAMETER WslExecutable
    wsl.exe; tests point it elsewhere.

.PARAMETER TestArguments
    Passed to doctest unchanged, e.g. -tc="Some case*".
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [string] $Distro = '',
    [string] $WslExecutable = (Join-Path $env:SystemRoot 'System32\wsl.exe'),
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $TestArguments = @()
)

$ErrorActionPreference = 'Stop'

function Skip {
    Write-Host 'Linux tests skipped: WSL is not available'
    exit 0
}

if (-not (Test-Path -LiteralPath $WslExecutable)) { Skip }

# wsl.exe writes its own messages in UTF-16; the NULs that leaves in a byte-wise read go.
$listed = (& $WslExecutable --list --quiet 2>$null | Out-String) -replace "`0", ''
$distros = @($listed -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
if ($LASTEXITCODE -ne 0 -or $distros.Count -eq 0) { Skip }

$distroArguments = @()
if ($Distro -ne '') { $distroArguments = @('-d', $Distro) }

$binary = Join-Path $PSScriptRoot "..\bin\$Configuration-linux-x86_64\CoreLinuxTests\CoreLinuxTests"
$binary = [System.IO.Path]::GetFullPath($binary)
if (-not (Test-Path -LiteralPath $binary)) {
    Write-Host "no Linux test binary at $binary; build it with: vendor\bin\zig\zig.exe build -Dconfiguration=$Configuration"
    exit 1
}

$linuxPath = (& $WslExecutable @distroArguments -- wslpath -a ($binary -replace '\\', '/') | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $linuxPath -eq '') {
    Write-Host "wslpath could not translate $binary"
    exit 1
}

# Run from /tmp, so a test's relative paths land on the distro's own file system.
& $WslExecutable @distroArguments --cd /tmp -- $linuxPath @TestArguments
exit $LASTEXITCODE
