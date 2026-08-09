#Requires -Version 5.1
<#
.SYNOPSIS
    Validates the shape of every architecture decision record.

.DESCRIPTION
    Checks each NNNN-*.md file in docs/adr for a well-formed filename, a heading
    whose number matches it, a status within the lifecycle, a date, the required
    sections, the three-way split of Consequences, and a row in the index. Also
    checks that no two records share a number and that no index row points at a
    record that does not exist. Prints one line per problem and exits 1 if there
    were any.

    This shares no code with New-Adr.ps1 on purpose: it has to be able to judge
    a record that was written by hand.

.EXAMPLE
    .\scripts\Test-Adr.ps1
#>
[CmdletBinding()]
param(
    [string] $AdrDirectory
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrEmpty($AdrDirectory)) {
    # $PSScriptRoot is empty when the script is launched through -File with a
    # relative path, which is exactly how README.md tells you to run it.
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $AdrDirectory = Join-Path $here '..\docs\adr'
}

if (-not (Test-Path -LiteralPath $AdrDirectory)) {
    Write-Output "The ADR directory '$AdrDirectory' does not exist."
    exit 1
}
$AdrDirectory = (Resolve-Path -LiteralPath $AdrDirectory).Path

$problems = New-Object System.Collections.Generic.List[string]

$readmePath = Join-Path $AdrDirectory 'README.md'
$readme = if (Test-Path -LiteralPath $readmePath) { Get-Content -LiteralPath $readmePath -Raw } else { '' }
if ([string]::IsNullOrEmpty($readme)) {
    $problems.Add('README.md is missing or empty, so nothing can be indexed.')
}

$records = @(Get-ChildItem -LiteralPath $AdrDirectory -Filter '*.md' -File |
    Where-Object { $_.Name -match '^\d{4}-' } |
    Sort-Object Name)

if ($records.Count -eq 0) {
    $problems.Add('No records found. Expected at least one NNNN-*.md file.')
}

$seenNumbers = @{}
$requiredSections = @('Context', 'Decision', 'Rationale', 'Consequences')
$consequenceHeadings = @('Positive', 'Negative', 'Neutral')

foreach ($record in $records) {
    $name = $record.Name

    if ($name -cnotmatch '^\d{4}-[a-z0-9]+(-[a-z0-9]+)*\.md$') {
        $problems.Add("$name : filename is not NNNN-kebab-case-title.md in lower case.")
    }

    $number = $name.Substring(0, 4)
    if ($seenNumbers.ContainsKey($number)) {
        $problems.Add("$name : number $number is already used by $($seenNumbers[$number]).")
    }
    else {
        $seenNumbers[$number] = $name
    }

    $body = Get-Content -LiteralPath $record.FullName -Raw

    if ($body -notmatch "(?m)^# $number\. \S") {
        $problems.Add("$name : heading must read '# $number. <title>' and match the filename.")
    }

    if ($body -notmatch '(?m)^Status: (proposed|accepted|rejected|deprecated|superseded by \d{4})\s*$') {
        $problems.Add("$name : status must be proposed, accepted, rejected, deprecated, or 'superseded by NNNN'.")
    }

    if ($body -notmatch '(?m)^Date: \d{4}-\d{2}-\d{2}\s*$') {
        $problems.Add("$name : date must be present as 'Date: YYYY-MM-DD'.")
    }

    foreach ($section in $requiredSections) {
        if ($body -notmatch "(?m)^## $section\s*$") {
            $problems.Add("$name : missing required section '$section'.")
        }
    }

    foreach ($heading in $consequenceHeadings) {
        if ($body -notmatch "(?m)^### $heading\s*$") {
            $problems.Add("$name : Consequences must include a '### $heading' heading.")
        }
    }

    if ($readme -notmatch [regex]::Escape("($name)")) {
        $problems.Add("$name : has no row in the README.md index.")
    }
}

# Index rows pointing at records that no longer exist.
foreach ($match in [regex]::Matches($readme, '\((\d{4}-[a-z0-9-]+\.md)\)')) {
    $linked = $match.Groups[1].Value
    if (-not (Test-Path -LiteralPath (Join-Path $AdrDirectory $linked))) {
        $problems.Add("README.md : indexes '$linked', which does not exist.")
    }
}

if ($problems.Count -gt 0) {
    foreach ($problem in $problems) { Write-Output $problem }
    Write-Output "$($problems.Count) problem(s) in $AdrDirectory"
    exit 1
}

Write-Output "$($records.Count) record(s) in $AdrDirectory, all well formed"
exit 0
