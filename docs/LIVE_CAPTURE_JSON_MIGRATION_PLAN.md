# Live Capture → JSON Table-Driven Migration Plan

> **Status**: PLANNED (not started)  
> **Created**: 2026-10-09  
> **Goal**: Replace hardcoded `lap.X.add(...)` calls in all three live captures with a single pre-resolved lookup table driven by `mapping.json`, so that one resolution logic serves both file import and live capture paths.

---

## 1. Motivation & Design Rationale

### Problem
- Three separate C++ files (`Ams2LiveCapture.cpp`, `AcsEvoLiveCapture.cpp`, `PmrLiveCapture.cpp`) each contain ~50–80 hardcoded channel assignments with inline unit conversions (×3.6, /9.81, ×DEG_PER_RAD, etc.).
- Adding a new simulator requires writing all of this from scratch in C++.
- Changing a conversion factor means editing C++, recompiling, rebuilding the exe.
- A forgotten `lap.X.add(...)` line = silent data gap with no warning.
- The existing `mapping.json` already has LIVE-source rows (AMS2) that are **dead weight** — parsed but never consumed at runtime.

### Solution: Pre-resolved JSON table per capture session

```
┌─────────────────────────────────────────────────────────────┐
│  CONNECT TIME (once, ~1 ms total):                          │
│                                                             │
│  mapping.json ──► filter(profile=AMS2, source=LIVE)        │
│                  ──► build ResolvedChannel[] array:         │
│                      { rawName → LapStats field ptr +       │
│                        factor + offset }                    │
│                  ──► validate all aliases covered           │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  PER FRAME (~60-100 Hz, <5 µs):                             │
│                                                             │
│  for each ResolvedChannel rc:                               │
│      value = rawChannels[rc.rawName] * rc.factor + offset   │
│      (rc.lapField)->add(value)                              │
└─────────────────────────────────────────────────────────────┘
```

**Key properties:**
- Zero string work in the hot path (pre-resolved to index/pointer).
- One generic `FeedLapFromTable()` function replaces three separate feed functions.
- Connect-time validation: "alias X has no mapping for profile Y" → visible warning.
- Adding a new sim = thin binary reader + JSON entries only.

---

## 2. Current State (what exists today)

### Files involved

| File | Role in current design |
|------|----------------------|
| `Ams2LiveCapture.cpp` (~line 184–305) | `Ams2FeedLap()` — ~60 hardcoded assignments from shared-memory struct |
| `AcsEvoLiveCapture.cpp` (~line 227–310) | `AcsEvoFeedLap()` — ~65 hardcoded assignments from physics page |
| `PmrLiveCapture.cpp` (~line 448–590) | PMR feed section inside UDP handler — ~70 hardcoded assignments from decoded packet |
| `MotecSetupAnalyzer.h` (lines 276–310) | `ChannelMapping`, `MappingSource`, `AnalysisAlias` enums/structs |
| `RuleConfig.cpp` (~line 453–457, ~618–632) | JSON parsing of `"source"` field; dead `resolveRawToAlias()` function |
| `MotecDataAnalyser.mapping.json` | Already has AMS2 LIVE rows (dead), no EVO/PMR profiles for live |

### What the captures do today (per frame):

```cpp
// Example: Ams2LiveCapture.cpp line 190-265
lap.speed.add(frame.mSpeed * 3.6);                    // m/s → km/h
lap.rpm.add(frame.mRpm);                              // direct
lap.throttle.add(frame.mThrottle * 100.0);            // normalized → %
lap.steerAngle.add(frame.mSteering * 360.0);          // turns → degrees
latG = frame.mLocalAcceleration[2] / G;              // m/s² → g (G=9.81)
lap.pressureLF.add(frame.mAirPressure[i] / 6.894757); // kPa → PSI
// ... ~50 more lines per sim, each with its own conversion factor
```

### What `mapping.json` already has for AMS2 LIVE (dead rows):

The file contains entries like:
```json
{ "alias": "Engine_Oil_Temp", "profile": "AMS2", "source": "LIVE",
  "channel": "mOilTempCelsius", "unit": "°C" }
```
These are parsed into `RuleConfig::m_channelMappings` but **never read** by any live code path.

---

## 3. Target Architecture

### New data structures (in a new header, e.g., `LiveChannelResolver.h`)

```cpp
// One entry in the pre-resolved table — built once at connect time.
struct ResolvedChannel {
    const char* rawName;              // key into per-frame channel dictionary
    StatSummary** lapFieldPtr;        // pointer to LapStats field (e.g., &lap.speed)
    double factor;                    // multiply after reading raw value
    double offset;                    // add after multiplying
};

// Built once, stored in the capture session object.
struct ResolvedChannelTable {
    std::vector<ResolvedChannel> channels;  // ~50-80 entries per sim
    int count = 0;
    
    void build(ChannelProfile profile);     // reads RuleConfig singleton
    bool validate(std::string& missingReport) const;  // check all required aliases covered
};

// The ONE generic feed function (replaces Ams2FeedLap / AcsEvoFeedLap / PMR section):
void FeedLapFromTable(LapStats& lap, 
                      const std::unordered_map<std::string,double>& rawChannels,
                      const ResolvedChannelTable& table);
```

### Per-sim C++ changes (what remains sim-specific)

Each capture file keeps ONLY:
1. **Binary layout parsing** — reading the shared-memory struct / UDP packet into a flat `std::unordered_map<std::string,double>` of named raw values. This is unavoidable; struct offsets can't live in JSON sanely.
2. **Connect/disconnect lifecycle** — socket, memory-mapped file handle, etc.

Everything else (unit conversion, field assignment) moves to the table-driven path.

### Example: AMS2 per-frame becomes:

```cpp
// Ams2LiveCapture.cpp — NEW version of frame processing:
void ProcessAms2Frame(const Ams2SharedMemory& f, LapStats& lap, 
                      const ResolvedChannelTable& tbl) {
    // 1. Build raw channel dictionary (sim-specific, no conversions here):
    std::unordered_map<std::string,double> ch;
    ch["mSpeed"] = f.mSpeed;                    // m/s — RAW value, no conversion
    ch["mRpm"] = f.mRpm;
    ch["mThrottle"] = f.mThrottle;              // 0..1 normalized
    ch["mSteering"] = f.mSteering;             // turns (fraction of full lock)
    ch["mLocalAccX"] = f.mLocalAcceleration[0]; // m/s² lateral
    ch["mLocalAccZ"] = f.mLocalAcceleration[2]; // m/s² longitudinal
    ch["mAirPressureLF"] = f.mAirPressure[TYRE_FRONT_LEFT];  // kPa RAW
    // ... ~50 raw values, NO unit conversion here

    // 2. Generic table-driven feed (ONE function for all sims):
    FeedLapFromTable(lap, ch, tbl);
}
```

### Corresponding `mapping.json` entries:

```json
{ "alias": "Speed",           "profile": "AMS2", "source": "LIVE",
  "channel": "mSpeed",        "unit": "km/h", "factor": 3.6 },
  
{ "alias": "ThrottlePct",     "profile": "AMS2", "source": "LIVE",
  "channel": "mThrottle",     "unit": "%",    "factor": 100.0 },

{ "alias": "SteerAngleDeg",   "profile": "AMS2", "source": "LIVE",
  "channel": "mSteering",     "unit": "deg",  "factor": 360.0 },

{ "alias": "LatG",            "profile": "AMS2", "source": "LIVE",
  "channel": "mLocalAccZ",    "unit": "g",    "factor": 0.10194 },   // = 1/9.81

{ "alias": "TyrePressFL",     "profile": "AMS2", "source": "LIVE",
  "channel": "mAirPressureLF","unit": "PSI",  "factor": 0.145037 },   // = 1/6.894757 (kPa→PSI)

{ "alias": "OilTempC",        "profile": "AMS2", "source": "LIVE",
  "channel": "mOilTempCelsius","unit": "°C" }   // factor defaults to 1.0, offset 0
```

---

## 4. Step-by-Step Migration Plan

### Phase 0: Foundation (no behavior change)

| # | Task | File(s) | Details |
|---|------|---------|--------|
| 0.1 | Create `LiveChannelResolver.h` | NEW file | Define `ResolvedChannel`, `ResolvedChannelTable`, declare `FeedLapFromTable()` and table build/validate functions |
| 0.2 | Implement `build(profile)` in `.cpp` | NEW: `LiveChannelResolver.cpp` | Iterate `RuleConfig::instance().channelMappings()`, filter by profile + source==LiveSharedMemory, resolve alias→field pointer via a static dispatch map (see §5) |
| 0.3 | Implement `FeedLapFromTable()` | Same file | Loop over resolved entries: read from raw dict → multiply factor → add offset → call `(rc.lapFieldPtr)->add(value)`; skip if key missing in dict |
| 0.4 | Add alias→field dispatch table | Same file or header | Static `std::unordered_map<std::string, StatSummary**>` mapping `"Speed"` → `&LapStats::speed`, etc. This is the ONLY place that knows LapStats field names — compile-time checked via a template helper if desired |
| 0.5 | Unit test: table build + feed with mock data | Test harness (or manual) | Build table for AMS2, create fake raw dict, verify `lap.speed.avg()` == expected value after N frames |

**Exit criteria**: New files compile standalone; no existing code modified yet.

---

### Phase 1: Migrate AMS2 (proof of concept — LIVE rows already exist in JSON)

| # | Task | File(s) | Details |
|---|------|---------|--------|
| 1.1 | Audit all `lap.X.add(...)` calls in `Ams2FeedLap()` | Read-only | List every field + conversion factor (see §6 AMS2 table below). Cross-check against existing LIVE rows in mapping.json — note gaps and mismatches |
| 1.2 | Update/complete AMS2 LIVE entries in `mapping.json` | `MotecDataAnalyser.mapping.json` | Ensure EVERY channel from the audit has a corresponding entry with correct factor/offset/unit. Add missing ones (e.g., suspension travel, wheel slip) |
| 1.3 | Refactor `Ams2FeedLap()` → raw dict + table feed | `Ams2LiveCapture.cpp`, `.h` | Replace all ~60 hardcoded assignments with: build `std::unordered_map<std::string,double>` from struct fields (raw values only), then call `FeedLapFromTable()`. Keep the function signature compatible so callers don't change. Store `ResolvedChannelTable` as a member, built in connect/init path |
| 1.4 | Add connect-time validation warning | `Ams2LiveCapture.cpp` + form status memo | After table build: call `validate()`; if any required alias is missing → show "WARNING: channel X not mapped for AMS2" in the live-capture status area (same pattern as PMR socket errors) |
| 1.5 | Build & verify values match pre-migration | Full msbuild Debug + manual test | Run a short AMS2 session, compare lap stats output against known-good baseline from before migration. Key checks: speed km/h, pressure PSI, G-forces, tyre temps inner/outer orientation (AMS2 has the L/R swap logic!) |
| 1.6 | **CRITICAL**: Verify AMS2 tyre temp left/right swap | `mapping.json` + resolver | Current code does: LF uses mTyreTempRight for Inner and mTyreTempLeft for Outer; RF is reversed. This per-wheel asymmetry CANNOT be expressed with a single factor/offset — it requires the raw dict to already carry pre-swapped names (e.g., `ch["tyreInnerLF"] = f.mTyreTempRight[0]`). The swap stays in C++ as part of "raw channel extraction" |

**Exit criteria**: AMS2 live capture produces identical LapStats values as before. No regression in any report tab. Build 0W/0E.

---

### Phase 2: Migrate AC EVO (ACs Evo)

| # | Task | File(s) | Details |
|---|------|---------|--------|
| 2.1 | Audit all `lap.X.add(...)` calls in `AcsEvoFeedLap()` | Read-only | List every field + conversion factor (see §6 EVO table). Note: EVO has NO existing LIVE rows in mapping.json — we'll create them from scratch |
| 2.2 | Create ACs Evo profile entries in `mapping.json` | `MotecDataAnalyser.mapping.json` | Add `"profile": "EVOM1"` (or whatever the enum maps to) + `"source": "LIVE"` for every channel. Include factor/offset from audit |
| 2.3 | Refactor `AcsEvoFeedLap()` → raw dict + table feed | `AcsEvoLiveCapture.cpp`, `.h` | Same pattern as Phase 1: extract raw values into dictionary, call generic feed function. Note EVO-specific items: wheel speed uses derived factor (`kWheelRadiusM * 3600 / (2π*1000)`), TCS/ABS are boolean→% conversion |
| 2.4 | Build & verify | Full msbuild + manual test | Same validation as Phase 1 step 1.5 |

**Exit criteria**: EVO live capture produces identical values. Build 0W/0E.

---

### Phase 3: Migrate PMR (UDP)

| # | Task | File(s) | Details |
|---|------|---------|--------|
| 3.1 | Audit all `lap.X.add(...)` calls in PMR feed section (~line 452–600) | Read-only | List every field + conversion factor (see §6 PMR table). Note: PMR has NO profile entries at all currently — we'll create them from scratch with `"profile": "PMR"` or similar. **Requires adding a new `ChannelProfile` enum value** if not present |
| 3.2 | Add PMR to `ChannelProfile` enum (if needed) | `MotecSetupAnalyzer.h`, `RuleConfig.cpp/.h` | Check existing values: Generic, EvoM1, AMS2... If no "PMR" → add it + update JSON parser's profile matching in `mergeJsonChannelMapping()` (~line 460-470 of RuleConfig.cpp) |
| 3.3 | Create PMR LIVE entries in `mapping.json` | `MotecDataAnalyser.mapping.json` | All channels from audit with correct factors (kMsToKmh=3.6, kBarToPsi≈14.5038, kMToMm=1000, etc.) |
| 3.4 | Refactor PMR feed section → raw dict + table feed | `PmrLiveCapture.cpp`, `.h` | Same pattern. Note: PMR has conditional logic (isfinite guards, fuel capacity check) — these stay in the "raw extraction" phase as they're data-quality decisions, not unit conversions. The table only handles clean value→field mapping |
| 3.5 | Build & verify | Full msbuild + manual test with UDP capture | Same validation pattern |

**Exit criteria**: PMR live capture produces identical values. Build 0W/0E.

---

### Phase 4: Cleanup & unification (optional, after all three are migrated)

| # | Task | File(s) | Details |
|---|------|---------|--------|
| 4.1 | Remove dead `resolveRawToAlias()` function | `RuleConfig.h`, `RuleConfig.cpp` | No longer needed — the new resolver replaces it with a purpose-built implementation |
| 4.2 | Unify file-import path to use same table (optional) | `analyze_motec_setup.cpp` | Currently uses flat alias maps (`m_genericAliases`). Could switch to calling the same pre-resolved mechanism for consistency, but this is lower priority since file import already works correctly and has no "silent gap" risk in practice (CSV headers are visible) |
| 4.3 | Update `agents.md` architecture doc | `agents.md` | Reflect new data flow: captures → raw dict → table feed → LapStats; mapping.json LIVE rows now functional |
| 4.4 | Remove old hardcoded code paths if fully replaced | All three capture files | Delete the original ~60-line assignment blocks (replaced by raw-dict + FeedLapFromTable) |

---

## 5. Alias-to-Field Dispatch Table (§0.4 detail)

This is a **compile-time static** mapping from JSON alias strings to `StatSummary*` member pointers on `LapStats`. It lives in one place and is the only code that knows LapStats field names:

```cpp
// In LiveChannelResolver.cpp (or .h as inline):
static const std::unordered_map<std::string, StatSummary**>& aliasToFieldMap() {
    static const auto map = []{ 
        std::unordered_map<std::string, StatSummary*> m;  // note: non-const ptr to member
        struct Entry { const char* name; StatSummary*& field; };
        LapStats dummy;   // address-of trick for member pointers (or use offsetof-style)
        m["Speed"]           = &dummy.speed;
        m["Rpm"]             = &dummy.rpm;
        m["ThrottlePct"]     = &dummy.throttle;
        m["BrakePedalPct"]   = &dummy.brakePedal;
        m["SteerAngleDeg"]   = &dummy.steerAngle;
        m["YawRateDps"]      = &dummy.yawRate;
        m["LatG"]            = &dummy.latG;
        m["LongG"]           = &dummy.longG;
        // ... ~50 more entries covering all LapStats fields used by captures
        return m; 
    }();
    return map;  // NOTE: this won't work directly with member pointers — see implementation note below
}

// IMPLEMENTATION NOTE: C++ doesn't allow a static table of "pointer to member" that's
// generic across instances. The practical approach is one of:
//   (a) A switch/if-chain on the alias string → direct field access per frame (~50 branches, 
//       branch predictor handles it fine at 100Hz). Simplest and fastest.
//   (b) Store a function pointer per entry: `void (*addFn)(LapStats&, double)` where each is 
//       an inline lambda like [](LapStats& l, double v){ l.speed.add(v); }. Zero overhead after inlining.
// RECOMMENDED: Option (b). Build the table as vector of {rawName, addFn, factor, offset}.
```

**Recommended implementation for `ResolvedChannel`:**
```cpp
struct ResolvedChannel {
    const char* rawName;                          // key into per-frame dict
    void (*addValue)(LapStats&, double);          // e.g., [](LapStats& l, double v){ l.speed.add(v); }
    double factor = 1.0;
    double offset = 0.0;
};

// Feed function:
void FeedLapFromTable(LapStats& lap, 
                      const std::unordered_map<std::string,double>& ch,
                      const ResolvedChannelTable& tbl) {
    for (const auto& rc : tbl.channels) {
        auto it = ch.find(rc.rawName);
        if (it == ch.end()) continue;  // channel not present this frame — skip silently
        double v = it->second * rc.factor + rc.offset;
        rc.addValue(lap, v);
    }
}
```

This is **zero-allocation** in the hot path: one hash-map lookup per entry (~50 lookups × ~20ns = 1µs total), then a direct function call that compiles to an inline `StatSummary::add()`.

---

## 6. Per-Sim Conversion Tables (the data to migrate)

### 6A: AMS2 (`Ams2FeedLap`, lines ~184–305 of Ams2LiveCapture.cpp)

| LapStats field | Raw struct member(s) | Source unit → Target unit | Factor | Offset | Notes |
|-|-|-|-|-|-|
| `speed` | `mSpeed` | m/s → km/h | 3.6 | 0 | |
| `rpm` | `mRpm` | RPM direct | 1.0 | 0 | |
| `throttle` | `mThrottle` | normalized(0-1) → % | 100.0 | 0 | |
| `brakePedal` | `mBrake` | normalized(0-1) → % | 100.0 | 0 | |
| `steerAngle` | `mSteering` | turns(fraction) → deg | 360.0 | 0 | Full lock = ±720°? Verify: factor=360 means 1 turn = 360°. If sim reports fraction of full steering range, this is correct |
| `yawRate` | `mAngularVelocity[1]` | rad/s → deg/s | DEG_PER_RAD (57.2958) | 0 | Y-axis in AMS2 local frame |
| `longG` | `mLocalAcceleration[0]` | m/s² → g | 1/9.81 = 0.10194 | 0 | X axis = longitudinal in AMS2 |
| `latG` | `mLocalAcceleration[2]` | m/s² → g | 1/9.81 = 0.10194 | 0 | Z axis = lateral in AMS2 (NOTE: swapped vs EVO!) |
| `susTravelLF..RR` | `mSuspensionTravel[i]` | m → mm | 1000.0 | 0 | i=TYRE_FRONT_LEFT etc. |
| `suspVelLF..RR` | `mSuspensionVelocity[i]` | m/s → mm/s | 1000.0 | 0 | Direct from shared memory (not derived) |
| `pressureLF..RR` | `mAirPressure[i]` | kPa → PSI | 1/6.894757 = 0.145037 | 0 | **CRITICAL**: verify target unit is PSI not bar — thresholds use PSI |
| `tyreInnerTempLF` | `mTyreTempRight[FL]` | °C direct | 1.0 | 0 | ⚠️ LEFT tyre: Right=Inner, Left=Outer (see note below) |
| `tyreCentreTempLF` | `mTyreTempCenter[FL]` | °C direct | 1.0 | 0 | |
| `tyreOuterTempLF` | `mTyreTempLeft[FL]` | °C direct | 1.0 | 0 | ⚠️ LEFT tyre: Left=Outer |
| `tyreInnerTempRF` | `mTyreTempLeft[FR]` | °C direct | 1.0 | 0 | ⚠️ RIGHT tyre: Left=Inner (swapped!) |
| `tyreCentreTempRF` | `mTyreTempCenter[FR]` | °C direct | 1.0 | 0 | |
| `tyreOuterTempRF` | `mTyreTempRight[FR]` | °C direct | 1.0 | 0 | ⚠️ RIGHT tyre: Right=Outer (swapped!) |
| *(same pattern for LR, RR)* | ... | ... | ... | ... | LEFT tyres use R→I,L→O; RIGHT tyres use L→I,R→O |
| `brakeTempLF..RR` | `mBrakeTempCelsius[i]` | °C direct | 1.0 | 0 | |
| `brakeBiasSetting` | `mBrakeBias` (if ≥0) | normalized → % | 100.0 | 0 | Conditional: only if mBrakeBias >= 0 |
| `fuelLevel` | `mFuelLevel` | normalized(0-1) → % | 100.0 | 0 | |
| `oilTemp` | `mOilTempCelsius` | °C direct | 1.0 | 0 | |
| `waterTemp` | `mWaterTempCelsius` | °C direct | 1.0 | 0 | |
| `trackTemp` | `mTrackTemperature` | °C direct | 1.0 | 0 | |
| `ambientTemp` | `mAmbientTemperature` | °C direct | 1.0 | 0 | |
| `tcsActive` | `(mCarFlags & CAR_TCS)` ? 1 : 0 | bool → % (or 0/1) | — | — | **Non-linear**: bit test, not factor×value. Stays in C++ raw extraction as pre-computed value |
| `absActive` | `(mCarFlags & CAR_ABS)` ? 1 : 0 | same | — | — | Same pattern |
| `wheelSlipLF..RR` | `mTyreSlipSpeed[i]` | m/s → km/h | 3.6 | 0 | Slip SPEED not ratio (differs from EVO/PMR!) |

**AMS2 special cases that stay in C++ raw extraction:**
- Tyre temp L/R swap per wheel position (cannot be expressed as factor)
- TCS/ABS bit-flag → numeric conversion
- Brake bias conditional (`if >= 0`)
- `rideHeightUnit = Centimeters` assignment

---

### 6B: AC EVO / EvoM1 (`AcsEvoFeedLap`, lines ~227–310 of AcsEvoLiveCapture.cpp)

| LapStats field | Raw struct member(s) | Source unit → Target unit | Factor | Offset | Notes |
|-|-|-|-|-|-|
| `speed` | `phys.speedKmh` | km/h direct | 1.0 | 0 | Already in target units! |
| `rpm` | `phys.rpms` | RPM direct | 1.0 | 0 | |
| `throttle` | `phys.gas` | normalized(0-1) → % | 100.0 | 0 | |
| `brakePedal` | `phys.brake` | normalized(0-1) → % | 100.0 | 0 | |
| `steerAngle` | `phys.steerAngle` | turns(fraction) → deg | 360.0 | 0 | Same convention as AMS2? Verify |
| `yawRate` | `phys.localAngularVel[1]` | rad/s → deg/s | DEG_PER_RAD (57.2958) | 0 | Y-axis in EVO local frame |
| `latG` | `phys.accG[0]` | g direct | 1.0 | 0 | ⚠️ X axis = lateral in EVO (opposite of AMS2!) |
| `longG` | `phys.accG[1]` | g direct | 1.0 | 0 | Y axis = longitudinal in EVO |
| `wheelSpeedLF..RR` | `phys.wheelAngularSpeed[i]` | rad/s → km/h | kWheelRadiusM × 3600/(2π×1000) ≈ **derived** | 0 | Factor depends on wheel radius constant. Compute: if r=0.3m then factor = 0.3*3600/6283.2 ≈ 0.1719. VERIFY kWheelRadiusM value in code! |
| `brakeTempLF..RR` | `phys.brakeTemp[i]` | °C direct | 1.0 | 0 | |
| `pressureLF..RR` | `phys.wheelsPressure[i]` | PSI direct? or bar→PSI? | **VERIFY** | 0 | Current code has NO conversion factor — check if EVO reports in PSI natively (likely yes for AC) |
| `tyreInnerTemp*` / Centre / Outer | `phys.tyreTempI/M/O[i]` | °C direct | 1.0 | 0 | No L/R swap needed! Direct I/M/O per wheel index |
| `surfTempCF/CRF/CCLR/CRR` | Same as centre temps above | °C direct | 1.0 | 0 | Duplicate of tyreCentre — feeds front/rear diff checks |
| `brakeTorqueLF..RR` | `phys.brakeTorque[i]` | Nm direct? **VERIFY** | ? | 0 | Check if any conversion needed |
| `wheelSlipLF..RR` | `phys.slipRatio[i]` | ratio(0-1) → % | 100.0 | 0 | Slip RATIO (differs from AMS2 which uses slip SPEED!) |
| `tcsActive` | `phys.tcinAction ? 100 : 0` | bool→% | — | — | Non-linear, stays in C++ raw extraction as pre-computed value |
| `absActive` | `phys.absInAction ? 100 : 0` | same | — | — | Same pattern |

**EVO special cases that stay in C++:**
- TCS/ABS boolean → % (non-linear)
- Wheel speed derived factor depends on `kWheelRadiusM` constant (could be a JSON entry with the computed value, or stays as raw extraction if radius is per-car setup-dependent — **VERIFY**)

---

### 6C: PMR UDP (`PmrLiveCapture.cpp`, lines ~452–600)

| LapStats field | Raw packet member(s) | Source unit → Target unit | Factor | Offset | Notes |
|-|-|-|-|-|-|
| `speed` | `pkt.chassis.overallSpeed` | m/s → km/h | 3.6 (kMsToKmh) | 0 | |
| `rpm` | `pkt.drivetrain.engineRPM` | RPM direct | 1.0 | 0 | |
| `throttle` | `pkt.input.accelerator` × 100 if finite else 0 | normalized → % | 100.0 | 0 | isfinite guard stays in C++ raw extraction |
| `brakePedal` | `pkt.input.brake` × 100 if finite else 0 | same | 100.0 | 0 | Same pattern |
| `steerAngle` | `pkt.general.steeringWheelAngle` × DEG_PER_RAD if finite else 0 | rad → deg | 57.2958 (kDegPerRad) | 0 | **VERIFY**: radians vs degrees on first capture! |
| `yawRate` | `pkt.chassis.angularVelocityLS.y` × kDegPerRad | rad/s → deg/s | 57.2958 | 0 | Y-axis local space |
| `latG` | `pkt.chassis.accelerationLS.x / kGravity` if finite else 0 | m/s² → g | 1/9.81 = 0.10194 (kGravity) | 0 | X axis = lateral in PMR local space |
| `longG` | `pkt.chassis.accelerationLS.z / kGravity` if finite else 0 | m/s² → g | 0.10194 | 0 | Z axis = longitudinal (NOTE: same as AMS2, opposite of EVO!) |
| `wheelSpeedLF..RR` | `wX.linearSpeed × kMsToKmh` | m/s → km/h | 3.6 | 0 | Direct linear speed per wheel on wire — preferred over angular×radius |
| `brakeTempLF..RR` | `wX.brake` | °C direct | 1.0 | 0 | Per-wheel struct field named "brake" (confusing name, it's temp) |
| `pressureLF..RR` | `wX.pressure × kBarToPsi` | bar → PSI | ~14.5038 (kBarToPsi = 6.894757... wait: 1 bar = 14.5038 psi, so factor=14.5038) **VERIFY constant** | 0 | ⚠️ PMR reports in BAR; AMS2 in kPa — different source units! |
| `tyreInnerTemp*` / Centre / Outer | `wX.tread.x/y/z` with innerFirst logic | °C direct | 1.0 | 0 | **Non-trivial**: tread vector order depends on `PmrGetTreadInnerFirst()` flag — stays in C++ raw extraction as pre-resolved per-wheel values |
| `surfTempCF/CRF/CCLR/CRR` | Same middle-tread value | °C direct | 1.0 | 0 | Duplicate of centre temp (same pattern as EVO) |
| `wheelSlipLF..RR` | `wX.slipRatio × 100` | ratio → % | 100.0 | 0 | Slip RATIO like EVO, not speed like AMS2 |
| `suspVelLF..RR` | `wX.damperVelocity × kMToMm` | m/s → mm/s | 1000.0 (kMToMm) | 0 | **PMR's key advantage**: direct damper velocity on wire! Feeds DIRECT histograms |
| `susTravelLF..RR` + `damperX.addSample()` | `wX.springStrain × kMToMm` | m → mm (strain→travel approx) | 1000.0 | 0 | **Dual-use**: feeds both susTravel AND damper histogram via addSample(). This dual-write pattern needs special handling in the resolver — see note below |
| `brakeBiasSetting` | `pkt.setup.brakeBias × 100 if finite else 50` | normalized → % (default 50) | 100.0 | 0 | isfinite + default stays in C++ raw extraction |
| `tcsActive` | `drivetrain.tractionControlActive != 0 ? 100 : 0` | bool→% | — | — | Non-linear, pre-computed in C++ |
| `absActive` | `pkt.general.absActive != 0 ? 100 : 0` | same | — | — | Same pattern |
| `oilTemp` | `drivetrain.engineOilTemperature` | °C direct | 1.0 | 0 | Drivetrain float #11 on wire |
| `waterTemp` | `drivetrain.engineCoolantTemperature` | °C direct | 1.0 | 0 | Drivetrain float #12 |
| `fuelLevel` | `drivetrain.fuelRemaining / constant.fuelCapacity × 100 if valid else skip` | litres ratio → % | **DYNAMIC** (depends on fuelCap) | 0 | ⚠️ Cannot be a fixed factor — denominator changes per car. Stays in C++ raw extraction as pre-computed percentage value |

**PMR special cases that stay in C++:**
- All `isfinite()` guards with fallback values
- Tread vector order resolution (`PmrGetTreadInnerFirst()`) → pre-resolved I/M/O per wheel before dict is built
- Fuel level: dynamic denominator (fuelCapacity from constant block) — compute % in raw extraction, table just does factor=1.0
- `damperX.addSample(travel, time)` dual-write alongside susTravel — needs a special "dual-add" entry type or stays as post-table code

---

## 7. Validation Checklist (run after EACH phase)

After migrating each sim, verify ALL of the following:

### Build
- [ ] `bcc32c` per-TU compile clean on all modified files
- [ ] Full `msbuild Project1.cbproj /p:Config=Debug /t:Build` → 0W/0E
- [ ] No new warnings in build_log.txt

### Functional (per sim)
- [ ] Connect to live capture, run ≥2 laps
- [ ] Compare LapStats averages against pre-migration baseline for these key channels:
  - [ ] Speed (km/h): within ±0.1 of expected
  - [ ] RPM: exact match
  - [ ] Throttle/Brake %: within ±0.5%
  - [ ] Steer angle (deg): sign and magnitude correct
  - [ ] G-forces: lateral AND longitudinal, signs correct per sim axis convention
  - [ ] Tyre pressures: unit is PSI (not bar/kPa), values in expected range (~28-34)
  - [ ] Tyre temps inner/centre/outer: **orientation verified** (inner < centre for cambered tyre on track)
  - [ ] Brake temps: reasonable range, L/R symmetry plausible
  - [ ] Suspension travel mm / velocity mm/s: non-zero during driving
  - [ ] Wheel slip: correct unit (% vs km/h per sim convention!)

### Report integrity
- [ ] All report tabs populate correctly (tyre temp grid, brake temps, suspension histograms)
- [ ] No "insufficient data" warnings that weren't present before migration
- [ ] Rake virtual channel still computes (DataProvider path unchanged — should be unaffected)

### Edge cases
- [ ] Disconnect/reconnect works cleanly (table rebuilt on each connect)
- [ ] Missing channels in raw dict → silently skipped, no crash
- [ ] mapping.json missing an entry for a profile → validation warning shown at connect time

---

## 8. Rollback Strategy

If anything goes wrong during migration:

1. **Per-phase rollback**: Each phase is self-contained (one sim file + its JSON entries). To roll back Phase N, revert that one capture `.cpp`/`.h` to the pre-migration version and remove/skip those profile's LIVE rows in mapping.json. Other sims are unaffected.
2. **Git/VCS checkpoint before each phase**: Commit current working state BEFORE starting any phase. Tag: `pre-json-live-phase-{N}`.
3. **The old code is never deleted until Phase 4** — during Phases 1-3, the original hardcoded blocks can be kept as commented-out reference (or in a `_legacy` function) for side-by-side value comparison if needed.

---

## 9. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Wrong factor/offset migrated to JSON → silent data drift | Medium | High (wrong analysis results) | Cross-check every value against §6 tables; run validation checklist per phase; compare baseline values before/after |
| AMS2 tyre temp L/R swap lost in migration | Low (documented above) | High (camber diagnosis inverted!) | Swap stays in C++ raw extraction — explicitly called out as non-table-able. Unit test with known asymmetric temps |
| PMR `damperX.addSample()` dual-write not handled by table pattern | Medium | Medium (suspension histograms lose data for PMR) | Add a "dual-add" flag to ResolvedChannel, or keep the 4 damper addSample calls as post-table code in PmrLiveCapture.cpp. Decide during Phase 3 implementation |
| EVO wheel speed factor depends on per-car `kWheelRadiusM` (if it's not compile-time constant) | Low-Medium | Medium (wheel speeds wrong for some cars) | VERIFY: if kWheelRadiusM is a #define/const → bake into JSON. If runtime variable from car setup → keep in C++ raw extraction as pre-computed km/h value, table factor=1.0 |
| New `ChannelProfile::PMR` enum addition breaks existing profile matching logic | Low | Medium (file import for other sims affected) | Additive change only; verify all switch/if chains on ChannelProfile handle the new value gracefully (default case). Test file import after adding PMR to enum |

---

## 10. Estimated Effort & Sequencing

| Phase | Est. time | Dependencies |
|-------|-----------|--------------|
| 0: Foundation (resolver + feed function) | ~2-3 hours | None — pure new code, no existing files touched |
| 1: AMS2 migration | ~3-4 hours | Phase 0 complete; mapping.json LIVE rows audited/completed |
| 2: EVO migration | ~2-3 hours | Phase 0 (pattern established in Phase 1); create EVOM1 LIVE entries from scratch |
| 3: PMR migration | ~3-4 hours | Phases 0+1; add ChannelProfile::PMR if needed; handle dual-write + dynamic fuel factor edge cases |
| 4: Cleanup & docs update | ~1 hour | All three phases verified working in production use for ≥1 session each |

**Total**: ~2 days of focused work, spread across multiple sessions. Each phase is independently shippable — you can stop after Phase 1 and have AMS2 running on the new system while EVO/PMR remain hardcoded (no conflict).

---

## 11. Open Questions (verify during each phase's validation step — do NOT block on these before starting)

Most conversion errors are **self-evident** at verification time: a speed of "8" instead of 290 km/h, pressure at 400 PSI, steer angle of 1857° — impossible to miss visually. Use best-guess factors from §6 tables and let the per-phase validation checklist (§7) confirm or correct them naturally.

| # | Question | Where it gets resolved | Phase |
|---|----------|----------------------|-------|
| Q1 | What is `kWheelRadiusM` in EVO? Compile-time constant or runtime value from car setup? If compile-time → bake into JSON factor; if per-car → keep pre-computed km/h in C++ raw extraction, table factor=1.0 | Phase 2 implementation: just read the #define/variable when writing the raw-dict code |
| Q2 | Does PMR's `steeringWheelAngle` arrive in radians or degrees? If we apply ×57.3 to an already-degree value → full lock shows ~18,000° instead of 32°. **Instantly obvious if wrong.** | Phase 3 validation: glance at steer angle on first capture session |
| Q3 | What is `kBarToPsi` exact #define in PMR? Should be ≈14.5038. If off by a factor → pressure shows ~2 PSI or ~600 PSI instead of ~32. **Instantly obvious if wrong.** | Phase 3 implementation: read the constant from PmrLiveCapture.h when writing raw-dict code |
| Q4 | Does EVO report tyre pressure natively in PSI (no factor needed) or bar (needs ×14.5038)? Current code has NO conversion — **if it's already correct today, keep factor=1.0; if values look 14× off after migration → add the factor.** | Phase 2 validation: check pressure reads ~28–34 PSI on first EVO session |
| Q5 | Is there a "PMR" value in `ChannelProfile` enum already? Check when adding PMR entries. If not, it's a 1-line additive change + 2 lines in RuleConfig.cpp profile matching. | Phase 3 implementation: check MotecSetupAnalyzer.h before writing JSON entries |
| Q6 | Should file-import path also migrate to the same table (future phase)? Current flat alias maps work fine — no silent-gap risk since CSV headers are visible. **Default answer: leave as-is unless user requests otherwise.** | Phase 4 scope decision (optional) |

**⚠️ The ONE non-obvious case:** AMS2 tyre temp inner/outer orientation (§6A). If the L/R swap is lost in migration, all temps still look plausible (~85–105°C) — just with "inner" and "outer" labels swapped. **Verification method**: on a cambered racing setup during sustained cornering, inner should read COOLER than outer. Check this specifically after Phase 1 (see §7 checklist item under tyre temps).

---

## Appendix A: File Change Summary per Phase

```
Phase 0 (new files only):
  + LiveChannelResolver.h       (~80 lines: structs, function declarations)
  + LiveChannelResolver.cpp     (~150 lines: build(), validate(), FeedLapFromTable())
  
Phase 1 (AMS2):
  ~ Ams2LiveCapture.cpp         (replace Ams2FeedLap body with raw-dict + table call; add ResolvedChannelTable member)
  ~ Ams2LiveCapture.h           (add #include, member declaration)
  ~ MotecDataAnalyser.mapping.json   (complete/fix AMS2 LIVE entries: add missing channels like susTravel, wheelSlip, etc.)

Phase 2 (EVO):
  ~ AcsEvoLiveCapture.cpp       (same pattern as Phase 1)
  ~ AcsEvoLiveCapture.h         (member declaration)
  ~ MotecDataAnalyser.mapping.json   (add EVOM1 LIVE entries — new section, ~50 lines of JSON)

Phase 3 (PMR):
  ~ PmrLiveCapture.cpp          (same pattern; handle dual-write + fuel edge cases in raw extraction)
  ~ PmrLiveCapture.h            (member declaration)
  ~ MotecSetupAnalyzer.h        (add ChannelProfile::Pmr if not present — Q5)
  ~ RuleConfig.cpp              (update profile matching for new enum value, ~2 lines)
  ~ MotecDataAnalyser.mapping.json   (add PMR LIVE entries — new section, ~60 lines of JSON)

Phase 4 (cleanup):
  - RuleConfig.h/.cpp           (remove dead resolveRawToAlias if desired)
  ~ agents.md                   (update architecture doc data flow diagram)
```

---

*End of plan. Refer to this document at any point during implementation for the exact conversion factors, edge cases, and validation steps.*
