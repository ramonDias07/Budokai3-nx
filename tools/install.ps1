# Budokai3-nx one-click installer for Windows. Started by install.bat in the repository root.
# It checks (and offers to install) the build tools, asks for your own USA/Japan Xbox 360 ISO, builds everything and
# leaves the SD card folder in out\sd\switch\B3deko. Safe to run again: finished steps are skipped.
#
# Optional parameters (for people who already have the tools somewhere else):
#   -Iso <file.iso>       skip the file dialog
#   -DevkitPro <folder>   devkitPro folder (default: DEVKITPRO variable or C:\devkitPro)
#   -ToolPath <folders>   extra folders put first on PATH (git, python, cmake, ninja, clang)
#   -DxcRoot <folder>     an extracted DXC release (inc\, lib\x64\, bin\x64\)
#   -LlvmMingw <folder>   an extracted LLVM-MinGW (with bin\clang.exe)
#   -NoInstall            never install anything; only report what is missing
#   -Jobs <n>             parallel compile jobs
#   -NoTranslation        do not add the PT-BR translation pack
#   -PtbrPack <file>      use a local ptbr.b3tr instead of downloading it
param(
    [string]$Iso = '',
    [string]$DevkitPro = '',
    [string[]]$ToolPath = @(),
    [string]$DxcRoot = '',
    [string]$LlvmMingw = '',
    [switch]$NoInstall,
    [int]$Jobs = 0,
    [switch]$NoTranslation,
    [string]$PtbrPack = ''
)
$ErrorActionPreference = 'Stop'
# powershell -File passes "a,b" as one string: accept commas and semicolons as separators too.
$ToolPath = @($ToolPath | ForEach-Object { $_ -split '[;,]' } | Where-Object { $_ })
$Root = Split-Path $PSScriptRoot -Parent
$Downloads = Join-Path $Root 'downloads'
$Log = Join-Path $Root 'out\install.log'
New-Item -ItemType Directory -Force (Join-Path $Root 'out'), $Downloads | Out-Null

# Pinned tool downloads (the versions the published builds were made with).
$LlvmZip = @{ Name = 'llvm-mingw-20261020-ucrt-x86_64'
    Url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip'
    Sha256 = 'FD5462B7F5D93B2FF981FCD3F9C7E427E47F3203C64EA91C27BCC444EEBD74CD' }
$DxcZip = @{ Name = 'dxc_2024_07_31'
    Url = 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip'
    Sha256 = 'E2627F004F0F9424D8C71EA1314D04F38C5A5096884AE9217F1F18BD320267B5' }
# Brazilian Portuguese fan translation by TheRalf, converted for this port (release asset).
$PtbrDownload = @{ Url = 'https://github.com/ramonDias07/Budokai3-nx/releases/download/v0.1.56/ptbr.b3tr'
    Sha256 = 'F898E83FC8E35A133A3E00C8F0613BD26F9BDC4B34A323952677C69CD05F1477' }
# winget packages for the common tools
$Winget = [ordered]@{ git = 'Git.Git'; python = 'Python.Python.3.12'; cmake = 'Kitware.CMake'; ninja = 'Ninja-build.Ninja' }

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Step($text) { Write-Host ''; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) {
    Write-Host ''
    Write-Host "PROBLEM: $text" -ForegroundColor Red
    Write-Host "Details of the last command are in $Log" -ForegroundColor Red
    Write-Host 'If you need help, open an issue at https://github.com/ramonDias07/Budokai3-nx/issues and attach that file.' -ForegroundColor Yellow
    exit 1
}
function Ask($question) {
    $a = Read-Host "$question [Y/n]"
    return ($a -eq '' -or $a -match '^[yYsS]')
}
function Refresh-Path {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = (($ToolPath + @("$env:LOCALAPPDATA\Microsoft\WinGet\Links", $machine, $user)) -join ';')
}
function Run($what, [string]$exe, [string[]]$arguments) {
    # Runs a program, appending its output to the log, and stops with a clear message if it fails.
    Say "    $what..."
    "`r`n===== $what`r`n> $exe $($arguments -join ' ')" | Out-File -Append -Encoding utf8 $Log
    $ErrorActionPreference = 'Continue'
    & $exe @arguments 2>&1 | ForEach-Object { "$_" } | Out-File -Append -Encoding utf8 $Log
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { Get-Content $Log -Tail 15 | ForEach-Object { Say "    | $_" DarkGray }; Fail "$what failed (exit code $code)." }
}
function Find-Exe($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*\WindowsApps\*' } | Select-Object -First 1
    if ($c) { return $c.Source }
    return $null
}
function Get-Zip($info) {
    $folder = Join-Path $Downloads $info.Name
    if (Test-Path (Join-Path $folder '.ok')) { return $folder }
    $zip = "$folder.zip"
    Say "    downloading $($info.Name) (this can take a few minutes)..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $info.Url -OutFile $zip -UseBasicParsing
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash
    if ($hash -ne $info.Sha256) { Remove-Item $zip -Force; Fail "The download of $($info.Name) is damaged (SHA-256 $hash). Run install.bat again." }
    if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
    New-Item -ItemType Directory -Force $folder | Out-Null
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (Test-Path $tar) { & $tar -xf $zip -C $folder; if ($LASTEXITCODE -ne 0) { Fail "Could not unpack $zip." } }
    else { Expand-Archive -Path $zip -DestinationPath $folder }
    Remove-Item $zip -Force
    # Some zips have a single top folder: use what is inside it.
    $inner = Get-ChildItem $folder
    if ($inner.Count -eq 1 -and $inner[0].PSIsContainer) {
        Get-ChildItem $inner[0].FullName | Move-Item -Destination $folder
        Remove-Item $inner[0].FullName
    }
    New-Item -ItemType File (Join-Path $folder '.ok') | Out-Null
    return $folder
}

"Budokai3-nx installer log, $(Get-Date -Format s)" | Out-File -Encoding utf8 $Log
Say 'Budokai3-nx installer' White
Say 'This builds Dragon Ball Z: Budokai 3 HD for your Switch from YOUR OWN Xbox 360 disc dump.'
Say 'The first run downloads the tools and takes a long time (often 1 to 2 hours). Keep this window open.'

# ---------------------------------------------------------------- 1. tools
Step '1/7 Checking the tools'
Refresh-Path
$free = (Get-PSDrive -Name ($Root.Substring(0, 1))).Free / 1GB
if ($free -lt 20) { Fail ("Only {0:N0} GB free on drive {1}:. About 20 GB are needed." -f $free, $Root.Substring(0, 1)) }

$missing = @()
foreach ($t in $Winget.Keys) { if (-not (Find-Exe $t)) { $missing += $t } }
if ($missing) {
    Say "    missing: $($missing -join ', ')" Yellow
    if ($NoInstall) { Fail "Install these programs first: $($missing -join ', ')." }
    if (-not (Find-Exe 'winget')) { Fail "Install these programs first (winget is not available): $($missing -join ', ')." }
    if (-not (Ask "    Install them now with winget?")) { Fail "Install these programs first: $($missing -join ', ')." }
    foreach ($t in $missing) {
        Run "installing $t" 'winget' @('install', '--id', $Winget[$t], '--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements')
    }
    Refresh-Path
    $still = @(); foreach ($t in $missing) { if (-not (Find-Exe $t)) { $still += $t } }
    if ($still) { Fail "Installed, but Windows has not picked them up yet: close this window and run install.bat again." }
}
$python = Find-Exe 'python'
$pyVersion = & $python -c "import sys; print(sys.version_info[0]*100+sys.version_info[1])"
if ([int]$pyVersion -lt 311) { Fail "Python 3.11 or newer is needed ($python is older)." }
& $python -c "import xxhash" 2>$null
if ($LASTEXITCODE -ne 0) { Run 'installing the Python module xxhash' $python @('-m', 'pip', 'install', '--user', 'xxhash') }

if (-not $LlvmMingw) { $LlvmMingw = Get-Zip $LlvmZip }
if (-not (Test-Path "$LlvmMingw\bin\clang.exe")) { Fail "clang.exe not found in $LlvmMingw\bin." }
if (-not $DxcRoot) { $DxcRoot = Get-Zip $DxcZip }
if (-not (Test-Path "$DxcRoot\inc\dxcapi.h")) { Fail "The DXC folder $DxcRoot does not have inc\dxcapi.h." }
$env:Path = "$LlvmMingw\bin;$env:Path"

if (-not $DevkitPro) {
    if ($env:DEVKITPRO -and $env:DEVKITPRO -notlike '/opt/*') { $DevkitPro = $env:DEVKITPRO } else { $DevkitPro = 'C:\devkitPro' }
}
$needed = @('devkitA64\bin\aarch64-none-elf-gcc.exe', 'libnx\lib\libnx.a', 'libnx\lib\libdeko3d.a', 'tools\bin\elf2nro.exe', 'portlibs\switch\lib\libzstd.a')
$lack = @($needed | Where-Object { -not (Test-Path (Join-Path $DevkitPro $_)) })
if ($lack) {
    $pacman = Join-Path $DevkitPro 'msys2\usr\bin\bash.exe'
    if ((Test-Path $pacman) -and -not $NoInstall -and (Ask "    devkitPro is missing Switch packages. Install switch-dev and switch-zstd now?")) {
        Run 'installing devkitPro Switch packages' $pacman @('-lc', 'pacman -Sy --needed --noconfirm switch-dev switch-zstd')
        $lack = @($needed | Where-Object { -not (Test-Path (Join-Path $DevkitPro $_)) })
    }
    if ($lack) {
        Say ''
        Say 'devkitPro (the Switch development kit) is needed and was not found or is incomplete.' Yellow
        Say '  1. Download the installer: https://github.com/devkitPro/installer/releases (devkitProUpdater-*.exe)' Yellow
        Say '  2. Run it and tick "Switch Development". Keep the folder C:\devkitPro.' Yellow
        Say '  3. Run install.bat again.' Yellow
        Start-Process 'https://github.com/devkitPro/installer/releases'
        Fail "devkitPro missing: $($lack -join ', ')"
    }
}
Say '    all tools found' Green
if ($Jobs -le 0) { $Jobs = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, 8)) }

# ---------------------------------------------------------------- 2. the disc
Step '2/7 Choosing your disc image'
if (-not $Iso) {
    Say '    Select the .iso of "Dragon Ball Z: Budokai HD Collection" (Xbox 360, USA/Japan) in the window that opens.'
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = 'Budokai HD Collection (Xbox 360, USA/Japan) .iso'
        $dialog.Filter = 'Xbox 360 disc image (*.iso)|*.iso|All files (*.*)|*.*'
        if ($dialog.ShowDialog() -eq 'OK') { $Iso = $dialog.FileName }
    } catch { }
    if (-not $Iso) { $Iso = (Read-Host '    Path of the .iso').Trim('"') }
}
if (-not (Test-Path $Iso)) { Fail "File not found: $Iso" }
Say "    $Iso"

# ---------------------------------------------------------------- 3. SDK
Step '3/7 Downloading and preparing the ReXGlue SDK (first time only)'
Push-Location $Root
try {
    if (-not (Test-Path 'sdk')) { Run 'preparing the SDK (downloads about 1 GB)' $python @('tools/prepare_sdk.py') }
    else { Say '    already prepared' }

    # ---------------------------------------------------------------- 4. host tools
    Step '4/7 Building the PC tools (first time only)'
    $cc = @("-DCMAKE_C_COMPILER=$LlvmMingw\bin\clang.exe", "-DCMAKE_CXX_COMPILER=$LlvmMingw\bin\clang++.exe")
    $rexglue = 'sdk\out\win-amd64\rexglue.exe'
    if (-not (Test-Path $rexglue)) {
        Run 'configuring rexglue' 'cmake' (@('-S', 'sdk', '-B', 'out/sdk-host', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release') + $cc +
            @('-DREXGLUE_ENABLE_TRACY=OFF', '-DREXGLUE_USE_D3D12=OFF', '-DREXGLUE_USE_VULKAN=ON', "-DPYTHON_EXECUTABLE=$python"))
        Run 'building rexglue' 'cmake' @('--build', 'out/sdk-host', '--target', 'rexglue', '--parallel', "$Jobs")
    }
    if (-not (Test-Path 'out\xenos\XenosRecomp.exe')) {
        Run 'configuring XenosRecomp' 'cmake' (@('-S', 'tools/xenos', '-B', 'out/xenos', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release') + $cc + @("-DDXC_ROOT=$DxcRoot"))
        Run 'building XenosRecomp' 'cmake' @('--build', 'out/xenos', '--target', 'XenosRecomp', '--parallel', "$Jobs")
    }
    if (-not (Test-Path 'out\spirv-cross\spirv-cross.exe')) {
        Run 'configuring SPIRV-Cross' 'cmake' (@('-S', 'sdk/thirdparty/SPIRV-Cross', '-B', 'out/spirv-cross', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DSPIRV_CROSS_ENABLE_TESTS=OFF') + $cc)
        Run 'building SPIRV-Cross' 'cmake' @('--build', 'out/spirv-cross', '--target', 'spirv-cross', '--parallel', "$Jobs")
    }
    if (-not (Test-Path 'out\uam\uam.exe')) {
        Run 'configuring uam' 'cmake' (@('-S', 'tools/vendor/uam', '-B', 'out/uam', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release') + $cc)
        Run 'building uam' 'cmake' @('--build', 'out/uam', '--target', 'uam_tool', '--parallel', "$Jobs")
    }
    Say '    PC tools ready' Green

    # ---------------------------------------------------------------- 5. the game
    Step '5/7 Building the game for the Switch (the long part)'
    Say '    Extracting your disc, recompiling the game, converting the shaders and compiling the NRO.'
    Run 'building B3.nro' $python @('tools/build.py', $Iso, '--rexglue', $rexglue, '--xenosrecomp', 'out/xenos/XenosRecomp.exe',
        '--spirv-cross', 'out/spirv-cross/spirv-cross.exe', '--uam', 'out/uam/uam.exe', '--devkitpro', $DevkitPro, '--jobs', "$Jobs")
} finally { Pop-Location }

$package = Join-Path $Root 'out\sd\switch\B3deko'
if (-not (Test-Path (Join-Path $package 'B3.nro'))) { Fail 'The build finished but B3.nro was not found.' }

# ---------------------------------------------------------------- 6. PT-BR translation
if (-not $NoTranslation) {
    Step '6/7 Adding the Brazilian Portuguese translation (by TheRalf)'
    $ptbrDir = Join-Path $package 'traducao'
    $ptbrFile = Join-Path $ptbrDir 'ptbr.b3tr'
    New-Item -ItemType Directory -Force $ptbrDir | Out-Null
    $have = (Test-Path $ptbrFile) -and ((Get-FileHash $ptbrFile -Algorithm SHA256).Hash -eq $PtbrDownload.Sha256)
    if (-not $have) {
        $cached = Join-Path $Downloads 'ptbr.b3tr'
        if ($PtbrPack) { Copy-Item $PtbrPack $cached -Force }
        elseif (-not ((Test-Path $cached) -and ((Get-FileHash $cached -Algorithm SHA256).Hash -eq $PtbrDownload.Sha256))) {
            Say '    downloading the translation pack (about 80 MB)...'
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            $ProgressPreference = 'SilentlyContinue'
            try { Invoke-WebRequest -Uri $PtbrDownload.Url -OutFile $cached -UseBasicParsing }
            catch { Say "    could not download the translation ($($_.Exception.Message)). The game works in English; run install.bat again later to add it." Yellow }
        }
        if ((Test-Path $cached) -and ((Get-FileHash $cached -Algorithm SHA256).Hash -eq $PtbrDownload.Sha256)) {
            Copy-Item $cached $ptbrFile -Force
        } elseif (Test-Path $cached) {
            Remove-Item $cached -Force
            Say '    the translation download is damaged; run install.bat again to retry. The game works in English.' Yellow
        }
    }
    if (Test-Path $ptbrFile) {
        Say '    translation added. On the Switch: quick menu (+ and - together) > LANGUAGE > PORTUGUESE (BR), then SAVE AND CLOSE and restart the game.' Green
        Say '    Translation: TheRalf (PS2 fan translation). All credit for it goes to him.'
    }
}

# ---------------------------------------------------------------- 7. done
Step '7/7 Done!'
Say "    Your Switch folder is ready: $package" Green
Say '    Copy the whole B3deko folder into the "switch" folder of your SD card (SD:\switch\B3deko).'
Say '    On the Switch, open the Homebrew Menu by holding R while starting any game, then pick B3.nro.'
Say '    When updating later, keep the budokai3 folder inside B3deko: your saves are there.'
Start-Process explorer.exe (Join-Path $Root 'out\sd\switch')
