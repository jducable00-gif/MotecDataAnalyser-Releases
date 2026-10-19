# Procedure: Creating New Diagnostic Alert/Warning Messages

This document outlines the step-by-step procedure for implementing a new diagnostic rule (alert or warning) within the software's decoupled architecture.

The system follows an **Alias → DataProvider → AnalysisRule → AnalysisResult** pattern to ensure that rules remain simulator-agnostic and easy to maintain.

---

## Overview of the Architecture

### Core Design Constraint: Rules Are Profile-Agnostic via Aliases

> ⚠️ **NEVER reference a raw channel name (e.g., `C_SURF_TEMP_LF`, `S_TYRE_P_FL`) inside rule logic.**
> Rules interact exclusively with `AnalysisAlias` enum values through `DataProvider::getValue()`. The alias layer is what makes rules simulator-agnostic: the same rule works identically for AMS2, AC EVO, EvoM1, or any future profile without modification. All profile-specific knowledge lives in `mapping.json`, not in C++ code.

This separation means adding a new simulator only requires a new mapping entry — zero changes to existing rules.

### Data Flow Layers

1.  **Channel Aliasing (`RuleConfig` + `mapping.json`)**: Raw telemetry channels (e.g., `C_SURF_TEMP_LF`) are mapped to semantic aliases (e.g., `TYRE_TEMP_FL_INNER`). This allows rules to work regardless of whether the data comes from AMS2, EvoM1, or other simulators.
2.  **DataProvider**: A gateway that resolves `AnalysisAlias` enum values to concrete numeric values from a `LapStats` object, including virtual channels (e.g., `Rake`).
3.  **AnalysisRule (in `MotecSetupAnalyzer.h`)**: Each rule is a class implementing `requiredInputs()` and `evaluate(provider, settings)`. Rules are registered in `buildDefaultAnalysisRegistry()`.
4.  **AnalysisResult**: When an issue is detected, the rule produces an `AnalysisResult` containing severity, category, title, description, and recommendation.

---

## Implementation Steps

### Step 1: Define or Verify Channel Aliases
Before writing logic, ensure the telemetry channels required for your rule are available as **Aliases**.

1.  Open `MotecDataAnalyser.mapping.json` (and/or `RuleConfig.cpp` for code-level defaults).
2.  Check if your required raw channels exist in the `generic` profile or a simulator-specific profile.
3.  If they don't exist, add a new mapping entry.
4.  **Note**: If you are using existing aliases (like `TYRE_TEMP_FL_INNER`), skip this step.

**Two mapping types — know which to use:**

| Type | When to use | Profile field? | Example in JSON |
|------|------------|----------------|-----------------|
| **Raw** (`"type":"raw"` or omitted) | The simulator exposes the value directly (LD file channel or shared-memory). One entry per profile + source. | Yes: `"profile":"AMS2"` / `"EVOM1"` and `"source":"MOTEC"` / `"LIVE"` | `{"alias":"Yaw_Rate","profile":"AMS2","source":"LIVE","channel":"mAngularVelocity[1]",...}` |
| **Virtual** (`"type":"virtual"`) | The value is *derived* from other aliases (e.g., a simulator doesn't expose it directly). Defined once globally — no profile needed because its inputs are themselves resolved per-profile. | No | `{"type":"virtual","alias":"Rake","operation":"difference_of_averages","positiveInputs":["Ride_Height_FL",...],...}` |

**Resolution order in `DataProvider`:** When a rule calls `getValue(alias)`, the provider first checks if a raw value was populated for that alias (from LD file or shared memory). If not found, it falls back to virtual channel computation. This means:
- A profile with direct data uses its raw mapping automatically — no extra work needed.
- A profile lacking that data can still be served by a global virtual definition as fallback.

So if Profile X has the sensor but Profile Y doesn't, you add one raw entry for X and optionally one virtual derivation (from other available aliases) to cover Y transparently.

**To a rule, both sources are invisible.** A rule always calls `provider.getValue(alias)` and receives a plain number — it never knows or cares whether that value came from a direct LD/shared-memory read (raw mapping) or was computed by the DataProvider via virtual derivation. The alias *is* the sole entry point for all logic and recommendations; there is no second path, no type-checking on origin, no branching between raw and virtual in rule code.

### Step 2: Add an `AnalysisAlias` Enum Value (if needed)
If no existing alias covers your channel:

1.  Open `MotecSetupAnalyzer.h`.
2.  Add a new value to the `AnalysisAlias` enum (~line 240).
3.  Ensure `buildDataProviderFromLap()` in **`MotecSetupAnalyzer.h`** (~line 1175) populates it from `LapStats`:

```cpp
// For StatSummary fields (average of samples):
addSummaryValue(provider, AnalysisAlias::MyNewChannel, lap.myStat);

// Or for computed values:
provider.setValue(AnalysisAlias::MyComputed, someExpression * unitConversion);
```

### Step 3: Implement the Rule Class in `MotecSetupAnalyzer.h`
Create a new class inheriting from `AnalysisRule`:

**Copy this template exactly — it matches current conventions:**

```cpp
class MyNewRule : public AnalysisRule {
public:
    std::vector<AnalysisAlias> requiredInputs() const override {
        return { AnalysisAlias::MyChannel1, AnalysisAlias::MyChannel2 };
    }

    // Returns a VECTOR — one rule can emit multiple findings per lap.
    std::vector<AnalysisResult> evaluate(const DataProvider& provider,
                                         const AnalysisSettings& settings) const override {
        // Local tr() lambda is defined at the top of EVERY rule's evaluate().
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        double val1 = 0.0, val2 = 0.0;
        // getValue() returns bool + out-ref — guard with early return if false:
        if (!provider.getValue(AnalysisAlias::MyChannel1, val1))
            return {};   // missing data → no finding (registry skips)
        if (!provider.getValue(AnalysisAlias::MyChannel2, val2))
            return {};

        std::vector<AnalysisResult> results;

        if (std::abs(val1 - val2) > settings.engine.myThresholdC) {  // nested struct access!
            AnalysisResult r;
            r.severity         = AnalysisSeverity::Warning;   // Info / Warning / Critical
            r.category         = "Tyre";                       // see Step 5 for valid values
            r.title            = tr("Temperature imbalance", "Déséquilibre de température");
            r.description      = tr(std::to_string(val1 - val2) + "\xC2\xB0C difference detected.",
                                    std::string("\xC3\x89cart : ") + std::to_string(val1 - val2) + " \xC2\xB0C.");
            r.recommendation   = tr("Adjust camber or pressure.",
                                    "Ajuster le carrossage ou la pression.");
            results.push_back(std::move(r));
        }

        return results;  // empty vector → no finding for this lap
    }
};
```

**Key conventions:**
- **Profile-agnostic**: Only access data via `AnalysisAlias` enum values through `provider.getValue(alias, out)`. Never hardcode raw channel names — the alias layer (`mapping.json`) handles all simulator-specific mapping transparently.
- `evaluate()` returns a **vector**, not a single result. Return `{}` (empty) when there's nothing to report — the registry filters these out automatically.
- The local `tr` lambda is defined at the top of every rule's `evaluate()`. It selects English/French based on `settings.language`. Always use it for user-facing strings.
- `provider.getValue(alias, out)` returns **bool** + output reference. Never assume a value exists just because it was in `requiredInputs()`; live data can be sparse even after the registry pre-checks.
- Severity enum: `AnalysisSeverity::Info`, `AnalysisSeverity::Warning`, `AnalysisSeverity::Critical`.

### Step 4: Register the Rule
Add your rule to `buildDefaultAnalysisRegistry()` at ~line 1219 of `MotecSetupAnalyzer.h`:

```cpp
inline AnalysisRegistry buildDefaultAnalysisRegistry() {
    AnalysisRegistry registry;
    registry.addRule(std::unique_ptr<AnalysisRule>(new EngineTemperatureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new TyreTemperatureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new TyreSuspensionBalanceRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new HandlingBalanceRule()));
    // Add your new rule here:
    registry.addRule(std::unique_ptr<AnalysisRule>(new MyNewRule()));
    return registry;
}
```

> **Note:** Your rule must be added via `registry.addRule(...)` alongside the other four so it participates in every lap evaluation automatically. No changes needed at call sites (`evaluateLapWithRules` / `registry.evaluateAll`).

**Verify Category Mapping:** The `category` string you set on each result determines which report tab displays it (via inline function `mapAnalysisResultToReportSection()` ~line 403, case-insensitive). Recognized values:
- `"Engine"`, `"Cooling"` → Engine/Cooling Tab
- `"Tyre"`, `"Tires"` → Tyres Tab
- `"Brake"`, `"Brakes"` → Brakes Tab
- `"Suspension"`, `"Damper"` → Suspension/Damper Tab
- `"Handling"`, `"Steering"`, `"Traction"` → Handling/Setup Tab
- Anything else (or empty) → General tab

If you need a genuinely new report section, that requires UI changes in `MotecSetupForm` — coordinate before choosing an unmapped category.

### Step 5: Add a Threshold (if needed)
The threshold system is **two-layered**. A rule never reads the global directly; it accesses values through the per-call nested settings struct built by `buildDefaultAnalysisSettings()`. Follow all sub-steps in order:

1. **Global default** — add field to the `Thresholds` struct and its initializer near line 40 of `MotecSetupAnalyzer.h`:
   ```cpp
   double myNewLimitC = 95.0;   // inside Thresholds / g_thresholds init block
   ```

2. **Nested settings struct** — add a matching field (with the same default) to whichever nested struct fits your domain: `TyreThresholds`, `EngineThresholds`, or `HandlingThresholds` (~line 218+). If none fit, create one and attach it to `AnalysisSettings`.

3. **Wire in buildDefaultAnalysisSettings()** — at ~line 1152 of `MotecSetupAnalyzer.h`, copy the global into the nested struct:
   ```cpp
   settings.engine.myNewLimitC = g_thresholds.myNewLimitC;
   // (or settings.tyre.*, settings.handling.* as appropriate)
   ```

4. **INI persistence** — in `MotecSettingsForm.cpp` add a read/write pair using the existing pattern:
   ```cpp
   ini->ReadFloat("Thresholds", "MyNewLimitC", g_thresholds.myNewLimitC);  // load
   ini->WriteFloat("Thresholds", "MyNewLimitC", g_thresholds.myNewLimitC); // save
   ```

5. **Optional: expose in the settings dialog** — add a labeled edit control on one of `MotecSettingsForm`'s tabs so users can adjust it without hand-editing `.thresholds.ini`. Not required for functionality; rules work with defaults even if no UI is added.

The persisted file is always exe-relative: `MotecDataAnalyser.thresholds.ini`. A restart (or re-open of the settings dialog) picks up changes — there's no hot-reload path today, so document that in any user-facing notes you add to the report text.

### Step 6: Verification & Testing
1. **Compile**: Build via `build.ps1` (MSBuild + rsvars.bat). Ensure zero new warnings/errors.
2. **Data Check**: Run with a telemetry file known to trigger this condition; confirm the alert appears in the correct tab of the report (`MotecSetupForm`).
3. **Localization check**: Toggle ReportLanguage between English and French in `MotecDataAnalyser.ini` (or via UI if exposed) and verify both strings render correctly, including any embedded numeric values built with `std::to_string`.

> There is no automated unit-test infrastructure for this project — the build system is a single C++ Builder 12 VCL Win32 `.cbproj`, so manual verification against real telemetry data is currently the standard. If you need to test an edge case that's hard to reproduce in live capture, temporarily lower (or raise) the relevant threshold value directly in `MotecDataAnalyser.thresholds.ini` next to the exe and re-run; remember to restore it before committing any sample INI files.

---

## Summary Checklist for Agents

- [ ] **Aliases**: Are all telemetry channels mapped to semantic names in `mapping.json` (or code defaults)?
- [ ] **Enum + population**: Is the new `AnalysisAlias` value defined AND populated inside inline `buildDataProviderFromLap()` (~line 1175 of MotecSetupAnalyzer.h) using either `addSummaryValue(...)` for StatSummary fields or direct `provider.setValue(alias, computed * unitConversion)`?
- [ ] **Rule class**: Does the new subclass return a **vector** from `evaluate()`, define its own local `tr` lambda at the top of that function body, guard every `getValue()` call with an early `{}` return on false, and use nested settings access (e.g. `settings.engine.myThresholdC`) rather than flat field names?
- [ ] **Registration**: Is the rule's unique_ptr added to the same collection inside `buildDefaultAnalysisRegistry()` (~line 1219) as the other four rules?
- [ ] **Category mapping**: Does your chosen category string match one of the recognized values in `mapAnalysisResultToReportSection()`, or have you coordinated a UI change for a new tab?
- [ ] **Threshold (if applicable)**: All five sub-steps done — global field, nested struct field, wiring line in `buildDefaultAnalysisSettings()` (~line 1152), INI read/write pair in MotecSettingsForm.cpp, and optional dialog control.
- [ ] **Localization**: Every user-facing string goes through the local `tr(en, fr)` lambda; numeric values are interpolated with `std::to_string` before/after the translated text so both languages stay correct.
