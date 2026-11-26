<#
  Builds B3deko-Installer.exe (one file, console) with PyInstaller.
  Needs already built: the host rexglue (with the REXGLUE_DUMP_ONLY change), XenosRecomp (+dxcompiler.dll), spirv-cross, uam.
  Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_installer.ps1 [-Work <folder>]
#>
param(
    [string]$Work = 'G:\Games\Emuladores\Nintendo\NintendoSwitch\Jogos\Meuprojeto360\Dragon Ball Z Budokai HD Collection\trabalho\instalador',
    [string]$Shared = 'G:\Games\Emuladores\Nintendo\NintendoSwitch\Jogos\usogeral',
    [string]$Python = '',
    [string]$SpirvCross = '',
    [string]$Uam = ''
)
$ErrorActionPreference = 'Continue'  # native tools write warnings to stderr; exit codes are checked below
$Repo = Split-Path -Parent $PSScriptRoot
if (-not $Python)     { $Python = Join-Path $Shared 'python-3.13.16\python.exe' }
if (-not $SpirvCross) { $SpirvCross = Join-Path $Shared 'compilacao\spirv-cross-build\spirv-cross.exe' }
if (-not $Uam)        { $Uam = Join-Path $Shared 'compilacao\uam-adf3a4ee-build\uam.exe' }
$LlvmBin = Join-Path $Shared 'compilacao\llvm\llvm-mingw-20261020-ucrt-x86_64\bin'
$PyDeps = Join-Path $Work 'pydeps'
$Stage = Join-Path $Work 'stage\tools'

# 1. PyInstaller into its own folder (the shared Python is not modified)
if (-not (Test-Path (Join-Path $PyDeps 'PyInstaller'))) {
    & $Python -I -m pip install --target $PyDeps pyinstaller
    if ($LASTEXITCODE) { throw 'pip install pyinstaller failed' }
}

# 2. stage the helper programs and data the installer carries
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
foreach ($d in 'bin', 'data', 'app') { New-Item -ItemType Directory -Force (Join-Path $Stage $d) | Out-Null }
$bin = Join-Path $Stage 'bin'
$sdkOut = Join-Path $Repo 'sdk\out\win-amd64'
$xenos = Join-Path $Repo 'out\xenos'
$copies = @(
    @("$sdkOut\rexglue.exe", $bin), @("$sdkOut\librexruntime.dll", $bin),
    @("$xenos\XenosRecomp.exe", $bin), @("$xenos\dxcompiler.dll", $bin),
    @($SpirvCross, $bin), @($Uam, $bin),
    @("$LlvmBin\libc++.dll", $bin), @("$LlvmBin\libunwind.dll", $bin), @("$LlvmBin\libwinpthread-1.dll", $bin),
    @("$Repo\tools\xenos\shader_common.h", (Join-Path $Stage 'data')),
    @("$Repo\tools\shader_layout.json", (Join-Path $Stage 'data')),
    @("$Repo\app\dbz3_manifest.toml", (Join-Path $Stage 'app')),
    @("$Repo\app\dbz3_config.toml", (Join-Path $Stage 'app'))
)
foreach ($c in $copies) {
    if (-not (Test-Path $c[0])) { throw "missing: $($c[0])" }
    Copy-Item $c[0] $c[1] -Force
}

# 3. freeze
$dist = Join-Path $Work 'dist'
$pyArgs = @(
    '--onefile', '--console', '--noconfirm', '--clean', '--name', 'B3deko-Installer',
    '--paths', (Join-Path $Repo 'tools'),
    '--add-data', "$Stage;tools",
    '--hidden-import', 'convert_dksh', '--hidden-import', 'deko3d_glsl',
    '--exclude-module', 'tkinter', '--exclude-module', 'unittest', '--exclude-module', 'pydoc',
    '--distpath', $dist, '--workpath', (Join-Path $Work 'pybuild'), '--specpath', (Join-Path $Work 'pybuild'),
    (Join-Path $Repo 'tools\b3_installer.py')
)
$code = "import sys; sys.path.insert(0, r'$PyDeps'); from PyInstaller.__main__ import run; run(sys.argv[1:])"
$env:TEMP = Join-Path $Work 'tmp'; $env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
& $Python -I -c $code @pyArgs
if ($LASTEXITCODE) { throw 'PyInstaller failed' }
Get-Item (Join-Path $dist 'B3deko-Installer.exe') | Select-Object FullName, Length
exit 0
