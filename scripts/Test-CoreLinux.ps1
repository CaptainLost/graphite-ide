#Requires -Version 5.1
<#
.SYNOPSIS
    Runs CoreLinuxTests, the Linux build of Core's doctest cases, inside WSL.

.DESCRIPTION
    The binary is built by `vendor\bin\zig\zig.exe build -Dconfiguration=<Configuration>` (or by
    MSBuild through the CoreLinux project). Building needs no WSL; running does. Without WSL, with
    no distro installed, or with no default distro (and no -Distro), this prints "Linux tests
    skipped: WSL is not available" and exits 0, so such a machine still passes. Otherwise it exits
    with the test binary's code.

.PARAMETER Configuration
    Debug (default) or Release.

.PARAMETER Distro
    The distro to run in; the default distro when empty.

.PARAMETER WslExecutable
    wsl.exe; tests point it elsewhere.

.PARAMETER Binary
    The test binary; defaults to bin\<Configuration>-linux-x86_64\CoreLinuxTests\CoreLinuxTests.

.PARAMETER TestArguments
    Every other argument, passed to doctest unchanged, e.g. '-tc=*Some case*'. They reach the
    binary through `wsl --exec`, so the Linux shell expands nothing in them.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [string] $Distro = '',
    [string] $WslExecutable = (Join-Path $env:SystemRoot 'System32\wsl.exe'),
    [string] $Binary = '',
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $TestArguments = @()
)

# Native commands only from here: a stderr line from one is an error record in PowerShell 5.1,
# which Stop would turn into a failure of this script.
$ErrorActionPreference = 'Continue'

function Skip {
    param([string] $Reason = '')
    Write-Host 'Linux tests skipped: WSL is not available'
    if ($Reason -ne '') { Write-Host "  $Reason" }
    exit 0
}

# wsl.exe writes its own messages in UTF-16; the NULs that leaves in a byte-wise read go.
function Invoke-Wsl {
    $output = (& $WslExecutable @args 2>&1 | Out-String) -replace "`0", ''
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output.Trim() }
}

if (-not (Test-Path -LiteralPath $WslExecutable)) { Skip }

$listed = Invoke-Wsl --list --quiet
$distros = @($listed.Output -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
if ($listed.ExitCode -ne 0 -or $distros.Count -eq 0) { Skip }

$distroArguments = @()
if ($Distro -ne '') { $distroArguments = @('-d', $Distro) }

# A listed distro is not necessarily a default one: Docker Desktop's are listed but never chosen.
$reachable = Invoke-Wsl @distroArguments --exec true
if ($reachable.ExitCode -ne 0) { Skip $reachable.Output }

if ($Binary -eq '') {
    $Binary = Join-Path $PSScriptRoot "..\bin\$Configuration-linux-x86_64\CoreLinuxTests\CoreLinuxTests"
}
$Binary = [System.IO.Path]::GetFullPath($Binary)
if (-not (Test-Path -LiteralPath $Binary)) {
    Write-Host "no Linux test binary at $Binary; build it with: vendor\bin\zig\zig.exe build -Dconfiguration=$Configuration"
    exit 1
}

$translated = Invoke-Wsl @distroArguments --exec wslpath -a ($Binary -replace '\\', '/')
if ($translated.ExitCode -ne 0 -or $translated.Output -eq '') {
    Write-Host "wslpath could not translate $Binary"
    if ($translated.Output -ne '') { Write-Host "  $($translated.Output)" }
    exit 1
}

# Run from /tmp, so a test's relative paths land on the distro's own file system.
& $WslExecutable @distroArguments --cd /tmp --exec $translated.Output @TestArguments
exit $LASTEXITCODE
