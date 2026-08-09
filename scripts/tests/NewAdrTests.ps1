#Requires -Version 5.1
# Tests for New-Adr.ps1. Plain PowerShell, no external modules.
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

function New-Fixture {
    # A throwaway ADR directory holding only what the script needs to read.
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("adr-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null

    $template = @'
# {{NUMBER}}. {{TITLE}}

Status: proposed
Date: {{DATE}}

## Context

## Decision

## Rationale

## Consequences

### Positive

### Negative

### Neutral
'@
    Write-Utf8NoBom -Path (Join-Path $root 'template.md') -Text $template

    $readme = @'
| # | Title | Status | Date |
|---|---|---|---|
<!-- adr-index -->
| [0001](0001-first.md) | First | accepted | 2026-08-09 |
<!-- /adr-index -->
'@
    Write-Utf8NoBom -Path (Join-Path $root 'README.md') -Text $readme
    Write-Utf8NoBom -Path (Join-Path $root '0001-first.md') -Text "# 0001. First`r`n"

    return $root
}

$scriptUnderTest = Join-Path $PSScriptRoot '..\New-Adr.ps1'

Write-Host 'New-Adr.ps1'

# --- allocates the next number and slugifies the title -----------------------
$fixture = New-Fixture
try {
    $path = & $scriptUnderTest -Title 'Require braces around every control-flow body' -AdrDirectory $fixture -Date '2026-08-09'

    Assert-That -Because 'names the file with the next number and a kebab-case slug' `
        -Condition ((Split-Path $path -Leaf) -eq '0002-require-braces-around-every-control-flow-body.md')

    $body = Get-Content $path -Raw
    Assert-That -Because 'fills the heading with the number and the title' `
        -Condition ($body -match '(?m)^# 0002\. Require braces around every control-flow body$')
    Assert-That -Because 'fills the date' -Condition ($body -match '(?m)^Date: 2026-08-09$')
    Assert-That -Because 'starts the record as proposed' -Condition ($body -match '(?m)^Status: proposed$')
    Assert-That -Because 'leaves no placeholder behind' -Condition ($body -notmatch '\{\{')

    $readme = Get-Content (Join-Path $fixture 'README.md') -Raw
    Assert-That -Because 'appends the index row inside the markers' `
        -Condition ($readme -match '(?m)^\| \[0002\]\(0002-require-braces-around-every-control-flow-body\.md\) \| Require braces around every control-flow body \| proposed \| 2026-08-09 \|\r?$')
    Assert-That -Because 'keeps the closing marker last' `
        -Condition ($readme.IndexOf('| [0002]') -lt $readme.IndexOf('<!-- /adr-index -->'))
    Assert-That -Because 'leaves the existing row alone' -Condition ($readme -match '\[0001\]\(0001-first\.md\)')
}
finally {
    Remove-Item $fixture -Recurse -Force
}

# --- strips punctuation and collapses separators in the slug -----------------
$fixture = New-Fixture
try {
    $path = & $scriptUnderTest -Title 'Return `Result<T>`, do not throw' -AdrDirectory $fixture -Date '2026-08-09'
    Assert-That -Because 'reduces punctuation to single hyphens without a trailing one' `
        -Condition ((Split-Path $path -Leaf) -eq '0002-return-result-t-do-not-throw.md')
}
finally {
    Remove-Item $fixture -Recurse -Force
}

# --- numbering ignores non-record files and does not reuse numbers -----------
$fixture = New-Fixture
try {
    Write-Utf8NoBom -Path (Join-Path $fixture '0007-later.md') -Text "# 0007. Later`r`n"
    $path = & $scriptUnderTest -Title 'After a gap' -AdrDirectory $fixture -Date '2026-08-09'
    Assert-That -Because 'continues from the highest number present, not from the count' `
        -Condition ((Split-Path $path -Leaf) -eq '0008-after-a-gap.md')
}
finally {
    Remove-Item $fixture -Recurse -Force
}

# --- a taken number is skipped, never reused ---------------------------------
$fixture = New-Fixture
try {
    Write-Utf8NoBom -Path (Join-Path $fixture '0002-taken.md') -Text "# 0002. Taken`r`n"
    $path = & $scriptUnderTest -Title 'Taken' -AdrDirectory $fixture -Date '2026-08-09'
    Assert-That -Because 'skips past a taken number rather than colliding with it' `
        -Condition ((Split-Path $path -Leaf) -eq '0003-taken.md')
}
finally {
    Remove-Item $fixture -Recurse -Force
}

# --- a missing index marker is an error, not a silent no-op ------------------
$fixture = New-Fixture
try {
    Write-Utf8NoBom -Path (Join-Path $fixture 'README.md') -Text "no markers here`r`n"
    $threw = $false
    try { & $scriptUnderTest -Title 'Orphan' -AdrDirectory $fixture -Date '2026-08-09' | Out-Null }
    catch { $threw = $true }
    Assert-That -Because 'refuses to write a record it cannot index' -Condition $threw
    Assert-That -Because 'writes no record when it refuses' `
        -Condition (-not (Test-Path (Join-Path $fixture '0002-orphan.md')))
}
finally {
    Remove-Item $fixture -Recurse -Force
}

# --- a missing template is an error ------------------------------------------
$fixture = New-Fixture
try {
    Remove-Item (Join-Path $fixture 'template.md')
    $threw = $false
    try { & $scriptUnderTest -Title 'No template' -AdrDirectory $fixture -Date '2026-08-09' | Out-Null }
    catch { $threw = $true }
    Assert-That -Because 'refuses to work without template.md' -Condition $threw
}
finally {
    Remove-Item $fixture -Recurse -Force
}

# --- a title with nothing usable in it is an error ---------------------------
$fixture = New-Fixture
try {
    $threw = $false
    try { & $scriptUnderTest -Title '---' -AdrDirectory $fixture -Date '2026-08-09' | Out-Null }
    catch { $threw = $true }
    Assert-That -Because 'refuses a title that slugifies to nothing' -Condition $threw
}
finally {
    Remove-Item $fixture -Recurse -Force
}

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)"
    exit 1
}
Write-Host 'all passed'
exit 0
