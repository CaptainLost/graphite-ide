#Requires -Version 5.1
# Tests for Update-AdrIndex.ps1. Plain PowerShell, no external modules.
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

function Write-Utf8NoBom {
    param([string] $Path, [string] $Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

function Read-Utf8Text {
    param([string] $Path)
    return [System.IO.File]::ReadAllText($Path)
}

function ConvertTo-Lf {
    param([string] $Text)
    return $Text -replace "`r`n", "`n"
}

function New-Fixture {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("adrindex-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null
    Write-Utf8NoBom -Path (Join-Path $root 'template.md') -Text "# {{NUMBER}}. {{TITLE}}`n"
    Write-Utf8NoBom -Path (Join-Path $root 'README.md') -Text (ConvertTo-Lf @'
# Architecture decision records

Prose above the table, with an em dash EMDASH in it.

| # | Title | Status | Date |
|---|---|---|---|
<!-- adr-index -->
<!-- /adr-index -->

Prose below the table.
'@).Replace('EMDASH', [string][char]0x2014)
    return $root
}

function Add-Record {
    param([string] $Root, [string] $FileName, [string] $Title, [string] $Status = 'accepted', [string] $Date = '2026-08-10')
    $number = $FileName.Substring(0, 4)
    Write-Utf8NoBom -Path (Join-Path $Root $FileName) -Text (ConvertTo-Lf @"
# $number. $Title

Status: $Status
Date: $Date

## Context
"@)
}

$scriptUnderTest = Join-Path $PSScriptRoot '..\Update-AdrIndex.ps1'

Write-Host 'Update-AdrIndex.ps1'

# --- builds a row per record, in number order --------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0002-second.md' -Title 'Second'
    Add-Record -Root $fixture -FileName '0001-first.md' -Title 'First'
    & $scriptUnderTest -AdrDirectory $fixture | Out-Null

    $readme = Read-Utf8Text (Join-Path $fixture 'README.md')
    Assert-That -Because 'writes a row for the first record' `
        -Condition ($readme -match '(?m)^\| \[0001\]\(0001-first\.md\) \| First \| accepted \| 2026-08-10 \|\r?$')
    Assert-That -Because 'writes a row for the second record' `
        -Condition ($readme -match '(?m)^\| \[0002\]\(0002-second\.md\) \| Second \| accepted \| 2026-08-10 \|\r?$')
    Assert-That -Because 'orders rows by number' `
        -Condition ($readme.IndexOf('[0001]') -lt $readme.IndexOf('[0002]'))
    Assert-That -Because 'leaves the prose above the table alone' `
        -Condition ($readme -match 'Prose above the table')
    Assert-That -Because 'leaves the prose below the table alone' `
        -Condition ($readme -match 'Prose below the table')
    Assert-That -Because 'leaves non-ASCII in the prose unharmed' `
        -Condition ($readme.Contains([string][char]0x2014))
}
finally { Remove-Item $fixture -Recurse -Force }

# --- takes the status from the record ----------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-declined.md' -Title 'Declined' -Status 'rejected'
    Add-Record -Root $fixture -FileName '0002-replaced.md' -Title 'Replaced' -Status 'superseded by 0009'
    & $scriptUnderTest -AdrDirectory $fixture | Out-Null

    $readme = Read-Utf8Text (Join-Path $fixture 'README.md')
    Assert-That -Because 'carries a rejected status into the index' -Condition ($readme -match '\| rejected \|')
    Assert-That -Because 'carries a superseded status into the index' -Condition ($readme -match '\| superseded by 0009 \|')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- repairs a mangled row and drops a stale one -----------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-first.md' -Title 'First'
    $readmePath = Join-Path $fixture 'README.md'
    $readme = Read-Utf8Text $readmePath
    Write-Utf8NoBom -Path $readmePath -Text $readme.Replace('<!-- /adr-index -->', @'
| [0001](0001-first.md) | First | accepted01 2026-08-10 |
| [0007](0007-ghost.md) | Ghost | accepted | 2026-08-10 |
<!-- /adr-index -->
'@)
    & $scriptUnderTest -AdrDirectory $fixture | Out-Null

    $readme = Read-Utf8Text $readmePath
    Assert-That -Because 'repairs a row with the wrong column count' -Condition ($readme -notmatch 'accepted01')
    Assert-That -Because 'drops a row whose record does not exist' -Condition ($readme -notmatch '0007-ghost')
    Assert-That -Because 'keeps the row it can rebuild' -Condition ($readme -match '\[0001\]\(0001-first\.md\)')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- idempotent ---------------------------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-first.md' -Title 'First'
    & $scriptUnderTest -AdrDirectory $fixture | Out-Null
    $once = Read-Utf8Text (Join-Path $fixture 'README.md')
    & $scriptUnderTest -AdrDirectory $fixture | Out-Null
    $twice = Read-Utf8Text (Join-Path $fixture 'README.md')
    Assert-That -Because 'a second run changes nothing' -Condition ($once -ceq $twice)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- refuses a record it cannot read ------------------------------------------
$fixture = New-Fixture
try {
    Write-Utf8NoBom -Path (Join-Path $fixture '0001-headless.md') -Text "no heading, no status`n"
    $threw = $false
    try { & $scriptUnderTest -AdrDirectory $fixture | Out-Null } catch { $threw = $true }
    Assert-That -Because 'refuses a record with no heading or status' -Condition $threw

    $readme = Read-Utf8Text (Join-Path $fixture 'README.md')
    Assert-That -Because 'writes no partial index when it refuses' -Condition ($readme -notmatch '\| \[0001\]')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a missing marker is an error ---------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-first.md' -Title 'First'
    Write-Utf8NoBom -Path (Join-Path $fixture 'README.md') -Text "no markers here`n"
    $threw = $false
    try { & $scriptUnderTest -AdrDirectory $fixture | Out-Null } catch { $threw = $true }
    Assert-That -Because 'refuses a README with no index markers' -Condition $threw
}
finally { Remove-Item $fixture -Recurse -Force }

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)"
    exit 1
}
Write-Host 'all passed'
exit 0
