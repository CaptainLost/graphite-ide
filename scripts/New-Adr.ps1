#Requires -Version 5.1
<#
.SYNOPSIS
    Creates a new architecture decision record and indexes it.

.DESCRIPTION
    Allocates the next four-digit number from docs/adr, writes the record from
    template.md with the number, title and date filled in, appends a row to the
    index table in README.md, and prints the new file's path.

.EXAMPLE
    .\scripts\New-Adr.ps1 "Require braces around every control-flow body"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string] $Title,

    # Overridable so the tests can run against a temporary directory.
    [string] $AdrDirectory,

    # Overridable so the tests do not depend on the day they are run.
    [string] $Date = (Get-Date -Format 'yyyy-MM-dd')
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrEmpty($AdrDirectory)) {
    # $PSScriptRoot is empty when the script is launched through -File with a
    # relative path, which is exactly how README.md tells you to run it.
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $AdrDirectory = Join-Path $here '..\docs\adr'
}

function ConvertTo-Slug {
    param([Parameter(Mandatory = $true)] [string] $Text)

    $slug = $Text.ToLowerInvariant()
    $slug = $slug -replace '[^a-z0-9]+', '-'
    $slug = $slug -replace '^-+', ''
    $slug = $slug -replace '-+$', ''
    if ([string]::IsNullOrEmpty($slug)) {
        throw "The title '$Text' contains no characters usable in a filename."
    }
    return $slug
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Text
    )
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

function Read-Utf8 {
    param([Parameter(Mandatory = $true)] [string] $Path)
    # Not Get-Content -Raw: on 5.1 it decodes with the ANSI codepage, so an em
    # dash read and written back comes out double-encoded.
    return [System.IO.File]::ReadAllText($Path)
}

if (-not (Test-Path -LiteralPath $AdrDirectory)) {
    throw "The ADR directory '$AdrDirectory' does not exist."
}
$AdrDirectory = (Resolve-Path -LiteralPath $AdrDirectory).Path

$templatePath = Join-Path $AdrDirectory 'template.md'
$readmePath = Join-Path $AdrDirectory 'README.md'
foreach ($required in @($templatePath, $readmePath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing '$required'."
    }
}

# The highest number present, not the count: numbers are never reused, so a
# deleted or renumbered record must not hand its number to the next writer.
$highest = 0
Get-ChildItem -LiteralPath $AdrDirectory -Filter '*.md' -File | ForEach-Object {
    if ($_.Name -match '^(\d{4})-') {
        $candidate = [int]$Matches[1]
        if ($candidate -gt $highest) { $highest = $candidate }
    }
}
$number = '{0:D4}' -f ($highest + 1)

$slug = ConvertTo-Slug -Text $Title
$recordPath = Join-Path $AdrDirectory "$number-$slug.md"
if (Test-Path -LiteralPath $recordPath) {
    throw "'$recordPath' already exists; refusing to overwrite it."
}

# Checked before anything is written, so a directory that cannot be indexed
# does not end up with an orphaned record in it.
$closingMarker = '<!-- /adr-index -->'
$readme = Read-Utf8 -Path $readmePath
if ($readme -notmatch [regex]::Escape($closingMarker)) {
    throw "'$readmePath' has no $closingMarker marker, so the record cannot be indexed."
}

$body = Read-Utf8 -Path $templatePath
$body = $body.Replace('{{NUMBER}}', $number).Replace('{{TITLE}}', $Title).Replace('{{DATE}}', $Date)
Write-Utf8NoBom -Path $recordPath -Text $body

# Match the line ending the file already uses rather than forcing CRLF, so the
# index does not end up with mixed endings.
$newline = if ($readme -match "`r`n") { "`r`n" } else { "`n" }

$row = "| [$number]($number-$slug.md) | $Title | proposed | $Date |"
$readme = $readme.Replace($closingMarker, "$row$newline$closingMarker")
Write-Utf8NoBom -Path $readmePath -Text $readme

Write-Output $recordPath
