# Skill: Data Analysis Engineering (MotecDataAnalyser)

Proactive reasoning framework for designing NEW telemetry-based diagnostics. Use when the user asks "can we detect X?", "what else can we analyze from these channels?", or wants to extend the rule engine beyond the current 4 rules. This is NOT a procedure checklist — it's how to *think* about what the data tells us and where the signal-to-noise boundary lies.

---

## 1. What We Actually Have (Channel Inventory)

The following semantic aliases are available via `DataProvider::getValue()` after mapping.json resolution. Availability varies by profile (AMS2 / EVOM1 / PMR). A rule is only valid if ALL its required inputs exist in the target profile(s).

### Always Available (all 3 profiles, LIVE source)
| Category | Aliases | Notes |
|----------|---------|-------|
| Speed/velocity | `Speed_Kmh`, `Velocity_X/Y/Z` | EVO: km/h; AMS2: m/s → factor in mapping |
| G-forces | `Acceleration_X/Y/Z` (or per-axis) | Lateral = Y, Longitudinal = Z/X depending on sim convention — VERIFY axis order per profile |
| Steering + yaw | `Steer_Angle`, `Yaw_Rate` | Core handling inputs |
| Throttle/Brake | `Throttle_Pos`, `Brake_Pos` | 0–100% pedal input (not pressure) |
| Wheel speeds / slip | `Wheel_Slip_Front`, `Wheel_Slip_Rear` | Derived: front = LF, rear = LR in current code |
| Tyre temps ×3 zones | `Tyre_Temp_Inner_FL/FR/RL/RR`, `_Middle_*`, `_Outer_*` | 12 values — the richest diagnostic surface we have |
| Brake temps | `Brake_Temp_FL/FR/RL/RR` | Single value per wheel (no inner/outer split) |
| Water/Oil temp | `Water_Temp`, `Oil_Temp` | Engine cooling state |
| Ambient/Road/Air | `Ambient_Temp`, `Road_Temp`(EVO), `Air_Density`(EVO) | Context for normalizing thermal rules |

### EVO-Only (LIVE)
| Category | Aliases | Why it matters |
|----------|---------|----------------|
| Camber per wheel | `Camber_FL/FR/RL/RR` (rad) | Direct geometry — no inference needed |
| Brake torque per wheel | `Brake_Torque_FL/FR/RL/RR` (Nm) | True bias calculation, not just pedal % |
| Suspension travel ×4 | `Suspension_Travel_FL/FR/RL/RR` (mm) | Bump/rebound dynamics, load transfer |
| Ride height F/R | → virtual channel **Rake** | Longitudinal balance proxy for aero/spring setup |
| CG Height | `CG_Height` (m) | Weight-transfer math constant |
| Ballast / Air temp / Road temp | Various | Context normalization |
| KERS/Battery state | Charge, input, voltage, charging flag | Power-unit diagnostics (if relevant to sim mode) |

### AMS2-Only (LIVE)
| Category | Aliases | Notes |
|----------|---------|-------|
| Suspension travel ×4 | `mSuspensionTravel[0..3]` | Same physics as EVO, different source path |
| Ride height F/R | → Rake virtual channel | Computed in DataProvider |
| Brake bias setting | `Brake_Bias_Setting` (%) | Driver-set value — compare vs actual torque distribution if available |
| Angular velocity XYZ | Full 3-axis body rates | Roll rate enables lateral load estimation |
| Tyre pressures ×4 | Per-wheel (LIVE) | Direct, not inferred from temps |

### PMR-Only / Partial
PMR has the broadest channel set via UDP packets but several are marked `VERIFY` in mapping.json. Treat as available-but-unvalidated until first live capture confirms units and semantics.

---

## 2. Physical Relationships Map (Channel → Phenomenon)

This is the core reasoning table: **what physical phenomenon does each signal combination reveal?** When designing a rule, start from the physics you want to detect, then find which channels are its observable proxies.

### Tyre Thermal State
| Observation | Physics | Channels needed | Interpretation |
|-------------|---------|-----------------|----------------|
| Inner > Middle > Outer (all 4 wheels) | Excessive negative camber — contact patch shifted inward | `Tyre_Temp_Inner_*` vs `_Middle_*` vs `_Outer_*` | Camber too negative → reduce neg. camber or check alignment |
| Middle >> Shoulders (symmetric L/R, F/R) | Under-inflation — centre carries more load than edges | All 12 tyre temps; compare middle avg vs shoulder avg per wheel | Pressure low by ~0.3–0.5 bar → increase pressure |
| Outer > Inner on one side only | Asymmetric loading: camber bump, bent arm, or uneven spring rate that corner | Per-wheel inner/outer delta + L/R comparison at same axle | Mechanical issue, not setup — flag as "check geometry" |
| Front temps >> Rear temps (all zones) | Car is front-heavy in thermal load → understeer-prone balance; possibly too much front wing / rear spring soft | Axle-averaged tyre temp F vs R + `Wheel_Slip_Front`/`Rear` correlation | Balance shift: reduce front aero or stiffen front springs |
| Temp delta between L and R on same axle > 8°C sustained | Uneven loading across track (banking, kerb usage) OR mechanical asymmetry | Per-axle L/R comparison over ≥3 laps | If consistent → geometry; if corner-specific → driving line / track feature |

### Handling Balance
| Observation | Physics | Channels needed | Interpretation |
|-------------|---------|-----------------|----------------|
| High front slip + low rear slip in corners | Understeer — front tyres saturating before rear | `Wheel_Slip_Front` vs `_Rear`, correlated with lateral G and steer angle | Need more rear grip: less front wing, softer rear springs, more neg. rear camber |
| Low front slip + high rear slip at corner exit (throttle on) | Oversteer / snap-over — rear breaking loose under power | `Wheel_Slip_Rear` spike coincident with throttle > 50% and yaw rate exceeding steer-angle prediction | Need stability: more front wing, stiffer anti-roll bar rear, less neg. camber rear |
| Steer angle high + lateral G low (vs corner speed) | Driver fighting understeer — large input for little response | `Steer_Angle` magnitude vs `Acceleration_Y` in sustained corners (>2 s) | Confirms understeer; cross-reference with tyre temps to find cause |
| Yaw rate oscillation at moderate steer + throttle | Neutral-to-over balance, car rotating then straightening repeatedly (limit cycling) | `Yaw_Rate` zero-crossing frequency vs corner duration | Borderline setup — small aero/spring change could stabilize or destabilize further |

### Suspension Dynamics
| Observation | Physics | Channels needed | Interpretation |
|-------------|---------|-----------------|----------------|
| Low std-dev of suspension travel + high bump/rebound asymmetry (one direction dominates) | Spring rate too stiff for the damper setting — car is "on top" of its springs, only compressing in one phase | `Suspension_Travel_*` time-series: compute mean, stdDev, and directional split per corner over a lap segment | Soften spring OR rebalance damper (more rebound if compression-dominated) |
| Suspension travel hitting bumpstop repeatedly (>N times/lap at same corner) | Insufficient total travel for the track's vertical profile; or spring too soft → bottoming out | `Suspension_Travel_*` max value vs known bumpstop threshold + frequency of peaks near limit | Stiffen spring, add preload, or (if sim allows) increase travel setting |
| Rake shifting significantly between corners and straights (>2 mm change) | Longitudinal weight transfer exceeding what the setup absorbs — springs too soft longitudinally OR aero creating dynamic pitch instability | `Rake` virtual channel time-series: compare corner avg vs straight-line avg + rate of change | Stiffen front/rear springs proportionally; check if aero balance is contributing (EVO only) |
| Front suspension travel >> Rear in braking zones | Brake bias too far forward, or rear springs much stiffer than front → all weight transfer absorbed by front axle | `Suspension_Travel_Front` avg during decel > 1.5 G vs `_Rear` + `Brake_Bias_Setting`(AMS2) / `Brake_Torque_*`(EVO) actual distribution | Adjust brake bias rearward; or rebalance spring rates F/R |

### Thermal Management (Engine/Brakes)
| Observation | Physics | Channels needed | Interpretation |
|-------------|---------|-----------------|----------------|
| Water temp rising monotonically over 5+ laps without plateauing | Cooling insufficient for sustained load — radiator undersized, fan not engaging, or ambient too high for the setup margin | `Water_Temp` trend (slope per lap) + `Ambient_Temp` context | If slope > X°C/lap after lap 3 → critical; recommend cooling upgrade or reduce engine output if sim allows |
| Brake temp L/R delta on same axle growing over session (>15°C and widening) | Uneven brake bias in practice (one caliper less effective, pad wear asymmetry, or fluid contamination) | `Brake_Temp_FL` vs `_FR`, `_RL` vs `_RR`: compute per-axle delta trend across laps | Mechanical issue — not a setup fix. Flag for inspection. |
| Brake temp > 650°C sustained (compound-dependent) | Pad glazing / fluid boil risk — braking too hard with insufficient cooling airflow, or bias wrong for the compound | `Brake_Temp_*` max per lap + correlation with brake input magnitude (`Brake_Pos`) and speed at entry | Reduce brake pressure setting if available; check if track has adequate runoff/cooling zones; consider softer pad in sim settings |

### Fuel / Power (context-dependent)
| Observation | Physics | Channels needed | Interpretation |
|-------------|---------|-----------------|----------------|
| Fuel level dropping faster than expected for lap distance | Aggressive throttle usage OR fuel flow setting too high — impacts strategy and weight distribution over session | `Fuel` trend per completed lap + average `Throttle_Pos` during that lap | If consumption/lap > baseline by 10%+ → driver style or map issue; affects late-race balance (lighter car = different setup optimum) |

---

## 3. Hypothesis Framework (How to Formulate a Testable Rule)

When the user proposes "can we detect X?", walk through these steps IN ORDER:

### Step A — Define the Physical Signal
What measurable change in the vehicle's state indicates phenomenon X? Be specific about direction and magnitude. Not "tyres are hot" but "inner-shoulder temperature exceeds middle by >5°C sustained for >30 s of cornering."

### Step B — Identify Observable Proxies
Which channels from Section 1 can we actually measure that correlate with the signal in A? If no direct channel exists, what's the best indirect proxy and how strong is the correlation (strong / moderate / weak)? Flag if you're relying on a weak proxy.

### Step C — Determine Required Context
What additional data do we need to avoid false positives? Examples:
- Tyre temp rules need `Ambient_Temp` + track surface type context (a 90°C tyre in 45°C ambient is normal; at 15°C it's a problem).
- Handling rules need speed gating — slip angle means nothing below 80 km/h.
- Suspension rules need to exclude pit lane / straight-line data where travel is near static ride height.

### Step D — Set Threshold with Physical Justification
Every threshold must have a WHY that traces back to physics, not "it looked about right on this lap." Examples:
- 5°C inner-middle delta → based on typical camber effect of ~1° producing ~4–6°C differential in sim tyre models.
- Slip > 0.8 (normalized) → represents the point where most FIA-spec compounds leave their linear grip region.
If you cannot justify a threshold physically, mark it as `TUNABLE` and default to a conservative value that minimizes false positives over missed detections.

### Step E — Define Severity Logic
- **Critical**: Immediate safety/performance risk (engine overheating > 120°C, brake fade). Triggers on single-lap data.
- **Warning**: Suboptimal setup causing measurable performance loss (>1 s/lap potential). Requires ≥3 laps of consistent evidence.
- **Info / Suggestion**: Marginal improvement opportunity (<0.5 s/lap). Requires ≥5 laps + low variance across the sample.

### Step F — Specify What Data Confirms/Refutes After Action
Every recommendation must state: "After making change Y, expect channel Z to show [specific direction/magnitude] within N laps." This makes the rule falsifiable and gives the user a verification path.

---

## 4. Validation Criteria (Real Signal vs Noise)

A proposed rule is NOT ready for implementation until it passes ALL of these:

| Criterion | Why | How to check |
|-----------|-----|--------------|
| **Reproducible across ≥2 sims** (or explicitly single-sim with justification) | A pattern that only exists in AMS2's tyre model isn't a universal physics signal — it's an artefact of one implementation. If truly sim-specific, mark the rule `profile:AMS2` and document why. | Test on recorded data from both EVO and AMS2 (or PMR if available). Same threshold should trigger at similar severity levels. |
| **Stable across ≥3 laps** before triggering | Single-lap anomalies are usually driving-line artefacts, kerb hits, or transient thermal spikes. The rule must require N consecutive/total qualifying frames OR M out of K recent laps showing the pattern. | Define a minimum sample window (e.g., "≥60 s of cornering data" not just "one lap"). |
| **Not confounded by ambient conditions** | A 10°C tyre temp delta in 45°C heat is normal; at 10°C it's extreme. Rules involving absolute temperatures MUST normalize against `Ambient_Temp` (or use relative deltas that are inherently normalized). | Check: does the rule fire identically if you shift all temps by +20°C? If yes → uses absolute values incorrectly. Fix with delta-based logic or ambient offset. |
| **Distinguishable from known benign causes** | E.g., high front slip in a slow hairpin is normal (front tyres at limit); the same reading in a fast sweeper indicates understeer. The rule must gate on context (speed, corner type via lateral G magnitude). | Enumerate 3+ scenarios where the raw signal appears but NO problem exists. Ensure the rule's conditions exclude them. |
| **Actionable recommendation** | "Something is wrong with your suspension" is not a useful output. The rule MUST produce a specific setup parameter to change, in which direction, and by approximately how much (within sim constraints). If you can't specify this, the diagnostic isn't complete — go back to Step A. | Write the `recommendation` string before coding. If it's vague, the physics model is incomplete. |

---

## 5. Anti-Patterns (What NOT To Do)

| Anti-pattern | Why it fails | Correct approach |
|--------------|-------------|-----------------|
| **Single-lap trigger for thermal rules** | Tyre temps take 3–5 laps to reach steady state. A lap-1 reading is meaningless. | Require minimum session length (≥3 completed laps) before evaluating any temperature-based rule. Use `sessionLapCounter` gate. |
| **Absolute thresholds without ambient normalization** | "Water temp > 90°C = warning" breaks in summer vs winter, sea level vs altitude. | Express as delta from expected steady-state: `(waterTemp - ambientTemp) > X` OR use a per-ambient lookup if the relationship is non-linear. |
| **Correlation treated as causation** | "Front slip high AND rear temps low → understeer" — but both could be caused by a flat spot on one front tyre (mechanical, not setup). The rule would recommend aero changes that don't fix it. | Add a discrimination check: if the signal is asymmetric L/R at the same axle, flag as "possible mechanical issue" BEFORE suggesting setup changes. Mechanical > Setup in priority. |
| **Overfitting to one track's geometry** | A rule calibrated on Spa (long straights → high brake temps) will false-positive on Monaco (no sustained braking). Track-specific patterns are NOT universal physics. | Gate rules by speed regime, not absolute values. "Brake temp delta > X during decel events where entry speed > 150 km/h" generalizes; "brake temp > 400°C" does not. |
| **Adding a rule without checking channel availability** | A beautiful handling rule that requires `Camber_FL` will silently never fire on AMS2 (no camber in shared memory). The user sees nothing and assumes the app is broken. | ALWAYS verify every alias in `requiredInputs()` exists for ALL target profiles in mapping.json before registering the rule. If a channel is missing, either add it to mapping or make the input optional with graceful degradation. |
| **Threshold from one data point** | "On my test lap, inner temp was 82°C and that felt like too much" → setting threshold at 80°C. One sample = no statistical basis. | Collect ≥10 laps across different conditions. Set threshold at mean + 2σ of the "normal" distribution, or derive from physics (see Step D). Document the derivation in a code comment. |
| **Ignoring the driver variable** | Aggressive driving produces signals identical to poor setup (high slip, high temps, large steer angles). The app cannot distinguish "driver at limit with good car" from "car understeering badly." Be honest about this limitation. | Frame recommendations as conditional: "IF you feel the car is sliding in corners AND front slip exceeds X, THEN..." Acknowledge that driver feedback disambiguates what telemetry alone cannot. |

---

## 6. Current Rule Coverage & Gaps (as of Oct 2025)

### Implemented
| Rule | Covers | Known limitation |
|------|--------|-----------------|
| `EngineTemperatureRule` | Water/oil over max → Critical | No trend detection (slow creep vs sudden spike treated same); no ambient normalization |
| `TyreTemperatureRule` | Camber (inner≤outer) + Pressure (middle vs shoulders) | Single-snapshot per lap average; doesn't distinguish "warming up" from "at steady state"; no L/R asymmetry check for mechanical issues |
| `TyrePressureRule` | L/R pressure disagreement per axle + cross-axle | AMS2 only has LIVE pressures; EVO infers from temps (indirect); PMR unverified |
| `HandlingBalanceRule` | Slip-based under/oversteer detection | No speed gating below ~80 km/h; doesn't distinguish power-over vs corner-entry oversteer; no multi-lap confirmation window |

### High-Value Gaps (candidates for next rules, in priority order)
1. **Brake thermal management** — per-wheel temp trend + L/R asymmetry detection. Channels fully available on all 3 profiles. Physics is straightforward. No current rule touches brakes at all beyond the settings form display.
2. **Suspension travel analysis (EVO/AMS2)** — bump/rebound ratio, bottoming-out frequency, static ride height drift over session (spring fatigue / thermal softening). Channels available; `evaluateDamperCorner` exists in code but is NOT wired to a registered rule yet.
3. **Rake stability** — longitudinal balance proxy. Virtual channel already computed. No rule uses it. Would detect "car pitches too much under braking/acceleration" → spring rate F/R imbalance.
4. **Tyre temp L/R asymmetry (mechanical flag)** — Current `TyreTemperatureRule` looks at inner/middle/outer per wheel but does NOT compare left vs right on the same axle for sustained deltas > threshold. This is the single most common "something's bent" indicator and it's missing.
5. **Multi-lap trend detection** — None of the current rules look at *change over time*. A water temp that's 95°C on lap 1 but 108°C on lap 6 is more dangerous than a stable 102°C. No rule implements slope/trend logic yet.

### Lower Priority / Research
- Aero balance inference from Rake + suspension travel at speed (EVO only, needs CG height constant)
- Fuel strategy impact on setup (weight change → optimal pressure/camber shift over session) — complex, sim-dependent
- Track-surface grip estimation from slip angle vs lateral G ratio (would enable "grip level" normalization for all other rules)

---

## 7. Quick Reference: Where Things Live in Code

| Concept | Location |
|---------|----------|
| Available aliases per profile | `MotecDataAnalyser.mapping.json` → search by `"alias":"X"` and check which profiles have rows |
| Virtual channels (Rake) | `DataProvider::getValue()` recursive resolution, max depth 16; defined in mapping.json with special handling |
| Current rule implementations | `MotecSetupAnalyzer.h` — inline classes registered by `buildDefaultAnalysisRegistry()` |
| Thresholds / tunable values | `g_thresholds` global + `MotecDataAnalyser.thresholds.ini`; per-rule fields in `AnalysisSettings` struct |
| Adding a new alias to mapping.json | Add row(s) for each profile that has the channel; set factor/offset from source unit → target unit. Test with live capture before wiring into a rule. |
| Rule registration | Append class + add pointer in `buildDefaultAnalysisRegistry()` return vector |
| Per-lap data available to rules | `LapStats` struct (see `MotecSetupAnalyzer.h`) — contains averaged temps, slip values, steer/yaw means, suspension stats if populated |
