# MotecDataAnalyser — compiler and debug context

## Quick Start (Developer)

To get up and running with development:

1.  **Environment:** Ensure you have **Embarcadero C++ Builder 12** installed.
2.  **Build:** Run `.\build.ps1` in a PowerShell terminal to perform an incremental Debug/Win32 build.
    *   Use `.\build.ps1 -Clean` for a fresh rebuild.
3.  **Run (Portable):** To test the portable distribution, run `.\package.ps1`, then execute the resulting `MotecDataAnalyser.exe` from the `MotecDataAnalyser_Portable\` folder.
4.  **IDE:** Open `Project1.cbproj` in C++ Builder to perform manual compiles or debugging.

---

Use this file instead of asking the user to re-explain the toolchain.

## Product and compiler

| Item | Value |
|------|--------|
| IDE | Embarcadero RAD Studio / **C++ Builder 12** |
| Studio version folder | `23.0` |
| Install root | `C:\Program Files (x86)\Embarcadero\Studio\23.0` |
| Environment script | `C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat` |
| Personality | C++Builder VCL (`CppVCLApplication`) |
| Project file | `Project1.cbproj` |
| App type | Win32 VCL GUI (`FrameworkType=VCL`) |
| Default config / platform | **Debug** / **Win32** |
| C++ frontend | **Clang** (`BCC_UseClassicCompiler=false`) — not classic BCC32 |
| PCH | `Project1PCH1.h` |
| TCHAR | `wchar_t` |
| RTL | Dynamic RTL, runtime packages (`rtl`, `vcl`, `vclwinx`, `vclimg`, `bindengine`) |

Do **not** build this project with MSVC, MinGW, or g++. VCL (`#include <vcl.h>`, `__fastcall`, `__published`, `.dfm`) only compiles with C++ Builder.

Clang here is Embarcadero’s toolchain (bcc32c / bcc64), not stock LLVM. C++17-ish features work, but the STL is the RAD Studio one. Prefer portable C++11/14 style if something fails to compile.

## Command-line build (Windows)

PowerShell treats `(x86)` as a subexpression. Call `cmd.exe` with **single-quoted** `/c` argument:

```bat
cmd.exe /c 'call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat" && msbuild Project1.cbproj /p:Config=Debug /p:Platform=Win32 /t:Build /v:minimal'
```

Outputs:

- Objects / exe: `.\Win32\Debug\` (or `.\$(Platform)\$(Config)`)
- Settings written by the app: `Win32\Debug\MotecDataAnalyser.ini`
- Analysis thresholds (auto-created if missing): `Win32\Debug\MotecDataAnalyser.thresholds.ini`

From the IDE: open `Project1.cbproj`, target **Win32**, configuration **Debug**, then Build / Compile.

### Quick start with `build.ps1`

A wrapper script in the repo root performs the same build and prints a clear verdict:

```powershell
.\build.ps1                 # incremental Debug/Win32 build
.\build.ps1 -Clean          # delete Win32\Debug first, then rebuild
.\build.ps1 -Config Release # also supports -Platform Win64
```

- Full MSBuild output is written to `build_log.txt` (repo root) and echoed.
- Success = msbuild exit code 0 **and** `Win32\<Config>\Project1.exe` on disk. Do not rely on the "Build succeeded" line: with `/v:minimal`, MSBuild 4.8 prints only its banner even for a full clean rebuild.
- Studio is expected at the default path; adjust `$StudioRoot` in the script if it differs.

Gotchas already handled by the script (do not regress):

- The `.ps1` must stay **pure ASCII** — PowerShell 5.1 reads BOM-less scripts as ANSI/cp1252, so accents or em-dashes break parsing ("string missing terminator").
- The msbuild command is passed to `cmd.exe /c` as ONE argument built from single-quoted literals; backtick-escaped double quotes get re-escaped by PowerShell for native commands and corrupt the `(x86)` path.

### Packaging (portable distribution) with `package.ps1`

Builds a ready-to-share folder after a successful build:

```powershell
.\build.ps1                 # first, produce Win32\Debug\Project1.exe
.\package.ps1               # -> MotecDataAnalyser_Portable\  (Debug/Win32)
.\package.ps1 -Config Release -Platform Win64   # also supports these switches
.\package.ps1 -SkipData     # skip the ~58 MB data\ samples for a quick package
```

Layout produced: `MotecDataAnalyser.exe` + 5 runtime `.bpl` beside it, plus `runtime\`, `config\MotecDataAnalyser.rules.example.json`, `data\` (samples), and `README.txt`.

- The app links the dynamic packages `rtl vcl vclwinx vclimg bindengine`; their redistributables are `<name>290.bpl` in `Studio\23.0\Redist\win32|win64` (~18.9 MB for the 5, vs ~89 MB for all 93).
- The `.bpl` MUST sit next to the exe: Windows DLL search checks the exe dir first and never a `runtime\` subfolder automatically. On this dev machine the app runs from `Win32\Debug` (no .bpl there) only because C++ Builder registers its package path; on a clean target box without C++ Builder, the shipped `.bpl` are what make it start.
- Keep `package.ps1` pure ASCII too (same PS 5.1 rule as build.ps1).

## What the program does

Desktop tool that analyzes MoTeC telemetry:

- **LD/LDX** import (`parseMotecLd` / `DecodeMotecLd`) — visible button **“Import Motec Data”** (`Button1`, filter `*.ld`). This is the primary entry point.
- **CSV** import (`parseMotecCsv`) — handled by `ButtonLoadClick`; the “Load MoTeC CSV” button exists but is hidden by default in the `.dfm` (`Visible = False`) and never shown at runtime. Set `ButtonLoad->Visible = true` to expose it.
- **Live capture** from shared memory: **“Live (AMS2)”** → `Ams2LiveCapture.*`, **“AC EVO Live”** → `AcsEvoLiveCapture.*`. The two checkboxes are mutually exclusive; the red LED turns green on connect. File-import buttons disable while a live session is active.
- Channel names normalized via `resolveChannelKey` / `RuleConfig::resolveAlias` and `ChannelProfile` (`EvoM1`, `AMS2` / Generic)
- **Planned:** Implementation of "Math Channels" (calculated channels like Rake, Slip Ratio, etc.) based on the definitions in `Channel_Correspondence.md` to provide professional-grade telemetry analysis.
- Reports in English or French (`ReportLanguage`) — selectable in the UI, persisted
- Last folders + UI options (language, profile, full-report) stored in `MotecDataAnalyser.ini` next to the exe
- Analysis thresholds configurable via `MotecDataAnalyser.thresholds.ini` next to the exe (auto-created with defaults if missing; changes require an app restart)
- Diagnostic rules & channel aliases are **data-driven** via `RuleConfig` + optional JSON — see “Architecture & configuration files”

## Diagnostic engine (symptom → causes → recommendation)

The analyzer is not just a stats dump. It produces **explainable, confidence-scored recommendations**:

- `Recommendation` struct (`MotecSetupAnalyzer.h`): `symptom`, `text`, `issue`, `confidence` (0–1), `dataQuality` (HIGH/MEDIUM/LOW/INSUFFICIENT), `evidence[]`, `possibleCauses[]` (`CauseEntry`: cause + weight + adjustment + direction), `direction`, `suggestedChange`, `validationChannels[]`, `context`.
- Cause→adjustment matrix now lives in **`RuleConfig`** (singleton, `RuleConfig.h/.cpp`) as dynamic `RuleSymptom` vectors built from `buildDefaults()` and/or the optional rules JSON. `findSymptomRule(symptom)` delegates to `RuleConfig::instance().findRule(...)`. Known symptoms: `rear_traction_loss_on_exit`, `front_understeer_entry`, `bumpstop_engagement`, … The original static `symptomRules[]` / alias tables remain only in an inert `#if 0` block in `analyze_motec_setup.cpp`.
- `computeConfidence(value, threshold, sampleCount, lapsReproduced, channelsAvailable, channelsNeeded)` — weighted score (proximity 40 %, samples 25 %, reproducibility 20 %, channels 15 %).
- `dataQualityLabel(confidence, language)` — maps confidence to HIGH/MEDIUM/LOW/INSUFFICIENT.
- `formatRecommendationBlock(rec, language)` — renders the structured report block (MAIN ISSUE / CONFIDENCE / CONTEXT / OBSERVED DATA / POSSIBLE CAUSES / RECOMMENDATION / VALIDATION / DATA QUALITY).
- **Symptom weights are user-tunable**: base weights come from `RuleConfig::buildDefaults()` / the rules JSON; `saveSymptomWeights` / `loadSymptomWeights` persist per-cause overrides in the `[SymptomWeights]` section of `MotecDataAnalyser.thresholds.ini` (key = `symptom|cause`, float 0..1) by rewriting `RuleConfig::rulesRef()`. Loaded in `loadThresholds()` after `loadThresholdsPart1/2`, so they override any JSON weight for the same cause.
- Design rule (see `analyse_suspension.md`): a recommendation is **never** presented as certain when multiple causes are possible — always list weighted causes + validation channels.

## Visual panels (right side of the form)

Two `TPaintBox` widgets render live after each analysis (see `updateTireTemps` / `updateSuspension` in `MotecSetupForm.cpp`):

- `TireTempPaintBox` — per-corner tyre inner/centre/outer temps + pressure.
- `SuspensionPaintBox` — per-corner damper-velocity distribution (41 bins, −200…+200 mm/s), average travel, bumpstop engagement, and a damper-issue flag (`hasDamperIssue`).
- Layout is recomputed in `FormResize`; do not hard-code positions elsewhere.

## Architecture & configuration files

Since `DEVELOPMENT_PLAN.md` étapes 1–2, diagnostic intelligence is data-driven and all file access is exe-relative.

**RuleConfig (`RuleConfig.h/.cpp`) — single source of truth for rules + channel aliases.** A singleton that owns the symptom→causes matrix AND the channel-name alias maps (generic + EvoM1). At startup `MotecSetupForm.cpp` calls `loadRuleConfig()` **before** `loadThresholds()`: it looks for `MotecDataAnalyser.rules.json` beside the exe, then in `<exedir>config\`; if found it is parsed by a self-contained JSON parser and merged over built-in defaults (`buildDefaults()`), otherwise (or on parse error) defaults are kept. No recompile needed to add a vehicle/symptom/alias.

- Public API: `instance()`, `load(path) -> RuleLoadStatus{Ok, FileNotFound, ParseError}`, `resetToDefaults()`, `findRule(symptom)`, `rules()` / mutable `rulesRef()`, and `resolveAlias(profile, key, outKey)` (generic first, then profile-specific — same order as the old code).
- `analyze_motec_setup.cpp` delegates to it: symptom lookup → `RuleConfig::instance().findRule(...)`, channel resolution → `RuleConfig::resolveAlias`. The legacy static tables are inert (`#if 0`).

**Exe-relative paths (`MotecSetupForm.cpp`).** All file I/O is anchored to the executable, never absolute:
- `AppDirPath()` = exe dir with trailing delimiter; `DataPath()` = `<exedir>data\` if present else the exe dir.
- `SettingsFilePath()`, `ThresholdsFilePath()`, `RulesFilePath()` all build on `AppDirPath()`.

**Config file layout (all next to the exe):**

| File | Purpose | Loaded by / when |
|------|---------|------------------|
| `MotecDataAnalyser.ini` | Last folders + UI options (language, profile, full-report) — auto-saved | on start / on change |
| `MotecDataAnalyser.thresholds.ini` | Numeric analysis limits (`g_thresholds`) + `[SymptomWeights]` overrides; auto-created with defaults if missing | at startup only → **restart required** after edits |
| `config\MotecDataAnalyser.rules.json` *(or beside exe)* | Optional custom symptoms / aliases / weights (JSON) | at startup, before thresholds. Template: `MotecDataAnalyser.rules.example.json` |

Load order matters: rules JSON first, then `[SymptomWeights]` from the INI overrides individual cause weights — so the INI always wins per-cause.

## Source map

| File | Role |
|------|------|
| `Project1.cpp` | `WinMain`, creates `TFormMotecSetup` |
| `MotecSetupForm.h/.cpp/.dfm` | Main VCL form, file dialogs, progress, report memo, tire-temp + suspension paint boxes, threshold INI load/save (`loadThresholds`, `saveDefaultThresholds`, `saveSymptomWeights`/`loadSymptomWeights`) |
| `MotecSetupAnalyzer.h` | `LapStats`, `VelocityHistogram`, `DamperCornerStats`, `HandlingPhaseStats`, `Recommendation`/`CauseEntry`, parser APIs, `Thresholds` struct + `g_thresholds` global |
| `analyze_motec_setup.cpp` | CSV/LD analysis, confidence scoring, reports; all analysis thresholds read from `g_thresholds`. Symptom rules + channel aliases now live in `RuleConfig`; the old static tables remain only as an inert `#if 0` block |
| `MotecToCSV.h/.cpp` | Decode `.ld` binary, optional Motec.exe CSV export |
| `RuleConfig.h/.cpp` | Diagnostic intelligence: symptom→causes matrix + channel aliases (generic/EvoM1), self-contained JSON parser, built-in defaults; loaded from optional `MotecDataAnalyser.rules.json`. Étape 1 of the dev plan |
| `Ams2LiveCapture.h/.cpp`, `AcsEvoLiveCapture.h/.cpp` | Live telemetry capture from AMS2 / AC Evora M1 shared memory (open/read/close + feed a frame into `LapStats`) |
| `Ams2SharedMemory.h`, `AcsEvoSharedMemory.h` | Shared-memory layout structs for the two live-capture sources |
| `Project1PCH1.h` | Precompiled header (`vcl.h`, `tchar.h`) |
| `Unit1.h/.cpp/.dfm` | Leftover default form — not the main UI |
| `extract_channels.py` / `extract_ams2_channels.py` | Helper scripts that dump channel lists from sample CSVs into `AC_EVO_channels.md` / `AMS2_channels.md` |
| `Channel_Correspondence.md` | Cross-reference table AC EVO (Evora M1) ↔ AMS2 channel names |
| `analyse_suspension.md` | Design brief for the diagnostic engine (events, symptoms, causes, confidence, recommendations) |

## Data folder

`data/` holds sample telemetry and reference docs:

- `data/AC_EVO/` — `.ld` / `.ldx` files (Lotus Evora M1, Assetto Corsa)
- `data/AMS2/` — `.ld` / `.ldx` files (Assetto Corsa Motorsport 2)
- `data/*.csv` — MoTeC CSV exports (Ferrari 296 GT3, Ferrari F40 LM, Porsche 992 GT3 R)
- `data/ld-file-format.md` — binary LD layout notes
- `data/AMS2_GuideRapide.pdf`, `data/GTR2 Setup Guide.pdf` — reference PDFs

Keep **LD parsing** and **CSV parsing** separate. LD uses decoded `MotecColumnInfo` columns + sample time. CSV uses `rowFields` and `idx*` header indexes. Do not copy `readOptional(rowFields, idxSteer, …)` into `parseMotecLd`.

## Debug / compile notes

Typical C++ Builder 12 Clang errors:

- `E5892` undeclared identifier — wrong function scope (CSV vs LD) or missing include
- `E5550` redefinition with a different type — duplicate locals (`const double` then `double`)
- `E5781` assign to const — same duplicate-const pattern
- Linker (`ilink32`) package/lib path issues — `rsvars.bat` must be called first so `BDS`, `BDSLIB` are set

VCL conventions in this project:

- Form methods are `__fastcall`
- UI strings are `UnicodeString` / `String`; convert paths with `UTF8String` then `std::string`
- Use `NULL` or `nullptr` for VCL owners; `__finally` for dialog cleanup
- Do not “fix” VCL by switching to Win32-only or Qt patterns

When investigating logic (not compile errors):

- CSV path: `MotecSetupForm.cpp` → `AnalyzeSelectedFile` → `parseMotecCsv`
- LD path: `AnalyzeSelectedLdFile` → `parseMotecLd` → `DecodeMotecLd`
- Channel mismatch / alias resolution: `RuleConfig::resolveAlias` (generic first, then profile-specific); entry point `normalizeChannelName` / `resolveChannelKey`
- Symptom rules & their base weights: `RuleConfig::buildDefaults()` + optional `MotecDataAnalyser.rules.json`, loaded by `loadRuleConfig()` in the form ctor **before** `loadThresholds()`
- Threshold values: `g_thresholds` in `analyze_motec_setup.cpp`, loaded from `MotecDataAnalyser.thresholds.ini` at startup (see `loadThresholds` → `loadThresholdsPart1/2` in `MotecSetupForm.cpp`)
- Symptom weight overrides: `[SymptomWeights]` section of the same INI, applied by `loadSymptomWeights` over `RuleConfig::rulesRef()` (called from `loadThresholds`, so it wins per-cause)
- Confidence / data quality: `computeConfidence` + `dataQualityLabel` in `analyze_motec_setup.cpp`
- Recommendation rendering: `formatRecommendationBlock` in `analyze_motec_setup.cpp`
- Binary LD layout notes: `data/ld-file-format.md`

Project `IncludePath` / `ILINK_LibraryPath` still reference `..\..\..\form test\` (legacy). Prefer local files in this repo.

## Agent instructions

- Compile with C++ Builder 12 as above; treat IDE or MSBuild output as the source of truth for compile errors.
- Do not retarget the project to another compiler.
- Do not mix CSV row-index helpers into the LD sample loop.
- Do not commit unless asked.
- Keep changes small and in the existing VCL + `std::` style already used in the `.cpp` files.
- Analysis thresholds live in `g_thresholds` (defined in `analyze_motec_setup.cpp`, declared in `MotecSetupAnalyzer.h`). New thresholds must be added to the `Thresholds` struct, `saveDefaultThresholds()`, and `loadThresholdsPart1/2()`. Changes to the INI file require an app restart.
- Symptom rules + channel aliases live in **`RuleConfig`** (`buildDefaults()`), optionally overridden/extended by `MotecDataAnalyser.rules.json`; per-cause weight overrides are persisted via `saveSymptomWeights` / `loadSymptomWeights`. To add a new symptom or alias, edit `RuleConfig::buildDefaults()` (and/or the rules JSON) with its `CauseEntry` list and validation channels — do **not** resurrect the old static tables in `analyze_motec_setup.cpp`.
- Recommendations must stay **explainable**: every `Recommendation` carries `evidence`, weighted `possibleCauses`, and `validationChannels`. Do not collapse multiple causes into a single "certain" conclusion.

## Search and analysis

- When a search returns no results, retry at most once with a simpler search pattern.
- If the second search also returns no results, consider the information unavailable and continue with the information already available.
- Do not repeat the same search or analysis unnecessarily.
- Once enough information is available to answer the request, stop investigating and provide the final answer.