#Requires -Version 5.1
<#
.SYNOPSIS
    Fails when source calls ImGui::Checkbox, so ImGui's own box cannot creep back.

.DESCRIPTION
    ImGui's checkbox fills its box with the frame colour and has no outline of its
    own, so it cannot take the look the app gives its checkboxes. Checkboxes go
    through Graphite::DrawTickBox instead. This scans Core/src and Graphite/src and
    reports every line that calls ImGui::Checkbox or ImGui::CheckboxFlags. Exits 1
    on any finding.

.EXAMPLE
    .\scripts\Test-NoImGuiCheckbox.ps1
#>
[CmdletBinding()]
param(
    [string[]] $SourceDirectory
)

$ErrorActionPreference = 'Stop'

$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $SourceDirectory) {
    $SourceDirectory = @((Join-Path $here '..\Core\src'), (Join-Path $here '..\Graphite\src'))
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
            if ($line -match 'ImGui::Checkbox(Flags)?\s*\(') {
                Write-Output ('{0}({1}): ImGui::Checkbox draws ImGui''s own box; use Graphite::DrawTickBox' -f $file.FullName, $lineNumber)
                $problems++
            }
        }
    }
}

if ($problems -gt 0) {
    exit 1
}

Write-Output 'No ImGui::Checkbox calls.'
exit 0
