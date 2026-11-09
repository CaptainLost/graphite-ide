#Requires -Version 5.1
<#
.SYNOPSIS
    Puts the pinned zig into vendor/bin/zig, where the Linux build looks for it.

.DESCRIPTION
    zig cross-compiles Core's Linux part from Windows. It is 361 MB unpacked, so it is not
    committed: this script downloads the pinned release, refuses it unless its SHA-256 is the
    pinned one, and unpacks it. A destination already holding the pinned version is left alone.
    Upgrading means changing both constants below.

.PARAMETER Destination
    Where zig.exe ends up. Defaults to vendor/bin/zig under the repository.

.PARAMETER Archive
    A zip already on disk to use instead of downloading.

.PARAMETER PrintPin
    Prints "<version> <sha256>" and stops; the tests read the pin through it.
#>
param(
    [string] $Destination = (Join-Path $PSScriptRoot '..\vendor\bin\zig'),
    [string] $Archive = '',
    [switch] $PrintPin
)

$ErrorActionPreference = 'Stop'

$PinnedVersion = '0.17.0'
$PinnedSha256 = 'b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428'
$PinnedUrl = "https://ziglang.org/download/$PinnedVersion/zig-x86_64-windows-$PinnedVersion.zip"
$Pin = "$PinnedVersion $PinnedSha256"

if ($PrintPin) {
    $Pin
    return
}

$marker = Join-Path $Destination '.pinned-version'
$zig = Join-Path $Destination 'zig.exe'

# An empty marker reads as $null, left by a run stopped while writing it: no install, then.
if ((Test-Path -LiteralPath $zig) -and (Test-Path -LiteralPath $marker) -and
    ("$(Get-Content -LiteralPath $marker -Raw)".Trim() -eq $Pin)) {
    Write-Host "zig $PinnedVersion is already in $Destination"
    exit 0
}

$downloaded = $false
if ($Archive -eq '') {
    $Archive = Join-Path ([System.IO.Path]::GetTempPath()) "zig-$PinnedVersion-$([guid]::NewGuid().ToString('N')).zip"
    Write-Host "Downloading $PinnedUrl"
    # Without TLS 1.2 named, Windows PowerShell 5.1 may offer only older protocols.
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $PinnedUrl -OutFile $Archive -UseBasicParsing
    $downloaded = $true
}

$unpackTo = $null
try {
    $actual = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $PinnedSha256) {
        Write-Host "checksum mismatch: expected $PinnedSha256, got $actual"
        exit 1
    }

    $unpackTo = Join-Path ([System.IO.Path]::GetTempPath()) ("zig-unpack-" + [guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $Archive -DestinationPath $unpackTo
    $top = Get-ChildItem -LiteralPath $unpackTo -Directory | Select-Object -First 1
    if ($null -eq $top -or -not (Test-Path -LiteralPath (Join-Path $top.FullName 'zig.exe'))) {
        Write-Host "the archive holds no zig.exe under a single top folder"
        exit 1
    }

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Move-Item -LiteralPath $top.FullName -Destination $Destination
    Set-Content -LiteralPath $marker -Value $Pin -Encoding ASCII
    Write-Host "zig $PinnedVersion is in $Destination"
    exit 0
}
finally {
    if ($null -ne $unpackTo -and (Test-Path -LiteralPath $unpackTo)) { Remove-Item -LiteralPath $unpackTo -Recurse -Force }
    if ($downloaded -and (Test-Path -LiteralPath $Archive)) { Remove-Item -LiteralPath $Archive -Force }
}
