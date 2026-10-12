# MoTeC Setup Analyzer

A desktop tool that reads MoTeC telemetry and turns it into an **explainable setup report**: per-lap statistics, tyre/thermal/suspension diagnostics, and confidence-scored recommendations (symptom → weighted causes → suggested adjustment + validation channels).

Reports can be generated in **English** or **Français**. Channel names are normalized automatically for two vehicle profiles: **Evo** (Lotus Evora M1 / Assetto Corsa) and **AMS2** (Assetto Corsa Motorsport 2, generic fallback).

---

## Getting started


On first run the app creates its settings file next to itself:
- `MotecDataAnalyser.ini` — last folders + UI options (language, profile, full-report).
- `MotecDataAnalyser.thresholds.ini` — all analysis thresholds (created with defaults if missing).

---

## Loading a data file

The main window has one import button and two optional live-capture checkboxes.

### Import Motec Data  →  `.ld` / `.ldx` files
Click **Import Motec Data**. A file dialog opens filtered to `*.ld`. Pick your MoTeC log (or its index) and the app decodes it, normalizes channel names against the selected profile, runs the analysis, and fills:

- the report memo on the left,
- the **tyre temperature** panel (per-corner inner/centre/outer temps + pressure),
- the **suspension / damper velocity** panel (41-bin distribution −400…+400 mm/s per corner, average travel, bumpstop engagement).

### Live capture
- **Live (AMS2)** — reads AMS2 shared memory in real time; auto-selects the *AMS2* profile.
- **AC EVO Live** — reads AC Evora M1 live data; auto-selects the *Evo* profile.
- **PMR UDP** — listens for PMR telemetry datagrams on a local UDP port (default `7576`, configurable in `[PMR] Port` of `MotecDataAnalyser.ini`); no simulator process is required, any PMR-compatible sender can feed it; auto-selects the *Pmr* profile.

The three are mutually exclusive (checking one unchecks the others). The red LED turns green once connected — for PMR this means the socket is bound and listening. While a session is active, file import buttons are disabled until you untick the checkbox.

### Options that persist automatically
These UI controls save themselves to `MotecDataAnalyser.ini` — no restart needed:

| Control | Values | Effect |
|---------|--------|--------|
| **Profile** | Evo / AMS2 / PMR | Channel profile: *Evo*/*AMS2* select the name-normalization tables for file imports; *Pmr* is live-capture only (PMR UDP). |
| **Language** | English / Français | Report language (all report strings are localized). |
| **Full Report** | on/off | Toggles the extended sections of the written-out report. |

---

## Tablet Viewer (LAN web report)

The app can serve the completed analysis as a self-contained web page to any tablet or phone on your local network — no internet, no native app required.

### Enabling it
1. Open **Settings → Tablet Viewer** tab.
2. Tick **Enable tablet report viewer (local web server)** and set an HTTP port (default `8765`, range 1024–65535).
3. Click **Save**. The status bar shows the URL, e.g. `Tablet viewer active — http://192.168.1.42:8765`.

On first start (or after a port change) Windows Firewall will show a one-time prompt for inbound TCP on that port — click **Allow access**. No firewall rules are modified automatically by the app.

### How it works
- The server starts at launch when enabled and runs until you disable it in Settings or close the app. Toggling live (no restart) is supported.
- Every time an analysis completes (file import *or* live-capture stop), the JSON payload is pushed automatically — connected tablets re-render within ~3 s via polling, no manual refresh needed.
- The page shows: 7 text finding cards + tyre-temp heat map + brake-temp bars + damper velocity histograms. Before any analysis it displays a "no data yet" notice gracefully.

### Disabling / stopping
Untick the checkbox in Settings and Save (or simply close the app). Connected browsers get connection-refused; re-enabling restores service immediately without an app restart.

---

## Reading a recommendation: confidence & data quality

Every diagnostic in the report is a `Recommendation` block, not a flat verdict. It always carries:

- **MAIN ISSUE / symptom** — what was detected (e.g. rear traction loss on exit).
- **OBSERVED DATA** — concrete evidence values from your telemetry.
- **POSSIBLE CAUSES** — several causes ranked by weight, each with an adjustment and direction. A recommendation is *never* presented as certain when multiple causes fit the data.
- **RECOMMENDATION / suggested change**.
- **VALIDATION channels** — which other channels to check to confirm or rule out a cause.
- **CONFIDENCE + DATA QUALITY** — how much you can trust it (below).

### Confidence score (0 → 1)
`computeConfidence()` blends four signals:

| Signal | Weight | How it's measured |
|--------|--------|-------------------|
| Proximity to threshold | **40 %** | `min(1, (\|value\|/threshold − 1))`. The further the value is past its limit, the higher this term (capped at 1). Below the threshold it contributes 0. |
| Sample count | **25 %** | `min(1, samples / 100)`. More supporting samples → more confidence; saturates at 100. |
| Reproducibility across laps | **20 %** | `min(1, lapsReproduced / MinLapsForReproducibility)`. A symptom seen on many laps is trusted more (default needs 3). |
| Channel coverage | **15 %** | `channelsAvailable / channelsNeeded`. Missing required channels directly lowers confidence. |

The four terms are summed and clamped to `[0, 1]`. If no required channel is available at all (`channelsAvailable ≤ 0`), the score is forced to **0**.

### Data quality tiers
The numeric confidence maps to a label via `dataQualityLabel()`, using thresholds from the `[Confidence]` INI section (defaults shown):

| Confidence | Label (EN / FR) | Default cutoff |
|------------|-----------------|----------------|
| ≥ 0.85 | HIGH / ÉLEVÉE | `High = 0.85` |
| ≥ 0.60 | MEDIUM / MOYENNE | `Medium = 0.60` |
| ≥ 0.30 | LOW / FAIBLE | `Low = 0.30` |
| < 0.30 | INSUFFICIENT / INSUFFISANTE | — |

A separate **data-coverage** line in the report also summarizes how complete your channel set is (e.g. "HIGH — most channels available" vs "INSUFFICIENT — very limited data"). Treat a LOW/INSUFFICIENT result as *directional only* and gather more laps or fix missing channels before acting on it.

---

## Tuning thresholds

There are **two** configuration files, both created next to the exe:

1. `MotecDataAnalyser.thresholds.ini` — numeric analysis limits (the "when is this a problem?" values).
2. `MotecDataAnalyser.rules.json` *(optional)* — symptom rules and per-cause weights; see below.

> **There is no threshold editor in the UI.** To change any limit, close the app, edit the `.ini`, then restart it (changes are read once at startup). The `[SymptomWeights]` section of the same INI can override individual cause weights that came from the rules JSON.

### `MotecDataAnalyser.thresholds.ini`
Sections and a few representative keys (all values are plain numbers; units noted):

| Section | What it controls | Example keys |
|---------|------------------|--------------|
| `[General]` | Minimum samples before an analysis is meaningful | `MinDamperSamples`, `MinHandlingSamples`, `MinSteerYawSamples`, `HandlingConfidenceSamples` |
| `[Pressure]` | Tyre pressure limits (psi) + side-to-side spread | `LowEvo`, `HighEvo`, `LowAms2`, `HighAms2`, `SideDiffMax` |
| `[TyreTemp]` | Temperature spreads (°C), camber-related bands | `CentreShoulderEvo`, `InnerOuterEvo`, …, `CamberMinSpread`, `CamberMaxSpread` |
| `[LSD]` | Lock / slip limits per profile | `HighEvo`, `LowEvo`, `HighAms2`, `LowAms2` |
| `[Damper]` | Damper velocity distribution checks (mm/s) | `LowSpeedThreshold`, `AsymmetryThreshold`, `HighSpeedShareMax`, `LowSpeedShareMin`, `LowSpeedStdDevMax` |
| `[Handling]` | Phase detection + under/oversteer triggers | `BrakePedalMin`, `SteerMinCornering`, `LatGMaxUndersteer`, `YawMinOversteer`, … |
| `[Thermal]` | Fluid temperature limits (°C) | `WaterTempMax`, `OilTempMax` |
| `[Suspension]` | Travel, ride-height and velocity limits | `TravelMax`, `RideHeightDiffMax`, `VelocityMax`, `AvgTravelFrontMax`, `AvgTravelRearMax` |
| `[Brake]` | Brake temp/bias/torque checks | `TempDiffMax`, `BiasMax`, `TorqueMin`, … |
| `[ChannelMismatch]` | Cross-channel consistency limits | `PressureMax/Min`, `SuspensionMax/Min` |
| `[TCS]` / `[ABS]` | Intervention activity caps | `ActiveMax` |
| `[WheelSlip]` | Slip limit | `Max` |
| `[WheelSpeed]` | Wheel-speed imbalance limits | `FrontDiffMax`, `RearDiffMax`, `TempDiffMax` |
| `[Traction]` | Traction-loss detection bands | `RearDiffMax`, `LongGMax`, … |
| `[Bumpstop]` | Bumpstop engagement floor | `Min` |
| `[Aero]` | Aero balance / ride-height sensitivity | `BalanceMax`, `HeightDiffMax`, `SpeedMin` |
| `[Steering]` | Steer/yaw correlation + speed bands | `CorrUndersteer`, `CorrOversteer`, … |
| `[Stability]` | Stability score and correlations | `ScoreMin`, `WheelSpeedDiffMax`, … |
| `[Lap]` | Lap-to-lap time gap limit (s) | `TimeGapMax` |
| `[Confidence]` | Quality-tier cutoffs + reproducibility laps | `Low = 0.30`, `Medium = 0.60`, `High = 0.85`, `MinLapsForReproducibility = 3` |

**How to tune:** raise a limit if you get too many false positives for that symptom; lower it if real problems are slipping through. Change one or two values at a time, restart, and re-run the same file to compare. If you delete `MotecDataAnalyser.thresholds.ini`, the app regenerates it with defaults on next start.

### `[SymptomWeights]` (same INI)
Per-cause weights that override the rules JSON. Key format is `"symptom|cause"`, value a float in `0..1`:

```ini
[SymptomWeights]
rear_traction_loss_on_exit|excessive_rear_damper_bump = 0.45
front_understeer_entry|low_front_camber = 0.30
```

### Custom rules: `MotecDataAnalyser.rules.json` *(optional)*
To change *which* symptoms exist, their cause lists, or validation channels (rather than just numeric limits), drop a file named **`MotecDataAnalyser.rules.json`** either next to the exe or in a `config\` subfolder. A ready-to-edit template is shipped at:

```
config\MotecDataAnalyser.rules.example.json   (in the portable package)
MotecDataAnalyser.rules.example.json          (repo root, for developers)
```

Copy it to one of those locations and rename without `.example`. If no rules file is found, built-in defaults are used. Load order at startup: **rules JSON first**, then `[SymptomWeights]` from the INI overrides individual weights — so the INI always wins on a per-cause basis.

---

## Where everything lives (all relative to the exe)

| File / folder | Purpose |
|---------------|---------|
| `MotecDataAnalyser.ini` | Last folders + UI options (auto-saved). |
| `MotecDataAnalyser.thresholds.ini` | Analysis thresholds + `[SymptomWeights]`. Edit → restart. |
| `config\MotecDataAnalyser.rules.json` *(or beside exe)* | Optional custom symptom rules / weights. |
| `data\` | Sample telemetry (CSV, `.ld`, `.ldx`) and reference docs for trying the app out. |

---

## Building from source (developers)

```powershell
.\scripts\build.ps1         # incremental Debug/Win32 build -> BUILD OK on success
.\scripts\build.ps1 -Clean  # wipe Win32\Debug first, then rebuild
.\scripts\package.ps1       # produce MotecDataAnalyser_Portable\ after a successful build
```

See `AGENTS.md` for the full toolchain (Embarcadero C++ Builder 12 / Clang VCL), command-line build details, and packaging notes. Do **not** retarget to MSVC/MinGW/g++.
#   M o t e c D a t a A n a l y s e r 
 
 