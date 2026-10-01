#Requires -Version 5.1
<#
.SYNOPSIS
    Fails when source calls .string() on anything, so path::string() cannot creep back.

.DESCRIPTION
    On MSVC, std::filesystem::path::string() converts through the ANSI code page
    and throws for characters it cannot map. Paths go through Core::ToUtf8
    instead. This scans Core/src and Graphite/src and reports every line that
    contains .string() and does not end with the marker comment "// not a path",
    which marks the rare call on a type that is not a path. Exits 1 on any
    finding.

.EXAMPLE
    .\scripts\Test-NoPathString.ps1
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

$marker = '// not a path'
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
            if ($line -match '\.string\s*\(' -and -not $line.TrimEnd().EndsWith($marker)) {
                Write-Output ('{0}({1}): .string() on a path breaks on non-ANSI names; use Core::ToUtf8, or end the line with "{2}"' -f $file.FullName, $lineNumber, $marker)
                $problems++
            }
        }
    }
}

if ($problems -gt 0) {
    exit 1
}

Write-Output 'No .string() calls without the marker.'
exit 0
