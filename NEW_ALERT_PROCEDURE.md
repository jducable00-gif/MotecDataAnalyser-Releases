# Procedure: Creating New Diagnostic Alert/Warning Messages

This document outlines the step-by-step procedure for implementing a new diagnostic rule (alert or warning) within the software's decoupled architecture. 

The system follows an **Alias $\rightarrow$ Rule Engine $\rightarrow$ Structured Recommendation** pattern to ensure that rules remain simulator-agnostic and easy to maintain.

---

## Overview of the Architecture

1.  **Channel Aliasing (`RuleConfig`)**: Raw telemetry channels (e.g., `C_SURF_TEMP_LF`) are mapped to semantic aliases (e.g., `TYRE_TEMP_FL_INNER`). This allows rules to work regardless of whether the data comes from AMS2, EvoM1, or other simulators.
2.  **Rule Registry (`RuleConfig`)**: A collection of `RuleSymptom` objects that define what a "symptom" is, its potential causes (with weights), and which channels are needed to validate it.
3.  **Analysis Engine (`RecommendationEngine`)**: The logic layer that evaluates telemetry data against thresholds and mathematical conditions.
4.  **Structured Recommendations (`Recommendation`)**: When an issue is detected, the engine produces a `Recommendation` object containing:
    *   **Symptom ID**: Links to the registry for causes/adjustments.
    *   **Severity & Confidence**: Calculated based on how much the threshold was exceeded and data quality.
    *   **Evidence**: Human-readable strings explaining *why* the alert triggered (e.g., "Inner temp is 5°C higher than outer").
    *   **Direction/Suggested Change**: The corrective action (e.g., "Reduce camber").

---

## Implementation Steps

### Step 1: Define or Verify Channel Aliases
Before writing logic, ensure the telemetry channels required for your rule are available as **Aliases**.

1.  Open `RuleConfig.cpp`.
2.  Check if your required raw channels exist in the `genericTable` (for all simulators) or `evoTable` (specifically for EvoM1).
3.  If they don't exist, add a new mapping:
    ```cpp
    // Example: Adding a new alias for brake pressure
    {"BRAKE_PRESSURE_RAW", "BRAKE_PRESS"},
    ```
4.  **Note**: If you are using existing aliases (like `TYRE_TEMP_FL_INNER`), skip this step.

### Step 2: Register the Symptom in `RuleConfig`
Define the "Symptom" which acts as a container for the diagnostic knowledge.

1.  In `RuleConfig::buildDefaults()`, create a new `RuleSymptom` object.
2.  Assign a unique, descriptive `id` (e.g., `tyre_temp_imbalance`).
3.  Add **Causes** using `addCause(rule, cause_id, weight, adjustment_action, direction)`. 
    *   `weight`: Importance of this cause ($0.0$ to $1.0$).
    *   `direction`: `"increase"`, `"decrease"`, `"stiffer"`, `"softer"`, etc.
4.  Add **Validation Channels** that the engine should check for data quality/presence.

**Example Implementation:**
```cpp
{
    RuleSymptom r;
    r.id = "tyre_temp_imbalance"; // The unique ID used by the engine
    addCause(r, "excessive_camber", 0.70, "adjust_camber", "increase");
    addCause(r, "low_pressure", 0.30, "increase_pressure", "increase");
    r.validationChannels.push_back("TYRE_TEMP_FL_INNER");
    r.validationChannels.push_back("TYRE_TEMP_FL_OUTER");
    m_rules.push_back(r);
}
```

### Step 3: Implement the Detection Logic in `RecommendationEngine`
Now, write the mathematical logic that triggers this symptom.

1.  Open `RecommendationEngine.cpp`.
2.  Create a new function (or add to an existing analysis block) that performs the calculation.
3.  Use the **Aliases** defined in Step 1 via the `RuleConfig` instance.
4.  If the condition is met:
    *   Set `rec.issue = true`.
    *   Assign `rec.symptom = "your_new_symptom_id"`.
    *   Calculate `rec.confidence` using `computeConfidence(...)`.
    *   Populate `rec.evidence` with the specific values that triggered the alert.

**Example Implementation:**
```cpp
// Inside an analysis function (e.g., buildTyreRecommendation)
double inner = getAliasValue("TYRE_TEMP_FL_INNER");
double outer = getAliasValue("TYRE_TEMP_FL_OUTER");
double threshold = 5.0; // degrees Celsius

if (std::abs(inner - outer) > threshold) {
    rec.issue = true;
    rec.symptom = "tyre_temp_imbalance";
    rec.confidence = computeConfidence(std::abs(inner - outer), threshold, sampleCount, 1, 2, 2);
    rec.evidence.push_back("Temp difference: " + formatDouble(std::abs(inner - outer), 1) + "°C");
    
    // Set the human-readable text
    rec.text = tr("Tyre temperature imbalance detected.", "Déséquilibre de température des pneus.");
    rec.direction = tr("Adjust camber or pressure", "Ajuster le carrossage ou la pression");
    
    applyRuleToRecommendation(rec, rec.symptom); // Pulls causes/adjustments from RuleConfig
}
```

### Step 4: Verification & Testing
1.  **Compile**: Ensure no syntax errors were introduced in `RuleConfig` or `RecommendationEngine`.
2.  **Data Check**: Run the software with a telemetry file known to trigger this condition (e.g., a setup with extreme camber).
3.  **UI Check**: Verify that the alert appears in the UI (`MotecSetupForm`) and that the "Evidence" and "Suggested Changes" match your implementation.

## Unit Testing

To ensure that new rules and aliases are implemented correctly without manual verification in the UI, it is highly recommended to implement unit tests.

### What to Test
1.  **Alias Resolution**: Verify that `RuleConfig::resolveAlias` returns the correct raw channel for a given semantic alias across different profiles (Generic vs EvoM1).
2.  **Mathematical Logic**: Use a testing framework (like GoogleTest) to verify that the logic in `RecommendationEngine` correctly calculates confidence and triggers symptoms based on specific input values.
3.  **Registry Integrity**: Ensure that adding new `RuleSymptom` entries does not affect existing rules or cause lookup failures.

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

- [ ] **Aliases**: Are all telemetry channels mapped to semantic names?
- [ ] **Registry**: Is the `RuleSymptom` added to `RuleConfig::buildDefaults()`?
- [ ] **Logic**: Does the math in `RecommendationEngine` correctly use those aliases?
- [ ] **Mapping**: Does `rec.symptom` match the ID used in `RuleConfig`?
- [ ] **Localization**: Are all user-facing strings wrapped in `tr(...)` for English/French support?
