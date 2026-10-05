# =============================================================================
#  MotecDataAnalyser - build.ps1 (Development plan, Step 3)
# -----------------------------------------------------------------------------
#  Compiles the C++ Builder 12 project (Clang / VCL, Win32 Debug) from the
#  command line. Wraps rsvars.bat + msbuild and can clean the output folder
#  before compiling.
#
#  NOTE: this file is intentionally PURE ASCII (no accents, no em-dashes).
#        PowerShell 5.1 reads .ps1 files as ANSI when there is no UTF-8 BOM;
#        non-ASCII bytes would be misdecoded and break string parsing.
#
#  Usage :
#      .\build.ps1                 # normal build (Debug / Win32), incremental
#      .\build.ps1 -Clean          # also deletes objects/exe BEFORE compiling
#      .\build.ps1 -Config Release # another configuration, if available
#
#  Prerequisite: Embarcadero RAD Studio / C++ Builder 12 installed in the
#  default folder. If it lives elsewhere, adjust $StudioRoot below.
# =============================================================================

param(
    [switch]$Clean,
    [ValidateSet('Debug','Release')]
    [string]$Config = 'Debug',
    [ValidateSet('Win32','Win64')]
    [string]$Platform = 'Win32'
)

$ErrorActionPreference = 'Stop'

# Project folder (where this script lives).
$ProjectDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Cbproj      = Join-Path $ProjectDir 'Project1.cbproj'

# RAD Studio install root. Adjust if needed.
$StudioRoot  = 'C:\Program Files (x86)\Embarcadero\Studio\23.0'
$rsvars      = Join-Path $StudioRoot 'bin\rsvars.bat'

if (-not (Test-Path $Cbproj)) { Write-Error "Project not found: $Cbproj"; exit 1 }
if (-not (Test-Path $rsvars)) { Write-Warning "rsvars.bat not found at '$rsvars'. Check \$StudioRoot." }

# -----------------------------------------------------------------------------
# Clean the output folder before compiling.
#   -Clean : removes objects, exe and intermediate artifacts (clean build).
#   Else   : let msbuild handle incremental builds (faster day-to-day), but drop
#            stale linker state files to avoid a broken link.
# -----------------------------------------------------------------------------
$OutDir = Join-Path $ProjectDir ("{0}\{1}" -f $Platform, $Config)

if ($Clean) {
    Write-Host "==> Cleaning '$OutDir'" -ForegroundColor Cyan
    if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
} else {
    Get-ChildItem -Path $ProjectDir -Filter '*.ilc' -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item $_.FullName -Force }
}

# -----------------------------------------------------------------------------
# Compile: use the exact command documented in AGENTS.md, passed to cmd.exe /c as
# ONE argument built from SINGLE-QUOTED literals (see below). We cd into $ProjectDir
# first and use relative names (project + log), matching the proven-working call.
# Full output goes to build_log.txt for inspection afterwards; %ERRORLEVEL% is left
# literal so CMD (not PowerShell) expands it at the end of the command line.
# -----------------------------------------------------------------------------
$msbuildArgs = '/p:Config={0} /p:Platform={1} /t:Build /v:minimal' -f $Config, $Platform
$logFile     = Join-Path $ProjectDir 'build_log.txt'

Write-Host "==> msbuild Project1.cbproj  (Config=$Config Platform=$Platform)" -ForegroundColor Cyan
$msbuildExit = 999   # sentinel: set to a failure code if the command never runs
Push-Location $ProjectDir
try {
    # Single command string for cmd.exe /c. Built from SINGLE-QUOTED parts so the
    # embedded double quotes around the rsvars path reach CMD untouched (a backtick
    # double-quoted string gets re-escaped by PowerShell for native cmd.exe and can
    # corrupt the "(x86)" path). %ERRORLEVEL% is left literal so CMD expands it.
    $cmd = 'call "' + $rsvars + '" && msbuild Project1.cbproj ' + $msbuildArgs + ' > build_log.txt 2>&1 & echo EXITCODE=%ERRORLEVEL%'
    & cmd.exe /c $cmd | ForEach-Object { Write-Host $_ }
    # The trailing "echo EXITCODE=..." makes the whole command line exit with msbuild's
    # code, so PowerShell sees it in $LASTEXITCODE.
    if ($null -ne $LASTEXITCODE) { $msbuildExit = [int]$LASTEXITCODE }
} finally {
    Pop-Location
}

# -----------------------------------------------------------------------------
# Result: read the log and report success/failure.
# -----------------------------------------------------------------------------
Write-Host ""
if (Test-Path $logFile) {
    Get-Content $logFile | ForEach-Object { Write-Host $_ }
} else {
    Write-Warning "No build_log.txt generated - compilation probably did not start."
}

# Success = msbuild exited 0 AND the exe is on disk. (MSBuild 4.8 with /v:minimal
# prints only its banner on success, so "Build succeeded" cannot be relied upon.)
$exePath = Join-Path $OutDir 'Project1.exe'
if ($msbuildExit -eq 0 -and (Test-Path $exePath)) {
    Write-Host ""
    Write-Host ("BUILD OK  ->  " + $exePath) -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host ("BUILD FAILED (exit code = $msbuildExit, exe present: $(Test-Path $exePath))") -ForegroundColor Red
    if ($msbuildExit -ne 0) { exit $msbuildExit } else { exit 1 }
}
