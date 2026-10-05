# Agent Instructions: Motec Data Analyser

This file provides context and operational guidelines for AI agents working on the Motec Data Analyser project, specifically regarding the new diagnostic recommendation architecture.

## 1. Core Architecture Overview
The system uses a decoupled, three-layer pattern to ensure simulator-agnostic analysis:

1.  **Data Mapping Layer (`RuleConfig` / `DataProvider`)**: Maps raw telemetry channels (e.g., `mRpm`, `TYRE_TEMP_LF`) to semantic **Aliases** (e.g., `Engine_RPM`, `TYRE_TEMP_FL_INNER`). This layer handles unit conversions and simulator-specific profiles.
2.  **Rule Registry Layer (`RuleConfig`)**: A central registry of `RuleSymptom` objects. Each symptom defines:
    *   A unique ID.
    *   Potential **Causes** with associated weights ($0.0$ to $1.0$).
    *   Corrective actions and directions (e.g., "increase camber").
3.  **Recommendation Engine Layer (`RecommendationEngine`)**: The mathematical logic layer. It evaluates telemetry data against thresholds using the Aliases, calculates a **Confidence Score**, and generates structured `Recommendation` objects containing evidence and suggested changes.

## 2. Key Documentation
Agents should refer to these files for specific tasks:
*   [DATA_MAPPING_SPEC.md](./DATA_MAPPING_SPEC.md): Detailed technical specification of the mapping layer and data flow.
*   [NEW_ALERT_PROCEDURE.md](./NEW_ALERT_PROCEDURE.md): Step-by-step implementation guide for creating new diagnostic rules.

## 3. Operational Guidelines for Agents

### When implementing a NEW rule:
1.  **Verify Aliases**: Check `RuleConfig.cpp` to ensure all required channels are mapped as semantic aliases. If not, add them in the mapping table first.
2.  **Register Symptom**: Add the new `RuleSymptom` to `RuleConfig::buildDefaults()`, defining its ID, causes, and validation channels.
3.  **Implement Logic**: Write the mathematical evaluation in `RecommendationEngine.cpp`. Use semantic aliases via the data provider/config instance.
4.  **Ensure Localization**: Wrap all user-facing strings (evidence, text, direction) in `tr()` for English/French support.

### When debugging existing rules:
1.  Trace from the **Symptom ID** back to its definition in `RuleConfig`.
2.  Verify that the mathematical logic in `RecommendationEngine` is using the correct semantic aliases and not raw channel names.
3.  Check if the confidence calculation correctly accounts for data quality/sample counts.

### When writing Unit Tests:
*   Focus on **Alias Resolution** (ensuring mapping works).
*   Focus on **Mathematical Logic** (verifying thresholds and confidence scores in `RecommendationEngine`).
*   Focus on **Registry Integrity** (ensuring new rules don't break existing ones).
