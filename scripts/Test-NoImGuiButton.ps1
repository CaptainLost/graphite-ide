#Requires -Version 5.1
<#
.SYNOPSIS
    Fails when source calls ImGui::Button or ImGui::SmallButton, so a bordered button cannot creep back.

.DESCRIPTION
    The theme gives every frame ImGui draws a border in the line colour, which a field wants
    and a button that shows a word does not. Such buttons go through Core::TextButton and
    Core::SmallTextButton, which take the border off and round the corners a pixel less than
    a field. This scans Core/src and Graphite/src and reports every line that calls
    ImGui::Button or ImGui::SmallButton anywhere but in the helper itself,
    Core/src/Core/ImGui/TextButton.cpp. Exits 1 on any finding.

.EXAMPLE
    .\scripts\Test-NoImGuiButton.ps1
#>
[CmdletBinding()]
param(
    [string[]] $SourceDirectory,
    [string] $Helper
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $SourceDirectory) {
    $SourceDirectory = @((Join-Path $here '..\Core\src'), (Join-Path $here '..\Graphite\src'))
}
if (-not $Helper) {
    $Helper = Join-Path $here '..\Core\src\Core\ImGui\TextButton.cpp'
}

$helperPath = [System.IO.Path]::GetFullPath($Helper)
$problems = 0

foreach ($directory in $SourceDirectory) {
    if (-not (Test-Path -LiteralPath $directory)) {
        Write-Output "The source directory '$directory' does not exist."
        exit 1
    }

    $files = Get-ChildItem -LiteralPath $directory -Recurse -File |
        Where-Object { $_.Extension -in '.h', '.cpp', '.hpp', '.inl' -and $_.FullName -ne $helperPath }

    foreach ($file in $files) {
        $lineNumber = 0
        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            $lineNumber++
            if ($line -match 'ImGui::(Small)?Button\s*\(') {
                Write-Output ('{0}({1}): ImGui::Button draws a bordered frame; use Core::TextButton or Core::SmallTextButton' -f $file.FullName, $lineNumber)
                $problems++
            }
        }
    }
}

if ($problems -gt 0) {
    exit 1
}

Write-Output 'No ImGui::Button calls.'
exit 0
