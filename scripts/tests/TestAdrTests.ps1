#Requires -Version 5.1
# Tests for Test-Adr.ps1. Plain PowerShell, no external modules.
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

function ConvertTo-Lf {
    param([string] $Text)
    return $Text -replace "`r`n", "`n"
}

function New-Fixture {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("adrcheck-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null
    Write-Utf8NoBom -Path (Join-Path $root 'template.md') -Text (ConvertTo-Lf @'
# {{NUMBER}}. {{TITLE}}

## Context

<!-- What forces and constraints were true when this decision was made? Name
     them; a record without context cannot be re-judged later. -->
'@)
    Write-Utf8NoBom -Path (Join-Path $root 'README.md') -Text (ConvertTo-Lf @'
| # | Title | Status | Date |
|---|---|---|---|
<!-- adr-index -->
<!-- /adr-index -->
'@)
    return $root
}

function Add-Record {
    param([string] $Root, [string] $FileName, [string] $Body, [switch] $SkipIndex)
    Write-Utf8NoBom -Path (Join-Path $Root $FileName) -Text $Body
    if (-not $SkipIndex) {
        $readmePath = Join-Path $Root 'README.md'
        $readme = Get-Content -LiteralPath $readmePath -Raw
        $number = $FileName.Substring(0, 4)
        $readme = $readme.Replace('<!-- /adr-index -->', "| [$number]($FileName) | T | accepted | 2026-08-09 |`r`n<!-- /adr-index -->")
        Write-Utf8NoBom -Path $readmePath -Text $readme
    }
}

$valid = ConvertTo-Lf @'
# 0001. A well formed record

Status: accepted
Date: 2026-08-09

## Context

Something.

## Decision

Do the thing.

## Rationale

Because.

## Consequences

### Positive

Good.

### Negative

Bad.

### Neutral

Other.
'@


$scriptUnderTest = Join-Path $PSScriptRoot '..\Test-Adr.ps1'

function Invoke-Validator {
    param([string] $Root)
    $output = & $scriptUnderTest -AdrDirectory $Root 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

Write-Host 'Test-Adr.ps1'

# --- a well-formed record passes --------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-a-well-formed-record.md' -Body $valid
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'accepts a well-formed, indexed record' -Condition ($result.ExitCode -eq 0)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- the heading number must match the filename ------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0002-mismatched.md' -Body ($valid -replace '# 0001\.', '# 0009.')
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a heading whose number differs from the filename' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'says which file is wrong' -Condition ($result.Output -match '0002-mismatched\.md')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a missing required section ----------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-no-rationale.md' -Body ($valid -replace '(?ms)## Rationale.*?(?=## Consequences)', '')
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a record missing a required section' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'names the missing section' -Condition ($result.Output -match 'Rationale')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- consequences must be split three ways -----------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-no-negative.md' -Body ($valid -replace '(?ms)### Negative.*?(?=### Neutral)', '')
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects consequences without a Negative heading' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- unknown status -----------------------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-bad-status.md' -Body ($valid -replace 'Status: accepted', 'Status: probably')
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a status outside the lifecycle' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- superseded carries a successor -------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-superseded-ok.md' -Body ($valid -replace 'Status: accepted', 'Status: superseded by 0004')
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'accepts "superseded by NNNN"' -Condition ($result.ExitCode -eq 0)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- an unindexed record ------------------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-unindexed.md' -Body $valid -SkipIndex
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a record with no row in the index' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a duplicated number ------------------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-one.md' -Body $valid
    Add-Record -Root $fixture -FileName '0001-two.md' -Body $valid
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects two records sharing a number' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'reports the duplicated number' -Condition ($result.Output -match '0001')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a malformed filename -----------------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-Has-Capitals.md' -Body $valid
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a filename that is not lower-case kebab' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- an index row pointing at nothing -----------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-a-well-formed-record.md' -Body $valid
    $readmePath = Join-Path $fixture 'README.md'
    $readme = Get-Content -LiteralPath $readmePath -Raw
    Write-Utf8NoBom -Path $readmePath -Text $readme.Replace('<!-- /adr-index -->', "| [0002](0002-ghost.md) | Ghost | accepted | 2026-08-09 |`r`n<!-- /adr-index -->")
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects an index row whose record does not exist' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'names the missing record' -Condition ($result.Output -match '0002-ghost\.md')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a scaffolded but unfilled record ----------------------------------------
$fixture = New-Fixture
try {
    $unfilled = $valid -replace '(?m)^Something\.$', @'
<!-- What forces and constraints were true when this decision was made? Name
     them; a record without context cannot be re-judged later. -->
'@
    Add-Record -Root $fixture -FileName '0001-never-written.md' -Body $unfilled
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a record still carrying the template guidance' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'says the record is unfilled' -Condition ($result.Output -match 'unfilled')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- an unsubstituted placeholder --------------------------------------------
$fixture = New-Fixture
try {
    Add-Record -Root $fixture -FileName '0001-placeholder.md' -Body ($valid -replace 'Do the thing\.', 'Do {{TITLE}}.')
    $result = Invoke-Validator -Root $fixture
    Assert-That -Because 'rejects a record with an unsubstituted placeholder' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)"
    exit 1
}
Write-Host 'all passed'
exit 0
