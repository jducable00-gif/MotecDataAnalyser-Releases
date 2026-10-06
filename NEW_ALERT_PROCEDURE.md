# Procedure: Creating New Diagnostic Alert/Warning Messages

This document outlines the step-by-step procedure for implementing a new diagnostic rule (alert or warning) within the software's decoupled architecture.

The system follows an **Alias → DataProvider → AnalysisRule → AnalysisResult** pattern to ensure that rules remain simulator-agnostic and easy to maintain.

---

## Overview of the Architecture

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

### Step 2: Add an `AnalysisAlias` Enum Value (if needed)
If no existing alias covers your channel:

1.  Open `MotecSetupAnalyzer.h`.
2.  Add a new value to the `AnalysisAlias` enum.
3.  Ensure `buildDataProviderFromLap()` in `analyze_motec_setup.cpp` populates it from `LapStats`.

### Step 3: Implement the Rule Class in `MotecSetupAnalyzer.h`
Create a new class inheriting from `AnalysisRule`:

```cpp
class MyNewRule : public AnalysisRule {
public:
    std::vector<AnalysisAlias> requiredInputs() const override {
        return { AnalysisAlias::MyChannel1, AnalysisAlias::MyChannel2 };
    }

    AnalysisResult evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        double val1 = provider.getValue(AnalysisAlias::MyChannel1);
        double val2 = provider.getValue(AnalysisAlias::MyChannel2);

        if (std::abs(val1 - val2) > settings.myThreshold) {
            AnalysisResult result;
            result.severity = Severity::Warning;
            result.category = "Tyre";
            result.title = tr("Temperature imbalance", "Déséquilibre de température");
            result.description = tr("Inner temp is 5°C higher than outer.", "La température interne est 5°C plus élevée que l'externe.");
            result.recommendation = tr("Adjust camber or pressure.", "Ajuster le carrossage ou la pression.");
            return result;
        }
        return AnalysisResult(); // empty = no issue
    }
};
```

### Step 4: Register the Rule
Add your rule to `buildDefaultAnalysisRegistry()` in `MotecSetupAnalyzer.h`:

```cpp
inline AnalysisRegistry buildDefaultAnalysisRegistry() {
    AnalysisRegistry registry;
    registry.addRule(std::unique_ptr<AnalysisRule>(new EngineTemperatureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new TyreTemperatureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new TyrePressureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new HandlingBalanceRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new MyNewRule())); // <-- add here
    return registry;
}
```

### Step 5: Add a Threshold (if needed)
If your rule uses a configurable threshold:

1.  Add a field to the `Thresholds` struct in `MotecSetupAnalyzer.h`.
2.  Add a default value in `g_thresholds` initialization.
3.  Add load/save logic in `MotecSettingsForm.cpp` (the threshold editor dialog).
4.  The threshold will be persisted in `MotecDataAnalyser.thresholds.ini`.

### Step 6: Verification & Testing
1.  **Compile**: Ensure no syntax errors were introduced.
2.  **Data Check**: Run the software with a telemetry file known to trigger this condition.
3.  **UI Check**: Verify that the alert appears in the report tabs (`MotecSetupForm`).

## Unit Testing

To ensure that new rules and aliases are implemented correctly without manual verification in the UI, it is highly recommended to implement unit tests.

### What to Test
1.  **Alias Resolution**: Verify that `RuleConfig::resolveAlias` returns the correct raw channel for a given semantic alias across different profiles (Generic vs EvoM1).
2.  **Rule Logic**: Verify that your `AnalysisRule::evaluate()` correctly triggers based on specific input values and threshold settings.
3.  **Registry Integrity**: Ensure that adding new rules does not affect existing rules or cause lookup failures.

### Example Test Case (Conceptual)
```cpp
TEST(RuleConfigTest, ResolvesCorrectAlias) {
    RuleConfig& config = RuleConfig::instance();
    std::string outKey;
    bool resolved = config.resolveAlias(ChannelProfile::Generic, "TYRE_TEMP_FL_INNER", outKey);
    EXPECT_TRUE(resolved);
    EXPECT_EQ(outKey, "TYRE_INNER_TEMP_LF");
}
```

---

## Summary Checklist for Agents

- [ ] **Aliases**: Are all telemetry channels mapped to semantic names in `mapping.json`?
- [ ] **Enum**: Is the `AnalysisAlias` value defined and populated in `buildDataProviderFromLap()`?
- [ ] **Rule Class**: Does the new `AnalysisRule` subclass correctly use `DataProvider::getValue()`?
- [ ] **Registration**: Is the rule added to `buildDefaultAnalysisRegistry()`?
- [ ] **Threshold**: Is the threshold configurable via `Thresholds` / `thresholds.ini`?
- [ ] **Localization**: Are all user-facing strings wrapped in `tr(...)` for English/French support?
