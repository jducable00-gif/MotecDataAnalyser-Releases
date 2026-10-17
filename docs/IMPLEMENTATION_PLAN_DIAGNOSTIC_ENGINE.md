# Implementation Plan — Multi-Signal Diagnostic & Setup Recommendation Engine

> **Spec:** [`specification_agent_diagnostic_multisignal_setup.md`](./specification_agent_diagnostic_multisignal_setup.md)  
> **Status:** Ready for implementation (phased delivery agreed 2026-10-17)  
> **Scope decisions:** Phased · Plain-text Diagnostics tab v1 · All three profiles with graceful degradation

---

## TL;DR — What already exists vs. what to build

| Already in codebase | To build / extend |
|---|---|
| `RuleConfig::buildDefaults()` → 10 symptoms × weighted causes (`CauseEntry{cause, weight, adjustment, direction}`) + validation channels | Symptom **detectors** that consume live evidence and emit findings |
| Dead-pipeline structs: `Recommendation`, `CauseEntry` in `MotecSetupAnalyzer.h`; confidence formula documented in README_FR.md; thresholds still in `g_thresholds` (`confidenceLow/Medium/High = 0.30/0.60/0.85`, `minLapsForReproducibility=3`) | Re-wired **cause ranking + confidence scoring** producing LOW/MEDIUM/HIGH labels (no numeric %) |
| Per-frame phase classification: `LapStats::addHandlingSample()` runs on all 3 ingestion paths, already buckets cornerEntry/cornerExit with understeer/oversteer indicators | Extended to full driving-phase enum (straight / braking approach / initial brake / entry / mid-corner / exit) + per-phase accumulators |
| Rich `LapStats` evidence: 4× damper velocity histograms (`VelocityHistogram`, 41 bins), steer/yaw Pearson correlation, suspension asymmetry sums, wheel slip ×4, brake torque/bias/TCS-ABS, aero/LSD, pitch/roll (EVO) | Exposed through **new `AnalysisAlias` values** + extended `buildDataProviderFromLap()` so rules can read them via the provider contract |
| 4 active rules (`EngineTemperatureRule`, `TyreTemperatureRule`, `TyrePressureRule`, `HandlingBalanceRule`) registered in `buildDefaultAnalysisRegistry()` | New **separate diagnostic registry** — existing 4 rules untouched, non-regression guaranteed |
| Report pipeline: `ReportSection` enum (7 values), `UpdateReportTabs()`, tablet JSON writer | New `ReportSection::Diagnostics` + plain-text tab following the existing memo pattern |

---

## Phase 0 — Signal exposure through DataProvider *(foundation; blocks all later phases)*

**Goal:** Make every piece of evidence in `LapStats` reachable by rules via the alias gateway. No mapping.json changes needed (these are derived lap-level aggregates, not raw channels).

### Steps

1. **Extend `enum AnalysisAlias`** (`MotecSetupAnalyzer.h`, ~line 260–300) with:
   - Handling evidence: `SteerYawCorrelation`, `UndersteerIndicatorsEntry`, `OversteerIndicatorsExit`, `HandlingSampleCount`
   - Wheels/brakes: per-corner wheel slip ×4 (replacing LF-only/LR-only front/rear averages), `BrakeTorqueFrontAvg/RearAvg`, `BrakeBiasSetting`, `TcsActivePct`, `AbsActivePct`, `WheelSpeedFrontAvg/RearAvg`
   - Suspension: `SusTravelMeanLF..RR`, `BumpStopEngagementLF..RR`, damper histogram summaries (asymmetry %, high-speed share %) via existing `pickDamperHistogram()` + zone helpers
   - Vehicle/aero: `AeroBalance`, `LsdTorque`; EVO-only pitch/roll means; vertical-load averages where mapped

2. **Extend `buildDataProviderFromLap(lap)`** (`MotecSetupAnalyzer.h` ~line 1286) to fill the new aliases from corresponding `LapStats` fields (`.average()`, histogram helpers, correlation). Follow existing pattern exactly — no virtual channels needed here.

3. **Channel-availability descriptor per rule:** each diagnostic rule declares:
   ```cpp
   struct ChannelRequirements {
       std::vector<AnalysisAlias> required;    // all must be present → else abstain entirely
       std::vector<AnalysisAlias> optional;    // missing reduces confidence, recorded on finding
   };
   ```
   Evaluation checks via `DataProvider::hasValue()` and records which channels were absent (spec §6 traceability: "why this rule didn't run / reduced confidence").

### Acceptance criteria
- All new aliases resolvable through `buildDataProviderFromLap` for a fixture lap with full data.
- A profile-missing channel (e.g., PMR has no ride height) → alias absent from provider, not zero-filled.
- Existing 4 rules produce **byte-identical** output on the same input (non-regression).

---

## Phase 1 — Symptom detection + multi-signal cause ranking *(Niveaux A & B)*

### Steps

4. **New types in `MotecSetupAnalyzer.h`** (next to existing dead-pipeline structs, do NOT modify them):
   ```cpp
   struct DiagnosticFinding {
       std::string symptomId;           // matches RuleConfig catalog id
       AnalysisSeverity severity;
       double confidence = 0.0;         // internal score [0..1]
       std::string dataQualityLabel;    // "LOW" / "MEDIUM" / "HIGH" (from g_thresholds)
       std::vector<std::string> evidence;   // concrete measurements, tr()-localized
       std::vector<CauseEntry> rankedCauses;// sorted by adjusted weight desc
       std::vector<AnalysisAlias> missingChannels;  // traceability
       std::vector<std::string> confounders;        // e.g. "ABS active on 40% of braking samples"
       std::string phaseWindow;         // human-readable: "corner exit, right-hand corners only"
   };

   class DiagnosticRule {
   public:
       virtual ~DiagnosticRule() = default;
       virtual ChannelRequirements channelRequirements() const = 0;
       virtual DiagnosticFinding evaluate(const DataProvider& provider,
                                          const AnalysisSettings& settings) const; // empty finding if insufficient data
   };

   struct DiagnosticRegistry {
       std::vector<std::unique_ptr<DiagnosticRule>> rules;
       std::vector<DiagnosticFinding> evaluateAll(const DataProvider&, const AnalysisSettings&) const;
   };
   ```
   Separate from `AnalysisRegistry` — existing 4 rules stay in their own registry, untouched.

5. **Confidence scoring** (re-implement per README_FR.md formula):
   - Threshold proximity: 40% × min(1, |value|/threshold − 1) for the primary indicator
   - Sample count: 25% × min(1, n / g_thresholds.minHandlingSamples)
   - Lap reproducibility: 20% × min(1, lapsReproduced / g_thresholds.minLapsForReproducibility) — requires multi-lap input; single-lap → cap at MEDIUM
   - Channel coverage: 15% × (available required channels / total declared)
   - Forced to 0 if any **required** channel missing. Label via `g_thresholds.confidenceLow/Medium/High`.

6. **Cause ranking:** start from `RuleConfig::instance().findRule(symptomId)` base weights; modulate each cause's weight by evidence strength on its validation channels (present + concordant → boost up to ×1.5; present but contradictory → dampen ×0.5). Always output the full ranked list — never collapse to a single conclusion. Dangling aliases (`front_slip_angle`, `rear_slip_angle` — unmapped in all profiles) simply contribute no modulation.

7. **Symptom detectors** (one class each, registered in new `buildDefaultDiagnosticRegistry()`):

   | Detector class | Symptom id (catalog #) | Primary evidence consumed |
   |---|---|---|
   | `RearTractionLossOnExitRule` | `rear_traction_loss_on_exit` (#1) | Rear wheel slip + throttle phase stats (`HandlingPhaseStats.cornerExit`) + yaw rate; tyre temp/pressure context from existing rule results |
   | `FrontUndersteerEntryRule` | `front_understeer_entry` (#2), `front_understeer_exit` (#9) | Existing understeer indicator counts (cornerEntry bucket), steer/yaw correlation vs `g_thresholds.steerYawCorrUndersteer`, latG, tyre temp/pressure evidence as context |
   | `RearOversteerRule` | `rear_oversteer_entry` (#10) + exit variant | Oversteer indicators + yaw-rate spikes with low steering input (existing thresholds: `yawMinOversteer=15°/s`, `steerChangeMaxOversteer`) |
   | `BrakeLockupInstabilityRule` | `brake_instability` (#4) | Per-corner wheel-speed divergence **where true per-wheel data exists**; ABS/TCS active % as context/confounder. AMS2: its "wheel speeds" are ground-speed proxies → declare channel optional/absent for AMS2 profile (graceful degradation, no false positives) |
   | `BumpstopEngagementRule` | `bumpstop_engagement` (#3) | Existing bumpStop stats + travel max vs `g_thresholds.suspensionTravelMax=8mm`; ride height context via Rake virtual channel where available (PMR: absent → reduced confidence) |
   | `DamperAsymmetryRule` / `DamperHighSpeedShareRule` | #5–#8 | Consume results of existing `detail::evaluateDamperCorner()` as evidence inputs — **do not recode** the damper analysis; read its output (asymmetry %, high-speed share, static-suspension flag) and feed into cause ranking |
   | `ThermalDegradationRule` *(stub)* | new id: `thermal_degradation_across_laps` | Lap-over-lap deltas via existing `LapDelta` fields + temp trends. Requires multi-lap input → full implementation in Phase 4 comparison context; stub now returns empty finding for single lap |

   Each finding carries: phase/window description, concrete measurements as evidence strings (`tr()`-localized EN/FR), missing channels list, confounders (e.g., "ABS active on 38% of braking samples — lockup detection unreliable").

### Acceptance criteria
- For a fixture lap with known understeer pattern → `FrontUndersteerEntryRule` emits finding with confidence ≥ MEDIUM, ranked causes matching catalog #2 order.
- AMS2 profile: brake-lockup rule reports missing-channel note (ground-speed proxy), does NOT emit false positive.
- PMR profile: bumpstop rule runs at reduced confidence (no ride height/Rake).
- All 10 catalog symptoms have a detector class; unimplemented ones return empty finding with traceability reason.

---

## Phase 2 — Driving-phase segmentation + asymmetry detection *(spec §2, §4)*

### Steps

8. **Extend per-frame classifier** `LapStats::addHandlingSample()` (~line 1250) from {braking, accelerating, cornering} to full phase enum:
   ```cpp
   enum class DrivingPhase : uint8_t {
       Unknown = 0,        // insufficient data — never guessed (spec §2)
       StraightStableAccel,// throttle > threshold, low latG, no brake
       BrakingApproach,    // brake rising, speed decreasing, pre-corner
       InitialBrake,       // peak deceleration zone
       CornerEntryRelease, // brake releasing + steer increasing (existing cornerEntry bucket)
       MidCorner,          // steady steering, moderate latG, throttle < threshold
       ExitReThrottle      // existing cornerExit bucket: accel rising + steer decreasing
   };
   ```
   All thresholds in new `g_thresholds` fields following the existing INI pattern (`loadThresholds()` reads them; MotecSettingsForm persists). No hardcoded universal values.

9. **Per-phase accumulators on `LapStats`:** modeled on `VelocityHistogram`, add per-phase summaries:
   ```cpp
   struct PhaseAccumulator {
       StatSummary latG, steerAngle, yawRate, wheelSlipFrontAvg;  // etc.
       std::size_t sampleCount = 0;
       void merge(const PhaseAccumulator& other);
   };
   DrivingPhase currentPhase;
   PhaseAccumulator phases[static_cast<int>(DrivingPhase)::Unknown]; // indexed by phase
   double segmentationConfidence = 1.0; // fraction of samples classified with criteria met
   ```
   Merge support in `averageSelectedLaps()` (same pattern as existing histogram `.merge()`).

10. **Asymmetry detection** (§4): L/R same-axle + front/rear comparisons computed **only within comparable phases**:
    - Compare left-corner segments vs right-corner segments when both exist with sufficient samples (`minHandlingSamples`)
    - 3-way verdict per comparison: `RepeatedInComparableConditions` / `LikelyLayoutPhaseArtifact` (e.g., track has more R-handers) / `InconclusiveInsufficientData`
    - Feed into findings as evidence or confounder — never standalone mechanical conclusions from global averages

### Acceptance criteria
- Synthetic lap with clear braking → cornering → exit pattern: all phases classified correctly, segmentationConfidence > 0.9.
- Ambiguous data (constant throttle + constant steer): samples land in `Unknown`, not guessed into a phase.
- L/R asymmetry on a track with only left-handers: verdict = `LikelyLayoutPhaseArtifact`, no mechanical conclusion emitted.

---

## Phase 3 — Recommendations protocol + Diagnostics tab UI *(Niveau C, spec §9)*

### Steps

12. **Recommendation gate** per spec Niveau C — emit a test recommendation ONLY when ALL of:
    - Symptom confidence ≥ MEDIUM (internal score ≥ `g_thresholds.confidenceMedium`)
    - Top cause is distinguishable from alternatives (its validation channels present AND concordant; at least one competing cause's key channel absent or contradictory)
    - The adjustment exists in that simulator's setup space:
      | Profile | Exposed setup channels | Implication |
      |---|---|---|
      | EVOM1/AC EVO | `Diff_Coast_Raw`, `Diff_Power_Raw` (LIVE), ballast, aero balance (MOTEC) | Can name specific setting + direction |
      | AMS2 | Almost none in mapping.json | → "run comparative A/B test" suggestion only |
      | PMR | ARB stiffness (VERIFY units) | Limited; mostly comparative tests |

    Each recommendation = controlled experiment:
    - Setting to test (name, never a click count or target value)
    - Direction of change **if justified**; else "run comparative A/B"
    - Reason + supporting measurements (from evidence list)
    - Confidence label (LOW/MEDIUM/HIGH — no numeric %)
    - Risks / side effects possible
    - Signals to watch during next run (validation channels from catalog)
    - Criteria to confirm or reject the hypothesis

13. **UI integration:**
    - `LapReportGenerator.h`: add `Diagnostics` to `enum class ReportSection`
    - Plain-text rendering in new section content — one block per finding:
      ```
      ── SYMPTOM: Front understeer on corner entry (Confidence: MEDIUM) ──
         When observed:  Corner entry, left-hand corners (12 of 18 samples)
         Measurements:   Steer/yaw correlation r=0.42 (> threshold 0.30); latG avg 0.6g; front tyre inner-outer spread 7°C LF / 5°C RF
         Possible causes (ranked):
           1. Front camber insufficient (weight 0.48) — supported by temp pattern from existing analysis
           2. Front roll stiffness low (weight 0.31) — no ARB data available to confirm/deny
           3. Front pressure low (weight 0.21) — pressures within normal range, less likely
         Suggested test: Increase front camber by a small step; watch for reduced inner-outer spread and improved entry speed in left-handers
         Watch next run:   Front tyre temps (inner/outer), yaw rate response at entry, lap time delta on L-handers vs R-handers
         Limitations:      No slip-angle channel available; ARB stiffness not exposed by this simulator profile
      ```
    - DFM + header: new `TTabSheet TabDiagnostics` + memo in `MotecSetupForm.dfm/.h`; one more block in `UpdateReportTabs()` using existing `setTabVisible(tab, section)` pattern (tab visible only when content non-empty AND hasRecommendation)
    - Tablet JSON path (`reportjson::buildReportJson`) gets the Diagnostics section too if non-empty; CP1252→UTF8 conversion already handled by `cp1252ToUtf8()` in ReportJsonWriter.h
    - All user-facing strings via existing `tr()` for EN/FR

### Acceptance criteria
- Fixture with strong understeer evidence → Diagnostics tab visible, shows ranked causes + test recommendation.
- Fixture with insufficient data (single lap, missing channels) → no finding emitted OR finding present but NO categorical recommendation ("insufficient evidence — run comparative test").
- EN and FR strings render correctly in both desktop memo and tablet JSON output.

---

## Phase 4 — Before/after comparison mode + tests *(spec §5)*

### Steps

14. **Comparison feature:**
    - User selects two lap groups (or auto-detects boundary if a setup-change marker exists)
    - Alignment check: same track, car, tyre compound where metadata allows; report mismatches as confounders
    - Compare per-phase metrics and symptom frequency/severity between the two sets
    - Show known setup delta if provided by user (free-text field in UI or INI entry)
    - Per-symptom verdict: `ProbableImprovement` / `ProbableDegradation` / `Inconclusive`, with explicit confounders listed (conditions, traffic, fuel level change, driving style variance)
    - No auto-attribution when multiple parameters changed between runs

15. **Tests** — standalone harness pattern per repo memory (`tooling.md`: compile relevant .cpp + link `rtl.lib vcl.lib cp32mt.lib`; console main):
    - Phase segmentation: clear patterns → correct phases; ambiguous data → UnknownPhase; boundary cases at threshold edges
    - Channel-absent matrix per profile (AMS2 no true wheel speeds, EVO damper-velocity gap in mapping.json, PMR no ride height/Rake) → rules degrade gracefully or abstain with traceability reason
    - Multi-candidate cause ranking: verify order matches expected weights; confidence labels correct at boundaries (0.30/0.60/0.85)
    - L/R asymmetry: comparable conditions → repeated verdict; non-comparable (track bias) → artifact verdict; insufficient data → inconclusive
    - Before/after with confounders (fuel drop, ambient temp change) → Inconclusive verdict, not false improvement claim
    - **No categorical recommendation when evidence insufficient** — verify gate logic blocks output below MEDIUM confidence or when top cause indistinguishable
    - Non-regression: existing 4 rules produce identical `AnalysisResult` vectors on fixture lap set before/after all changes

### Acceptance criteria (spec §10)
All items from spec section 10 "Critères de fin" met, verified by test harness + manual smoke.

---

## Relevant files & integration points

| File | Changes in this plan |
|---|---|
| `MotecSetupAnalyzer.h` | New aliases (Phase 0), `DiagnosticFinding`/`DiagnosticRule`/registry types (P1), phase enum + accumulators on `LapStats::addHandlingSample()` (P2); reuses existing `CauseEntry`, `VelocityHistogram`, `pickDamperHistogram()`, `detail::evaluateDamperCorner()`, `g_thresholds` |
| `analyze_motec_setup.cpp` | File-import row loop already calls `addHandlingSample()` per frame — no change needed for phase hook; new diagnostic evaluation entry point alongside existing `evaluateLapWithRules()` (P1); INI persistence for new thresholds follows existing pattern |
| `RuleConfig.h/.cpp` | Consumed as-is (`findRule`, base weights, validation channels). Optional: user can edit symptom catalog via rules.json without recompile (already supported) — no code change needed unless schema extension desired |
| `LiveChannelResolver.cpp` + 3 live capture feed functions | **No changes expected** — per-frame hook already exists via `addHandlingSample()` on all paths. If new phase-specific channels are added to mapping.json later, the table-driven sink handles them automatically |
| `LapReportGenerator.h/.cpp` | New `ReportSection::Diagnostics`; merge of phase accumulators in `averageSelectedLaps()`; diagnostics text generation (P3); comparison logic entry point (P4) |
| `MotecSetupForm.dfm/.h/.cpp` | Diagnostics tab: new TTabSheet + memo in DFM, pointer in header, one more block in `UpdateReportTabs()`, tablet JSON section update |
| `MotecSettingsForm.*` | Threshold editor entries for new segmentation thresholds (follows existing 6-tab dialog pattern) |
| `docs/NEW_ALERT_PROCEDURE.md` | Extension conventions to follow — read before implementing any rule class |

---

## Profile channel gaps (first-class design inputs, not bugs)

Every rule declares required vs optional aliases and degrades confidence / abstains per spec §3–§6:

| Gap | Affected profiles | Impact on rules |
|---|---|---|
| Wheel speeds = ground-speed proxy (`mSpeed` duplicated ×4) | AMS2 LIVE | Brake-lockup rule must NOT use these for L/R divergence; declare optional/absent → reduced confidence or abstain. MOTEC file import has true `Wheel_Angular_Speed_*` rad/s — usable there only |
| Damper velocity absent from mapping.json (no rows) | EVOM1 | 6 of 10 catalog symptoms reference damper_velocity → EVO diagnostics degraded unless C++ synthesizes it in `AcsEvoLiveCapture.cpp` (verify; if not, rules fall back to travel-derived via existing `DamperCornerStats::addSample()` path which IS wired for PMR) |
| No ride height / Rake virtual channel resolvable | PMR | Bumpstop rule reduced confidence; any rake-dependent finding abstains with traceability note |
| Slip angle (`front_slip_angle`, `rear_slip_angle`) unmapped in ALL profiles | All 3 | Dangling validation channels in catalog #2/#9 → contribute no modulation, recorded as missing channel on findings. Not a blocker — rules work without them at reduced confidence |
| Vertical load per wheel: EVO LIVE only (`physics.wheelLoad[]`); PMR has averages (VERIFY units); AMS2 none | All 3 except full EVO | Load-transfer-based cause validation only fires for EVO; others get "no vertical load data" confounder note |
| Setup channels nearly empty (no spring rates, damper clicks, ride-height settings) | AMS2, PMR fully; EVO partial (diff raw + ballast only) | Recommendations can name direction but never target values/clicks for these profiles → comparative-test suggestions per spec Niveau C rule 4 |

---

## Out of scope v1

- GDI+ diagnostic cards / custom paint-box rendering
- Per-corner slip-angle channels (unmapped in all profiles — no source data exists)
- Inventing setup target values, click counts, mechanical ranges anywhere
- Numeric probability display ("90% sure") without statistical calibration
- New rule-engine dependency or JSON DSL beyond existing `rules.json` schema
- Automatic detection of which specific sim setting was changed between sessions (requires in-sim telemetry not exposed)

---

## Verification checklist (run at end of each phase)

1. **Build:** Embarcadero C++Builder via Project1.cbproj — zero errors, no new warnings on modified files. BCC quirks per repo memory if touching GDI+ strings or wide-char APIs.
2. **Harness tests:** All test cases for the current phase pass; non-regression check that original 4 rules' outputs are byte-identical on fixture lap set.
3. **Manual desktop smoke:** Load an LD/LDX file → Diagnostics tab appears only when findings exist (setTabVisible pattern); EN + FR strings render correctly.
4. **Live capture smoke per profile** (AMS2 / EVO / PMR): confirm graceful degradation — e.g., AMS2 brake-lockup rule reports missing-channel note instead of false positives; PMR shows no Rake-dependent findings; EVO damper rules use travel-derived fallback if direct velocity unavailable.
5. **Tablet JSON:** Diagnostics section present in `reportjson` output when non-empty; UTF-8 valid (validator script from repo memory).

---

## Agent handoff notes

- Read this file top-to-bottom before starting any phase. Each phase's acceptance criteria are the definition of done for that phase.
- The spec (`specification_agent_diagnostic_multisignal_setup.md`) is the **requirements** document; this plan is the **implementation sequence**. When they conflict, the spec wins — flag the discrepancy and update this file.
- Follow `docs/NEW_ALERT_PROCEDURE.md` conventions for any new rule class (alias → provider fill → rule template → registry registration).
- All user-facing strings must use `tr()` for EN/FR localization from day one.
- Do NOT modify existing 4 rules, their aliases, or the file-import dispatch enum (`ChannelDispatch`) in a way that changes current behavior — additive only.
- Thresholds: new values go into `g_thresholds` struct + INI load/save + MotecSettingsForm tab (follow existing pattern exactly; no hardcoded constants in rule logic).
