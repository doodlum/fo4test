<#
.SYNOPSIS
    Runs the fo4test previs harness end to end and reports the result.

.DESCRIPTION
    Deploys the built plugin (unless -NoDeploy), clears the previous run's
    output, launches the game through f4se_loader.exe, waits for the plugin to
    write result.json, then diffs the two captures.

    The plugin quits the game itself when QuitWhenDone=1 (the default); this
    script force-closes it only if the timeout expires.

.EXAMPLE
    pwsh -File tools/run_harness.ps1
#>
[CmdletBinding()]
param(
    # Fallout 4 install directory.
    [string] $GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Fallout 4',

    # Where the plugin writes its captures.  Must match OutputDir in the INI.
    [string] $OutputDir = (Join-Path $HOME 'Documents\My Games\Fallout4\F4SE\fo4test'),

    # Build config directory under build/windows/x64 to deploy from.
    [string] $Config = 'releasedbg',

    # How long to wait for result.json before giving up.
    [int] $TimeoutMinutes = 15,

    # Skip copying the freshly-built DLL/INI into the game folder.
    [switch] $NoDeploy
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Write-Step($message) { Write-Host "==> $message" -ForegroundColor Cyan }

$loader = Join-Path $GamePath 'f4se_loader.exe'
if (-not (Test-Path -LiteralPath $loader)) {
    throw "f4se_loader.exe not found at $loader -- install F4SE for this runtime first."
}

if (-not $NoDeploy) {
    Write-Step 'Deploying plugin'
    $dll = Join-Path $repo "build\windows\x64\$Config\fo4test.dll"
    if (-not (Test-Path -LiteralPath $dll)) {
        throw "fo4test.dll not found at $dll -- run 'xmake build' first (or pass -NoDeploy)."
    }

    $pluginDir = Join-Path $GamePath 'Data\F4SE\Plugins'
    New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
    Copy-Item -LiteralPath $dll -Destination $pluginDir -Force

    $ini = Join-Path $repo 'package\F4SE\Plugins\fo4test.ini'
    if ((Test-Path -LiteralPath $ini) -and
        -not (Test-Path -LiteralPath (Join-Path $pluginDir 'fo4test.ini'))) {
        # Never clobber an INI the operator has already tuned.
        Copy-Item -LiteralPath $ini -Destination $pluginDir
    }
}

Write-Step "Clearing previous run in $OutputDir"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Get-ChildItem -LiteralPath $OutputDir -Include '*.bmp', '*.json' -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
$resultPath = Join-Path $OutputDir 'result.json'

# F4SE's address-library check opens the RELATIVE path
# "Data\F4SE\Plugins\version-<major>-<minor>-<build>-0.bin", and f4se_loader
# calls CreateProcess with a null lpCurrentDirectory, so Fallout4.exe inherits
# whatever CWD the loader was started with.  Launch it from anywhere but the
# game folder and F4SE cannot find the .bin, so it disables every
# address-library plugin with "address library needs to be updated" -- which
# looks exactly like the plugin being out of date.  -WorkingDirectory is what
# keeps that from happening; do not drop it.
$versionBin = Join-Path $GamePath 'Data\F4SE\Plugins\version-1-11-240-0.bin'
if (-not (Test-Path -LiteralPath $versionBin)) {
    throw "$versionBin is missing -- install Address Library for F4SE Plugins (Nexus 47327). Without it F4SE disables the plugin."
}

# Vsync pins every configuration to the refresh rate, which would make the
# fps figures identical and useless -- and fps is how we tell a real fix from
# one that quietly disabled previs.  Turn it off for the run and put the
# operator's setting back afterwards, whatever happens.
$prefs = Join-Path $HOME 'Documents\My Games\Fallout4\Fallout4Prefs.ini'
$prefsBackup = $null
if (Test-Path -LiteralPath $prefs) {
    $prefsBackup = "$prefs.harness-backup"
    Copy-Item -LiteralPath $prefs -Destination $prefsBackup -Force
    (Get-Content -LiteralPath $prefs) `
        -replace '^iPresentInterval\s*=.*$', 'iPresentInterval=0' |
        Set-Content -LiteralPath $prefs -Encoding utf8
    Write-Step 'Disabled vsync for this run (original saved)'
}

Write-Step 'Launching Fallout 4 through F4SE'
Start-Process -FilePath $loader -WorkingDirectory $GamePath | Out-Null

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
Write-Step "Waiting for $resultPath (up to $TimeoutMinutes min)"
while (-not (Test-Path -LiteralPath $resultPath)) {
    if ((Get-Date) -gt $deadline) {
        Write-Warning 'Timed out; closing the game.'
        Get-Process -Name 'Fallout4' -ErrorAction SilentlyContinue | Stop-Process -Force
        throw "The harness never wrote $resultPath. Check Documents\My Games\Fallout4\F4SE\fo4test.log."
    }
    Start-Sleep -Seconds 5
}

Write-Step 'Harness reported in; waiting for the game to exit'
$exitDeadline = (Get-Date).AddMinutes(2)
while (Get-Process -Name 'Fallout4' -ErrorAction SilentlyContinue) {
    if ((Get-Date) -gt $exitDeadline) {
        Get-Process -Name 'Fallout4' -ErrorAction SilentlyContinue | Stop-Process -Force
        break
    }
    Start-Sleep -Seconds 2
}

if ($prefsBackup -and (Test-Path -LiteralPath $prefsBackup)) {
    Move-Item -LiteralPath $prefsBackup -Destination $prefs -Force
    Write-Step 'Restored the original vsync setting'
}

Write-Step 'Comparing captures'
& python (Join-Path $PSScriptRoot 'compare_captures.py') $OutputDir
exit $LASTEXITCODE
