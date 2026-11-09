#Requires -Version 5.1
# Tests for Get-Zig.ps1. Plain PowerShell, no external modules, no network.
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

function New-ScratchDirectory {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("getzig-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null
    return $root
}

$scriptUnderTest = Join-Path $PSScriptRoot '..\Get-Zig.ps1'

function Invoke-GetZig {
    param([string] $Destination, [string] $Archive)
    # A child's stderr line is an error record in PowerShell 5.1, which Stop would throw on.
    $ErrorActionPreference = 'Continue'
    $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptUnderTest -Destination $Destination -Archive $Archive 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

Write-Host 'Get-Zig.ps1'

# --- an archive whose checksum differs ------------------------------------------
$scratch = New-ScratchDirectory
try {
    $archive = Join-Path $scratch 'not-zig.zip'
    Compress-Archive -Path $PSCommandPath -DestinationPath $archive
    $destination = Join-Path $scratch 'zig'
    $result = Invoke-GetZig -Destination $destination -Archive $archive
    Assert-That -Because 'refuses an archive whose SHA-256 is not the pinned one' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'says which checksum it expected and which it got' -Condition ($result.Output -match 'checksum mismatch: expected [0-9a-f]{64}, got [0-9a-f]{64}')
    Assert-That -Because 'unpacks nothing from a refused archive' -Condition (-not (Test-Path (Join-Path $destination 'zig.exe')))
}
finally { Remove-Item $scratch -Recurse -Force }

# --- the pinned version already in place ----------------------------------------
$scratch = New-ScratchDirectory
try {
    $destination = Join-Path $scratch 'zig'
    New-Item -ItemType Directory -Path $destination | Out-Null
    Set-Content -LiteralPath (Join-Path $destination 'zig.exe') -Value 'stub'
    $marker = & powershell -NoProfile -ExecutionPolicy Bypass -Command ". '$scriptUnderTest' -PrintPin"
    Set-Content -LiteralPath (Join-Path $destination '.pinned-version') -Value $marker
    $result = Invoke-GetZig -Destination $destination -Archive (Join-Path $scratch 'missing.zip')
    Assert-That -Because 'leaves a destination holding the pinned version alone' -Condition ($result.ExitCode -eq 0)
    Assert-That -Because 'does not read the archive when nothing is to be done' -Condition ($result.Output -notmatch 'missing\.zip')
    Assert-That -Because 'keeps the zig.exe already there' -Condition ((Get-Content -LiteralPath (Join-Path $destination 'zig.exe')) -eq 'stub')
}
finally { Remove-Item $scratch -Recurse -Force }

# --- the pin itself --------------------------------------------------------------
$pin = & powershell -NoProfile -ExecutionPolicy Bypass -Command ". '$scriptUnderTest' -PrintPin"
Assert-That -Because 'pins zig 0.17.0 by its published SHA-256' -Condition ($pin -eq '0.17.0 b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428')

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)."
    exit 1
}
Write-Host 'All passed.'
exit 0
