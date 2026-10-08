# =============================================================================
#  package.ps1 -- Build a portable distribution folder for MotecDataAnalyser.
#
#  Usage (from the repo root):
#      .\scripts\package.ps1                     # Debug / Win32 -> MotecDataAnalyser_Portable\
#      .\scripts\package.ps1 -Config Release     # package a Release build instead
#      .\scripts\package.ps1 -Platform Win64     # 64-bit build (uses Redist\win64 packages)
#      .\scripts\package.ps1 -SkipData           # skip copying the ~58 MB data/ samples
#
#  Output layout:
#      MotecDataAnalyser_Portable\
#          MotecDataAnalyser.exe         (renamed from Win32\<Config>\Project1.exe)
#          *.bpl                         (dynamic RTL packages, beside the exe so it runs)
#          runtime\                      (same .bpl set; reference copy / documented layout)
#          config\MotecDataAnalyser.mapping.json        (runtime channel mapping)
#          config\MotecDataAnalyser.rules.example.json   (template for custom rules)
#          data\                         (sample telemetry + reference documents)
#          README.txt                    (short usage instructions)
# =============================================================================

param(
    [ValidateSet('Debug','Release')][string]$Config = 'Debug',
    [ValidateSet('Win32','Win64')][string]$Platform = 'Win32',
    [switch]$SkipData
)

$ErrorActionPreference = 'Stop'

# --- Resolve paths -----------------------------------------------------------
$ScriptDir  = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not (Test-Path $ScriptDir)) { throw "Cannot determine the scripts directory." }
$ProjectDir = Split-Path -Parent $ScriptDir   # this script lives in <root>\scripts\

$StudioRoot  = 'C:\Program Files (x86)\Embarcadero\Studio\23.0'
$redistSub   = if ($Platform -eq 'Win64') { 'win64' } else { 'win32' }
$RedistDir   = Join-Path $StudioRoot ("Redist\" + $redistSub)

# The dynamic RTL packages this app links against (see the ilink32 line in build logs).
$bplNames    = @('rtl290.bpl','vcl290.bpl','vclwinx290.bpl','vclimg290.bpl','bindengine290.bpl')

$exeSource   = Join-Path $ProjectDir ("Win32\" + $Config + "\Project1.exe")
$outRoot     = Join-Path $ProjectDir 'MotecDataAnalyser_Portable'

# --- Pre-flight checks -------------------------------------------------------
if (-not (Test-Path $exeSource)) {
    throw "Build output not found: $exeSource`nRun .\scripts\build.ps1 -Config $Config first."
}
if (-not (Test-Path $RedistDir)) {
    throw "Runtime package folder not found: $RedistDir"
}

# --- Fresh output ------------------------------------------------------------
Write-Host "==> Building portable package -> $outRoot" -ForegroundColor Cyan
if (Test-Path $outRoot) { Remove-Item $outRoot -Recurse -Force }
New-Item -ItemType Directory -Path $outRoot | Out-Null

$dirRuntime = Join-Path $outRoot 'runtime'
$dirConfig  = Join-Path $outRoot 'config'
$dirData    = Join-Path $outRoot 'data'
foreach ($d in @($dirRuntime, $dirConfig, $dirData)) { New-Item -ItemType Directory -Path $d | Out-Null }

# --- Executable (renamed) ----------------------------------------------------
Copy-Item $exeSource (Join-Path $outRoot 'MotecDataAnalyser.exe') -Force
Write-Host "    exe     -> MotecDataAnalyser.exe" -ForegroundColor Green

# --- Runtime packages (.bpl): beside the exe AND in runtime\ ------------------
foreach ($n in $bplNames) {
    $src = Join-Path $RedistDir $n
    if (-not (Test-Path $src)) { throw "Missing runtime package: $src" }
    Copy-Item $src (Join-Path $outRoot $n) -Force          # beside exe -> loads on double-click
    Copy-Item $src (Join-Path $dirRuntime $n) -Force       # documented layout / reference copy
}
$bplMb = ((Get-ChildItem $dirRuntime | Measure-Object Length -Sum).Sum) / 1MB
Write-Host ("    runtime: {0} .bpl packages ({1:N1} MB)" -f $bplNames.Count, $bplMb) -ForegroundColor Green

# --- Config (mapping + rules template + any existing INIs next to the exe) ---
$example = Join-Path $ProjectDir 'MotecDataAnalyser.rules.example.json'
if (Test-Path $example) { Copy-Item $example $dirConfig -Force }
$mapping = Join-Path $ProjectDir 'MotecDataAnalyser.mapping.json'
if (-not (Test-Path $mapping)) { throw "Required channel mapping file not found: $mapping" }
Copy-Item $mapping $dirConfig -Force
foreach ($ini in @('MotecDataAnalyser.ini','MotecDataAnalyser.thresholds.ini')) {
    $p = Join-Path (Split-Path $exeSource -Parent) $ini
    if (Test-Path $p) { Copy-Item $p $dirConfig -Force }
}
Write-Host "    config  : channel mapping + rules template + existing INIs" -ForegroundColor Green

# --- Data samples ------------------------------------------------------------
if ($SkipData) {
    Write-Host "    data    : SKIPPED (-SkipData)" -ForegroundColor Yellow
} else {
    $dataSrc = Join-Path $ProjectDir 'data'
    # Copy everything except the working/ scratch folder (dev artifacts).
    Get-ChildItem $dataSrc | Where-Object { $_.Name -ne 'working' } | ForEach-Object {
        if ($_.PSIsContainer) {
            Copy-Item $_.FullName (Join-Path $dirData $_.Name) -Recurse -Force
        } else {
            Copy-Item $_.FullName $dirData -Force
        }
    }
    $dataMb = ((Get-ChildItem $dirData -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB
    Write-Host ("    data    : copied ({0:N1} MB)" -f $dataMb) -ForegroundColor Green
}

# --- README.txt --------------------------------------------------------------
$readme = @'
MotecDataAnalyser -- Portable Edition
=====================================

To run: double-click MotecDataAnalyser.exe. No installation required.
(Windows 7 or later; x86 for Win32 builds, x64 for Win64 builds.)

Folder layout:
  MotecDataAnalyser.exe   The application.
  *.bpl                   Runtime packages (dynamic RTL). They must sit next to the
                          executable so Windows can load them without any setup; a
                          reference copy is also kept under runtime\.
    config\                 MotecDataAnalyser.mapping.json contains runtime source mappings.
                                                    MotecDataAnalyser.rules.example.json is a template: rename it to
                                                    "MotecDataAnalyser.rules.json" to customize diagnostic rules and
                                                    legacy channel aliases without recompiling the app.
  data\                   Sample telemetry (CSV / LD) and reference documents you can open
                          from the app to try it out.

Settings:
  On first run the app creates MotecDataAnalyser.ini and
  MotecDataAnalyser.thresholds.ini next to the executable. Edit them while the app is
  closed; changes take effect after a restart.

Loading data in the app:
  - CSV files: use the "Load CSV" button (MoTeC CSV export).
  - LD / LDX files: use the "Load LD" button (raw MoTeC binary capture).
'@
Set-Content -Path (Join-Path $outRoot 'README.txt') -Value $readme -Encoding ASCII

# --- Summary -----------------------------------------------------------------
$totalMb = ((Get-ChildItem $outRoot -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB
Write-Host ""
Write-Host ("DONE. Portable package ready: {0}" -f $outRoot) -ForegroundColor Green
Write-Host ("Total size: {0:N1} MB" -f $totalMb) -ForegroundColor Green
