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
}
finally { Remove-Item $scratch -Recurse -Force }

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)."
    exit 1
}
Write-Host 'All passed.'
exit 0
