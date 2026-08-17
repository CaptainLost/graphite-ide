# Writes compile_commands.json: the compilation database clangd reads to know how this
# repository is built, and - just as importantly - which files it contains.
#
# The file list is the point. clangd does no background indexing at all from a compile_flags.txt,
# because that file carries flags and nothing else: with no list to walk, the server knows only
# the files that have been opened, so a symbol's references stop at the current tab and a count
# over a declaration reads far too low. A compilation database names every file, so the server
# indexes the project and answers about all of it.
#
# Generated rather than committed, for the same reason the project files are: premake globs its
# sources, so any list written by hand goes stale the first time someone adds a file.

[CmdletBinding()]
param(
    [string] $Root
)

$ErrorActionPreference = 'Stop'

# Not a default on the parameter: $PSScriptRoot is not yet populated while the param block is
# being bound, so a default computed there binds an empty string.
if (-not $Root)
{
    $Root = Split-Path -Parent $PSScriptRoot
}

# Kept in step with premake by hand. They are the same flags for every file here - one language
# standard, one platform, one set of include directories - which is why a database this simple
# is honest rather than a shortcut.
$flags = @(
    '-std=c++20'
    '-xc++'
    '--target=x86_64-pc-windows-msvc'
    '-fms-compatibility'
    '-fms-extensions'
    '-DCORE_PLATFORM_WINDOWS'
    '-DCORE_DEBUG'
    '-DNOMINMAX'
    '-DWIN32_LEAN_AND_MEAN'
    '-DIMGUI_USE_WCHAR32'
    '-ICore/src'
    '-IGraphite/src'
    '-IGraphite/tests/src'
    '-Ivendor/imgui'
    '-Ivendor/glfw/include'
    '-Ivendor/fonts'
    '-Ivendor/json/single_include'
    '-Ivendor/spdlog/include'
    '-Ivendor/pugixml/src'
    '-Ivendor/libgit2/include'
    '-Ivendor/doctest'
    '-Wno-unknown-pragmas'
)

# Only this repository's own sources. The vendored dependencies are built by premake and are of
# no interest to a reader navigating this code; indexing them would cost minutes and megabytes
# to answer questions nobody asks.
$sourceRoots = @(
    'Core/src'
    'Core/tests/src'
    'Graphite/src'
    'Graphite/tests/src'
)

$entries = New-Object System.Collections.ArrayList

foreach ($relative in $sourceRoots)
{
    $directory = Join-Path $Root $relative

    if (-not (Test-Path $directory))
    {
        Write-Warning "Skipping $relative - no such directory"
        continue
    }

    foreach ($file in Get-ChildItem -Path $directory -Recurse -Filter *.cpp -File)
    {
        $arguments = @('clang++') + $flags + @($file.FullName)

        $null = $entries.Add([ordered]@{
            directory = $Root
            file      = $file.FullName
            arguments = $arguments
        })
    }
}

$output = Join-Path $Root 'compile_commands.json'

# -Depth because PowerShell 5.1 flattens anything nested more than two levels deep by default,
# which would turn every argument list into the string "System.Object[]".
$entries | ConvertTo-Json -Depth 4 | Set-Content -Path $output -Encoding utf8

Write-Output "Wrote $output ($($entries.Count) files)"

# compile_flags.txt wins over compile_commands.json wherever both exist, so leaving one behind
# would silently undo everything above.
$stale = Join-Path $Root 'compile_flags.txt'

if (Test-Path $stale)
{
    Write-Warning "compile_flags.txt still exists and takes precedence over compile_commands.json - remove it"
}
