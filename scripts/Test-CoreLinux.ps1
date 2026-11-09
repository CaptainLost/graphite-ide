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

.PARAMETER Suite
    CoreLinuxTests, AgentTests, or All (default): which Linux test binaries to run. With All the
    script fails when either does.

.PARAMETER BinaryRoot
    Where the bin\<Configuration>-linux-x86_64 folders are; the repository's bin by default.

.PARAMETER Binary
    One test binary to run instead of the suites; tests use it.

.PARAMETER TestArguments
    Every other argument, passed to doctest unchanged, e.g. '-tc=*Some case*'. They reach the
    binary through `wsl --exec`, so the Linux shell expands nothing in them.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [string] $Distro = '',
    [string] $WslExecutable = (Join-Path $env:SystemRoot 'System32\wsl.exe'),
    [ValidateSet('CoreLinuxTests', 'AgentTests', 'All')] [string] $Suite = 'All',
    [string] $BinaryRoot = '',
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

# Not a parameter default: $PSScriptRoot is empty there in Windows PowerShell 5.1.
if ($BinaryRoot -eq '') { $BinaryRoot = Join-Path $PSScriptRoot '..\bin' }

$binaries = @()
if ($Binary -ne '') {
    $binaries = @($Binary)
}
else {
    $suites = if ($Suite -eq 'All') { @('CoreLinuxTests', 'AgentTests') } else { @($Suite) }
    $binaries = @($suites | ForEach-Object { Join-Path $BinaryRoot "$Configuration-linux-x86_64\$_\$_" })
}

$failed = $false
foreach ($candidate in $binaries) {
    $full = [System.IO.Path]::GetFullPath($candidate)
    if (-not (Test-Path -LiteralPath $full)) {
        Write-Host "no Linux test binary at $full; build it with: vendor\bin\zig\zig.exe build -Dconfiguration=$Configuration"
        exit 1
    }

    $translated = Invoke-Wsl @distroArguments --exec wslpath -a ($full -replace '\\', '/')
    if ($translated.ExitCode -ne 0 -or $translated.Output -eq '') {
        Write-Host "wslpath could not translate $full"
        if ($translated.Output -ne '') { Write-Host "  $($translated.Output)" }
        exit 1
    }

    Write-Host "== $(Split-Path -Leaf $full)"

    # Run from /tmp, so a test's relative paths land on the distro's own file system.
    & $WslExecutable @distroArguments --cd /tmp --exec $translated.Output @TestArguments
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}

if ($failed) { exit 1 }
exit 0
