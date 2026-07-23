[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('PreNG', 'PostNG', 'PostAE')]
    [string]$Variant
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot "build\clangd\$Variant"
$compileCommands = Join-Path $buildDirectory 'compile_commands.json'
$activeDatabase = Join-Path $projectRoot 'compile_commands.json'
$temporaryDatabase = "$activeDatabase.tmp"
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found at '$cmake'."
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio Developer Command Prompt was not found at '$vsDevCmd'."
}

$runtimeFlags = switch ($Variant) {
    'PreNG'  { '-DBUILD_PRE_NG=ON -DBUILD_POST_NG=OFF -DBUILD_POST_AE=OFF' }
    'PostNG' { '-DBUILD_PRE_NG=OFF -DBUILD_POST_NG=ON -DBUILD_POST_AE=OFF' }
    'PostAE' { '-DBUILD_PRE_NG=OFF -DBUILD_POST_NG=OFF -DBUILD_POST_AE=ON' }
}

$configureArguments = @(
    "-S `"$projectRoot`"",
    "-B `"$buildDirectory`"",
    '-G Ninja',
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    "-DCMAKE_TOOLCHAIN_FILE=`"$projectRoot\cmake\Fo4csMsvcVcpkgToolchain.cmake`"",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md',
    '-DCOMMUNITY_SHADERS=ON',
    '-DAIO=ON',
    '-DF4SE_SUPPORT_XBYAK=ON',
    '-DFRAMEGEN=ON',
    '-DREFLEX=ON',
    '-DUPSCALER=ON',
    '-DOVERLAY=ON',
    $runtimeFlags
) -join ' '

Write-Host "Configuring clangd analysis tree: $buildDirectory"
& cmd.exe /d /s /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && `"$cmake`" $configureArguments"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration for clangd $Variant failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $compileCommands)) {
    throw "CMake completed without generating '$compileCommands'."
}

Copy-Item -LiteralPath $compileCommands -Destination $temporaryDatabase -Force
Move-Item -LiteralPath $temporaryDatabase -Destination $activeDatabase -Force

Write-Host "Activated clangd database: $Variant"
Write-Host "Restart clangd from VS Code ('clangd: Restart language server') to reload the new compile command database."
