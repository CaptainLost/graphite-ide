#Requires -Version 5.1
<#
.SYNOPSIS
    Fails when source draws an icon the font atlas never loads.

.DESCRIPTION
    Only the icons listed in kUsedIcons (Core/src/Core/ImGui/Icons.cpp) are baked into
    the font atlas's icon span; any other ICON_LC_ constant draws a wrong glyph (Inter's
    own private-use glyph in the interface face, or Lucide's unclipped one in the code face).
    This scans Core/src and Graphite/src for ICON_LC_ constants and reports every one
    missing from that list. It also reports every ICON_MDI_ constant, listed or not:
    Material Design Icons are no longer loaded, so each would draw a wrong glyph or none.
    Exits 1 on any finding.

.EXAMPLE
    .\scripts\Test-IconsLoaded.ps1
#>
[CmdletBinding()]
param(
    [string[]] $SourceDirectory,
    [string] $IconList
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $SourceDirectory) {
    $SourceDirectory = @((Join-Path $here '..\Core\src'), (Join-Path $here '..\Graphite\src'))
}
if (-not $IconList) {
    $IconList = Join-Path $here '..\Core\src\Core\ImGui\Icons.cpp'
}

if (-not (Test-Path -LiteralPath $IconList)) {
    Write-Output "The icon list '$IconList' does not exist."
    exit 1
}

$listSource = [System.IO.File]::ReadAllText($IconList)
$listMatch = [regex]::Match($listSource, 'kUsedIcons\[\]\s*=\s*\{(?<icons>[^}]*)\}')
if (-not $listMatch.Success) {
    Write-Output "No kUsedIcons list in '$IconList'."
    exit 1
}

$loaded = @{}
foreach ($icon in [regex]::Matches($listMatch.Groups['icons'].Value, 'ICON_LC_[A-Z0-9_]+')) {
    $loaded[$icon.Value] = $true
}

$problems = 0

foreach ($directory in $SourceDirectory) {
    if (-not (Test-Path -LiteralPath $directory)) {
        Write-Output "The source directory '$directory' does not exist."
        exit 1
    }

    $files = Get-ChildItem -LiteralPath $directory -Recurse -File |
        Where-Object { $_.Extension -in '.h', '.cpp', '.hpp', '.inl' }

    foreach ($file in $files) {
        $lineNumber = 0
        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            $lineNumber++
            foreach ($icon in [regex]::Matches($line, 'ICON_LC_[A-Z0-9_]+')) {
                if (-not $loaded.ContainsKey($icon.Value)) {
                    Write-Output ('{0}({1}): {2} is not in kUsedIcons, so the atlas never loads it' -f $file.FullName, $lineNumber, $icon.Value)
                    $problems++
                }
            }
            foreach ($icon in [regex]::Matches($line, 'ICON_MDI_[A-Z0-9_]+')) {
                Write-Output ('{0}({1}): {2} is a Material Design icon, which the atlas no longer loads; draw an ICON_LC_ one' -f $file.FullName, $lineNumber, $icon.Value)
                $problems++
            }
        }
    }
}

if ($problems -gt 0) {
    exit 1
}

Write-Output 'Every icon drawn is loaded.'
exit 0
