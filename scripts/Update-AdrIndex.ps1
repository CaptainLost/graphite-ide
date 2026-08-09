#Requires -Version 5.1
<#
.SYNOPSIS
    Rebuilds the architecture decision record index from the records themselves.

.DESCRIPTION
    Reads every NNNN-*.md file in docs/adr, takes its title, status and date from
    the record, and rewrites the table between the index markers in README.md.
    Prose outside the markers is untouched.

    The records are the source of truth. A row that disagrees with its record is
    corrected, a row whose record no longer exists is dropped, and a record with
    no row gains one. Nothing else edits those rows, so drift is repairable by
    running this rather than by hand.

    It refuses to write anything if any record is unreadable, so a partial index
    is never produced.

.EXAMPLE
    .\scripts\Update-AdrIndex.ps1
#>
[CmdletBinding()]
param(
    [string] $AdrDirectory
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrEmpty($AdrDirectory)) {
    # $PSScriptRoot is empty when the script is launched through -File with a
    # relative path, which is how README.md tells you to run these.
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $AdrDirectory = Join-Path $here '..\docs\adr'
}

if (-not (Test-Path -LiteralPath $AdrDirectory)) {
    throw "The ADR directory '$AdrDirectory' does not exist."
}
$AdrDirectory = (Resolve-Path -LiteralPath $AdrDirectory).Path

function Read-Utf8 {
    param([Parameter(Mandatory = $true)] [string] $Path)
    # Not Get-Content -Raw: on 5.1 it decodes with the ANSI codepage, so an em
    # dash read and written back comes out double-encoded.
    return [System.IO.File]::ReadAllText($Path)
}

$readmePath = Join-Path $AdrDirectory 'README.md'
if (-not (Test-Path -LiteralPath $readmePath)) {
    throw "Missing '$readmePath'."
}

$readme = Read-Utf8 -Path $readmePath
$markers = [regex]::Match($readme, '(?s)(<!-- adr-index -->).*?(<!-- /adr-index -->)')
if (-not $markers.Success) {
    throw "'$readmePath' has no <!-- adr-index --> ... <!-- /adr-index --> markers to rebuild."
}

# Every row is built before anything is written, so an unreadable record aborts
# the run rather than leaving half an index behind.
$rows = @()
Get-ChildItem -LiteralPath $AdrDirectory -Filter '*.md' -File |
    Where-Object { $_.Name -match '^\d{4}-' } |
    Sort-Object Name |
    ForEach-Object {
        $number = $_.Name.Substring(0, 4)
        $body = Read-Utf8 -Path $_.FullName

        $title = ([regex]::Match($body, "(?m)^# $number\. (.+?)\s*$")).Groups[1].Value
        if ([string]::IsNullOrEmpty($title)) {
            throw "$($_.Name): no '# $number. <title>' heading to build an index row from."
        }

        $status = ([regex]::Match($body, '(?m)^Status: (.+?)\s*$')).Groups[1].Value
        if ([string]::IsNullOrEmpty($status)) {
            throw "$($_.Name): no 'Status:' line to build an index row from."
        }

        $date = ([regex]::Match($body, '(?m)^Date: (\d{4}-\d{2}-\d{2})\s*$')).Groups[1].Value
        if ([string]::IsNullOrEmpty($date)) {
            throw "$($_.Name): no 'Date: YYYY-MM-DD' line to build an index row from."
        }

        $rows += "| [$number]($($_.Name)) | $title | $status | $date |"
    }

# Match the line ending the file already uses rather than forcing CRLF.
$newline = if ($readme -match "`r`n") { "`r`n" } else { "`n" }

$table = $markers.Groups[1].Value + $newline
if ($rows.Count -gt 0) {
    $table += ($rows -join $newline) + $newline
}
$table += $markers.Groups[2].Value

$rebuilt = $readme.Remove($markers.Index, $markers.Length).Insert($markers.Index, $table)
if ($rebuilt -cne $readme) {
    [System.IO.File]::WriteAllText($readmePath, $rebuilt, (New-Object System.Text.UTF8Encoding $false))
}

Write-Output "$($rows.Count) row(s) in $readmePath"
