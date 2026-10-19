#Requires -Version 5.1
# Tests for Test-NoImGuiButton.ps1. Plain PowerShell, no external modules.
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

# A source tree with the helper, which may call ImGui::Button, and one file drawing its buttons through it.
function New-Fixture {
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("buttoncheck-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $root 'ImGui') | Out-Null
    Write-Utf8NoBom -Path (Join-Path $root 'ImGui\TextButton.cpp') -Text @'
bool TextButton(const char* label)
{
	return ImGui::Button(label);
}
'@
    Write-Utf8NoBom -Path (Join-Path $root 'Panel.cpp') -Text @'
void Draw()
{
	if (Core::TextButton("Search")) {}
	if (Core::SmallTextButton("Reset")) {}
	ImGui::InvisibleButton("##hit", size);
	ImGui::ButtonBehavior(rect, id, &hovered, &held);
}
'@
    return $root
}

$scriptUnderTest = Join-Path $PSScriptRoot '..\Test-NoImGuiButton.ps1'

function Invoke-Lint {
    param([string] $Root)
    $output = & $scriptUnderTest -SourceDirectory $Root -Helper (Join-Path $Root 'ImGui\TextButton.cpp') 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

Write-Host 'Test-NoImGuiButton.ps1'

# --- every button goes through the helper --------------------------------------
$fixture = New-Fixture
try {
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'passes when buttons go through Core::TextButton and the helper alone calls ImGui::Button' -Condition ($result.ExitCode -eq 0)
    Assert-That -Because 'does not take an invisible button or ButtonBehavior for a text button' -Condition ($result.Output -notmatch 'Panel\.cpp')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a planted ImGui::Button ----------------------------------------------------
$fixture = New-Fixture
try {
    Add-Content -LiteralPath (Join-Path $fixture 'Panel.cpp') -Value 'bool clicked = ImGui::Button("Close");'
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'fails on ImGui::Button outside the helper' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'names the file and the line' -Condition ($result.Output -match 'Panel\.cpp\(\d+\): ImGui::Button')
}
finally { Remove-Item $fixture -Recurse -Force }

# --- a planted ImGui::SmallButton ----------------------------------------------
$fixture = New-Fixture
try {
    Add-Content -LiteralPath (Join-Path $fixture 'Panel.cpp') -Value 'bool clicked = ImGui::SmallButton ("Close");'
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'fails on ImGui::SmallButton, a space before its parenthesis included' -Condition ($result.ExitCode -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

# --- another file named like the helper ----------------------------------------
$fixture = New-Fixture
try {
    New-Item -ItemType Directory -Path (Join-Path $fixture 'Elsewhere') | Out-Null
    Write-Utf8NoBom -Path (Join-Path $fixture 'Elsewhere\TextButton.cpp') -Text 'bool Other() { return ImGui::Button("Other"); }'
    $result = Invoke-Lint -Root $fixture
    Assert-That -Because 'checks a file that only shares the helper''s name' -Condition ($result.ExitCode -eq 1)
    Assert-That -Because 'reports that file, not the helper' -Condition (($result.Output -match 'Elsewhere\\TextButton\.cpp') -and ($result.Output -notmatch 'ImGui\\TextButton\.cpp'))
}
finally { Remove-Item $fixture -Recurse -Force }

# --- no source directory -------------------------------------------------------
$fixture = New-Fixture
try {
    $result = & $scriptUnderTest -SourceDirectory (Join-Path $fixture 'missing') -Helper (Join-Path $fixture 'ImGui\TextButton.cpp') 2>&1 | Out-String
    Assert-That -Because 'fails when a source directory does not exist' -Condition ($LASTEXITCODE -eq 1)
}
finally { Remove-Item $fixture -Recurse -Force }

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) failure(s)."
    exit 1
}
Write-Host 'All passed.'
exit 0
