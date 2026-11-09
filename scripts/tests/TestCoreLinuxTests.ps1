#Requires -Version 5.1
# Tests for Test-CoreLinux.ps1: the cases that need no WSL. Plain PowerShell, no external modules.
# Exits 0 when every test passes, 1 otherwise.

$ErrorActionPreference = 'Stop'

$script:Failures = 0

function Assert-That {
    param(
        [Parameter(Mandatory = $true)] [string] $Because,
        [Parameter(Mandatory = $true)] [bool] $Condition
    )
    if ($Condition) {
        Write-Host "  PASS  $Because"
    }
    else {
        Write-Host "  FAIL  $Because"
        $script:Failures++
    }
}

$scriptUnderTest = Join-Path $PSScriptRoot '..\Test-CoreLinux.ps1'

function Invoke-TestCoreLinux {
    param([string] $WslExecutable)
    # A child's stderr line is an error record in PowerShell 5.1, which Stop would throw on.
    $ErrorActionPreference = 'Continue'
    $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptUnderTest -WslExecutable $WslExecutable 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

Write-Host 'Test-CoreLinux.ps1'

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("corelinux-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    # --- no wsl.exe at all ------------------------------------------------------
    $result = Invoke-TestCoreLinux -WslExecutable (Join-Path $scratch 'missing\wsl.exe')
    Assert-That -Because 'skips when wsl.exe is missing' -Condition ($result.ExitCode -eq 0)
    Assert-That -Because 'says the Linux tests were skipped and why' -Condition ($result.Output -match 'Linux tests skipped: WSL is not available')

    # --- wsl.exe present, no distro installed ------------------------------------
    $stub = Join-Path $scratch 'wsl.cmd'
    Set-Content -LiteralPath $stub -Value '@exit /b 0' -Encoding ASCII
    $result = Invoke-TestCoreLinux -WslExecutable $stub
    Assert-That -Because 'skips when WSL lists no distro' -Condition ($result.ExitCode -eq 0)
    Assert-That -Because 'says so in the same words' -Condition ($result.Output -match 'Linux tests skipped: WSL is not available')

    # A wsl stand-in that lists one distro, records every call, translates any path, and fails
    # a command run in the default distro when STUB_NO_DEFAULT is set.
    $stubScript = Join-Path $scratch 'wsl-stub.ps1'
    $calls = Join-Path $scratch 'calls.txt'
    Set-Content -LiteralPath $stubScript -Encoding ASCII -Value @'
Add-Content -LiteralPath (Join-Path $PSScriptRoot 'calls.txt') -Value ($args -join ' | ')
if ($args[0] -eq '--list') { 'docker-desktop'; exit 0 }
if ($env:STUB_NO_DEFAULT -and -not ($args -contains '-d')) { 'There is no default distribution.'; exit 1 }
if ($args -contains 'wslpath') { '/fake/CoreLinuxTests'; exit 0 }
exit 0
'@
    $binary = Join-Path $scratch 'CoreLinuxTests'
    Set-Content -LiteralPath $binary -Value 'not a real binary'

    # In-process, as a user calls it from PowerShell; a binding error counts as a failure.
    function Invoke-InProcess {
        param([hashtable] $Named, [string[]] $Rest = @())
        $ErrorActionPreference = 'Continue'
        try {
            $output = & $scriptUnderTest @Named @Rest *>&1 | Out-String
            return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
        }
        catch {
            return [pscustomobject]@{ ExitCode = -1; Output = $_.ToString() }
        }
    }

    # --- a quoted doctest argument, the form CLAUDE.md shows ---------------------
    $result = Invoke-InProcess -Named @{ WslExecutable = $stubScript; Binary = $binary } -Rest @('-tc=*A case, with a comma*')
    $exitCode = $result.ExitCode
    $recorded = if (Test-Path -LiteralPath $calls) { Get-Content -LiteralPath $calls -Raw } else { '' }
    Assert-That -Because 'passes a quoted doctest argument to the test binary' -Condition (($exitCode -eq 0) -and ($recorded -match [regex]::Escape('/fake/CoreLinuxTests | -tc=*A case, with a comma*')))
    Assert-That -Because 'runs the binary with --exec, so the Linux shell expands nothing' -Condition ($recorded -match '--exec \| /fake/CoreLinuxTests')
    Remove-Item -LiteralPath $calls -ErrorAction SilentlyContinue

    # --- by default both Linux suites run ----------------------------------------
    $binaryRoot = Join-Path $scratch 'bin'
    foreach ($suite in 'CoreLinuxTests', 'AgentTests') {
        New-Item -ItemType Directory -Path (Join-Path $binaryRoot "Debug-linux-x86_64\$suite") -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $binaryRoot "Debug-linux-x86_64\$suite\$suite") -Value 'not a real binary'
    }
    $result = Invoke-InProcess -Named @{ WslExecutable = $stubScript; BinaryRoot = $binaryRoot }
    $recorded = if (Test-Path -LiteralPath $calls) { Get-Content -LiteralPath $calls -Raw } else { '' }
    Assert-That -Because 'translates both suites'' binaries' -Condition (($recorded -match 'CoreLinuxTests/CoreLinuxTests') -and ($recorded -match 'AgentTests/AgentTests'))
    Assert-That -Because 'runs a binary once for each suite' -Condition (([regex]::Matches($recorded, '--cd \| /tmp \| --exec')).Count -eq 2)
    Remove-Item -LiteralPath $calls -ErrorAction SilentlyContinue

    $result = Invoke-InProcess -Named @{ WslExecutable = $stubScript; BinaryRoot = $binaryRoot; Suite = 'AgentTests' }
    $recorded = if (Test-Path -LiteralPath $calls) { Get-Content -LiteralPath $calls -Raw } else { '' }
    Assert-That -Because 'runs only the suite asked for' -Condition (($recorded -match 'AgentTests/AgentTests') -and ($recorded -notmatch 'CoreLinuxTests/CoreLinuxTests'))
    Remove-Item -LiteralPath $calls -ErrorAction SilentlyContinue

    # --- WSL lists a distro but none is the default ------------------------------
    $env:STUB_NO_DEFAULT = '1'
    try {
        $result = Invoke-InProcess -Named @{ WslExecutable = $stubScript; Binary = $binary }
        $output = $result.Output
        $exitCode = $result.ExitCode
    }
    finally { Remove-Item Env:\STUB_NO_DEFAULT }
    Assert-That -Because 'skips when no distro is the default' -Condition ($exitCode -eq 0)
    Assert-That -Because 'says it skipped, and what WSL said' -Condition (($output -match 'Linux tests skipped: WSL is not available') -and ($output -match 'no default distribution'))
}
finally { Remove-Item $scratch -Recurse -Force }

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)."
    exit 1
}
Write-Host 'All passed.'
exit 0
