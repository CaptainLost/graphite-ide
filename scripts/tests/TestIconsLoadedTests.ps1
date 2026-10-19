#Requires -Version 5.1
# Tests for Test-IconsLoaded.ps1. Plain PowerShell, no external modules.
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

# A source tree with an icon list naming two Lucide icons and one file drawing them.
function New-Fixture {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("iconcheck-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null
    Write-Utf8NoBom -Path (Join-Path $root 'Icons.cpp') -Text @'
namespace {
	constexpr const char* kUsedIcons[] = {
		ICON_LC_X,
		ICON_LC_FILE,
	};
}
'@
    Write-Utf8NoBom -Path (Join-Path $root 'Panel.cpp') -Text @'
void Draw()
{
	DrawIconButton("##close", ICON_LC_X);
	DrawIcon(ICON_LC_FILE);
}
'@
    return $root
}

$scriptUnderTest = Join-Path $PSScriptRoot '..\Test-IconsLoaded.ps1'

function Invoke-Lint {
    param([string] $Root)
    $output = & $scriptUnderTest -SourceDirectory $Root -IconList (Join-Path $Root 'Icons.cpp') 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

Write-Host 'Test-IconsLoaded.ps1'

# --- every icon drawn is listed -----------------------------------------------
$fixture = New-Fixture
try {
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'passes when every Lucide icon drawn is in kUsedIcons' -Condition ($result.ExitCode -eq 0)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a Lucide icon missing from the list --------------------------------------
$fixture = New-Fixture
try {
    Add-Content -LiteralPath (Join-Path $fixture 'Panel.cpp') -Value 'const char* kGear = ICON_LC_SETTINGS;'
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'fails when a Lucide icon drawn is missing from kUsedIcons' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'names the missing icon' -Condition ($result.Output -match 'ICON_LC_SETTINGS')
    Assert-That -Because 'does not report the icons that are listed' -Condition ($result.Output -notmatch 'ICON_LC_FILE\b')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a planted Material Design icon -------------------------------------------
$fixture = New-Fixture
try {
    Add-Content -LiteralPath (Join-Path $fixture 'Panel.cpp') -Value 'const char* kOld = ICON_MDI_CLOSE;'
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'fails on a Material Design icon in a source file' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'names the Material Design icon' -Condition ($result.Output -match 'ICON_MDI_CLOSE')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a Material Design icon in the list itself --------------------------------
$fixture = New-Fixture
try {
    $list = Join-Path $fixture 'Icons.cpp'
    $text = [System.IO.File]::ReadAllText($list).Replace('ICON_LC_FILE,', "ICON_LC_FILE,`n`t`tICON_MDI_CLOSE,")
    Write-Utf8NoBom -Path $list -Text $text
    Add-Content -LiteralPath (Join-Path $fixture 'Panel.cpp') -Value 'const char* kOld = ICON_MDI_CLOSE;'
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'fails on a Material Design icon even when kUsedIcons lists it' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- no icon list -------------------------------------------------------------
$fixture = New-Fixture
try {
    Remove-Item -LiteralPath (Join-Path $fixture 'Icons.cpp')
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'fails when the icon list does not exist' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)."
    exit 1
}
Write-Host 'All passed.'
exit 0
