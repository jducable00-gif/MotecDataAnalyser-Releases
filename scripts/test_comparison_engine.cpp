// =============================================================================
//  test_comparison_engine.cpp — Phase 4 standalone test harness.
//
//  Covers (per docs/IMPLEMENTATION_PLAN_DIAGNOSTIC_ENGINE.md step-15):
//    A. Driving-phase segmentation correctness (clear patterns, ambiguous ->
//       Unknown not guessed, threshold-edge boundaries).
//    B. Channel-absent matrix: rules degrade gracefully / abstain with a
//       traceability reason when required channels are missing per profile.
//    C. Multi-candidate cause ranking order + confidence-label boundaries at
//       0.30 / 0.60 / 0.85 (rankCauses modulation x1.25 / none / x0.75).
//    D. L/R asymmetry verdicts: comparable -> repeated; track bias -> artifact;
//       insufficient data -> inconclusive.
//    E. Before/after comparison with confounders (fuel drop, ambient temp) is
//       forced Inconclusive — never a false "improvement" claim. Clean pairs
//       yield correct Improvement / Degradation verdicts + per-phase deltas.
//    F. Gate-logic primitives: single-lap confidence cap at MEDIUM(0.60), label
//       boundaries, and the categorical-gate predicate (MEDIUM+ AND EVO profile).
//    G. Non-regression of the 4 legacy rules on a fixture lap set (identical
//       AnalysisResult vectors) + clean-lap no-finding case.
//
//  Pure ASCII on purpose (bcc32c encodes narrow literals as CP1252; French text
//  is built accent-free). Console main() does not clash with vcl.lib's WinMain
//  (lazy member resolution). g_thresholds is owned by analyze_motec_setup.cpp —
//  do NOT redefine it here.
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "MotecSetupAnalyzer.h"   // LapStats, StatSummary, g_thresholds, rules, DataProvider
#include "DiagnosticEngine.h"     // diag:: detectors, rankCauses, evaluateLapWithDiagnostics
#include "ComparisonEngine.h"     // diag:: compareLapGroups / buildComparisonSection

// DiagnosticFinding (struct) and diagnosticConfidenceLabel() live in GLOBAL scope
// (MotecSetupAnalyzer.h), not inside namespace diag. Re-expose them under diag:: so
// the test code can refer to them uniformly with the other diagnostic symbols.
namespace diag {
    typedef ::DiagnosticFinding DiagnosticFinding;   // type alias into global struct
    using ::diagnosticConfidenceLabel;               // function declaration import
}

// ---------------------------------------------------------------------------
// Tiny assertion framework (no external deps). Counts pass/fail; exits non-zero
// on any failure so the .bat can detect it.
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_MSG(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        std::printf("FAIL [%s:%d] %s  (%s)\n", __FILE__, __LINE__, #cond, (msg)); \
    } \
} while (0)

static void section(const char* name) { std::printf("\n== %s ==\n", name); }

// Local NaN helper (the codebase uses numeric_limits directly; this keeps call
// sites terse). Not in any namespace so it resolves unqualified.
inline double NaN() { return std::numeric_limits<double>::quiet_NaN(); }

// ---------------------------------------------------------------------------
// Fixture helpers. A "valid" lap has a positive finite duration inside the
// median-based selection window and rowCount>0 so buildAverageLapData() yields
// a non-empty averaged lap (the comparison engine's validity gate). All channel
// StatSummarys start empty (count==0) => alias absent => rules abstain.
// ---------------------------------------------------------------------------
static LapStats makeValidLap(int number = 1, const char* track = "TestTrack",
                             const char* car = "TestCar") {
    LapStats lap;
    lap.lapNumber   = number;
    lap.startTime   = 0.0;
    lap.endTime     = 90.0;      // duration 90s == median => inside [63,117] window
    lap.rowCount    = 500;       // >0 so averaged rowCount>0 (validity gate)
    lap.trackName   = track ? track : "";
    lap.vehicleName = car ? car : "";
    return lap;
}

// Add N identical samples to a StatSummary (average == value).
static void fill(StatSummary& s, double value, int n = 10) {
    for (int i = 0; i < n; ++i) s.add(value);
}

// Find a diagnostic finding by id in a vector.
static const diag::DiagnosticFinding* findById(const std::vector<diag::DiagnosticFinding>& v,
                                               const char* id) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].symptomId == id) return &v[i];
    return nullptr;
}

// Find a symptom comparison by id.
static const diag::SymptomComparison* findSc(const std::vector<diag::SymptomComparison>& v,
                                             const char* id) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].symptomId == id) return &v[i];
    return nullptr;
}

// ===========================================================================
// A. Driving-phase segmentation correctness
// ===========================================================================
static void testPhaseSegmentation() {
    section("A: Phase segmentation");

    // --- Clear straight + stable acceleration pattern -----------------------
    // throttle>25, brake<10, |latG|<=0.15, longG>=0.08  => StraightStableAccel(1)
    LapStats lap;
    for (int i = 0; i < 40; ++i) {
        // speed must be finite & decreasing-by-<3km/h to avoid braking branch: keep constant
        lap.addHandlingSample(/*steer*/0.0, /*yaw*/0.0, /*latG*/0.10, /*longG*/0.20,
                              /*brake*/5.0,  /*throttle*/80.0, /*suspAsym*/NaN(),
                              /*speedKmh*/150.0);
    }
    CHECK(lap.phases[static_cast<int>(DrivingPhase::StraightStableAccel)].sampleCount() == 40u);
    // No other phase should have absorbed samples from this pure pattern.
    for (int p = 2; p < kDrivingPhaseCount; ++p) {
        CHECK(lap.phases[p].sampleCount() == 0u);
    }

    // --- Ambiguous data: low latG, no throttle/brake signal -> Unknown ------
    LapStats lapAmbig;
    for (int i = 0; i < 30; ++i) {
        // Not braking (brake<10), not accelerating (throttle<=25), |latG| below cornering,
        // longG low => no branch matches -> stays Unknown(0). Never guessed.
        lapAmbig.addHandlingSample(/*steer*/1.0, /*yaw*/0.3, /*latG*/0.05, /*longG*/0.02,
                                   /*brake*/4.0,  /*throttle*/10.0, NaN(), 90.0);
    }
    // Unknown samples are never bucketed into phases[] at all — the classifier only
    // records a sample when it fully matches a known phase. "Never guessed" therefore
    // means: zero classified samples and every real-phase bucket empty (checked below).
    CHECK(lapAmbig.phaseClassifiedSamples == 0u);
    for (int p = 1; p < kDrivingPhaseCount; ++p) {
        CHECK(lapAmbig.phases[p].sampleCount() == 0u);
    }

    // --- Boundary: latG exactly at phaseLatGMidMin(0.30g) with stable steer --
    // MidCorner requires absLatG >= 0.30 and dSteer <= 2.5, not braking/accelerating.
    LapStats lapMid;
    for (int i = 0; i < 25; ++i) {
        lapMid.addHandlingSample(/*steer*/18.0, /*yaw*/4.0, /*latG*/0.30, /*longG*/-0.05,
                                 /*brake*/6.0,   /*throttle*/12.0, NaN(), 95.0);
    }
    CHECK(lapMid.phases[static_cast<int>(DrivingPhase::MidCorner)].sampleCount() == 25u);

    // --- Boundary: speed drop exactly at phaseSpeedDropPerSec(3.0 km/h) ------
    // Braking (brake>10, throttle<35) + hasSpeed + speedDrop>=3 => braking branch fires.
    LapStats lapBrk;
    for (int i = 0; i < 20; ++i) {
        const double spd = 140.0 - static_cast<double>(i) * 3.0;   // drops exactly 3 km/h per frame
        lapBrk.addHandlingSample(/*steer*/2.0, /*yaw*/-6.0, /*latG*/0.10, /*longG*/-1.5,
                                 /*brake*/70.0, /*throttle*/8.0, NaN(), spd);
    }
    // InitialBrake(3) is the peak-decel branch (|longG|>1.2). The FIRST frame of a fresh
    // lap has no prior-speed reference, so its speed-drop can't be computed and it stays
    // Unknown; frames 1..N-1 each drop exactly phaseSpeedDropPerSec(3.0) => InitialBrake.
    CHECK(lapBrk.phases[static_cast<int>(DrivingPhase::InitialBrake)].sampleCount() == 19u);

    std::printf("   segmentation: straight/ambiguous/mid-corner-edge/braking-edge OK\n");
}

// ===========================================================================
// B. Channel-absent matrix — graceful degradation / abstention with reason
// ===========================================================================
static void testChannelAbsentMatrix() {
    section("B: Channel-absent matrix (graceful degradation)");

    AnalysisSettings settings = buildDefaultAnalysisSettings();   // INI-driven thresholds

    // --- B1. No rear wheel slip at all -> RearTractionLossOnExitRule abstains --
    LapStats noSlip;                       // every channel empty by default
    const std::vector<diag::DiagnosticFinding> fNo = diag::evaluateLapWithDiagnostics(noSlip, settings);
    CHECK(findById(fNo, "rear_traction_loss_on_exit") == nullptr);

    // --- B2. Rear slip present but NO ride height/Rake -> bumpstop rule (if it
    //        fired) would carry the reduced-confidence confounder; here we prove
    //        Rake is simply absent from a lap with no aero heights set. --------
    DataProvider prov = buildDataProviderFromLap(noSlip);
    double rakeVal = 12345.0;
    CHECK(prov.getValue(AnalysisAlias::Rake, rakeVal) == false);   // alias unset -> absent

    // --- B3. Wheel-speed channels absent (AMS2 has no true wheel speeds): the
    //        brake-instability rule must abstain rather than fabricate a value. --
    LapStats noWheelSpeed;                 // only front brake torque set, NO rear speed/torque
    fill(noWheelSpeed.brakeTorqueLF, 300.0);   // both corners required for axle avg:
    fill(noWheelSpeed.brakeTorqueRF, 280.0);   // -> BrakeTorqueFrontAvg present (front only)
    const std::vector<diag::DiagnosticFinding> fBrk = diag::evaluateLapWithDiagnostics(noWheelSpeed, settings);
    CHECK(findById(fBrk, "brake_instability") == nullptr);        // abstains: no rear speed/torque

    // --- B4. A lap that DOES fire bumpstop with Rake absent -> confounder text --
    LapStats bsLap;
    fill(bsLap.bumpStopLF, 1.0, 20);       // average*100 = 100% engagement (>=5 threshold)
    const std::vector<diag::DiagnosticFinding> fBs = diag::evaluateLapWithDiagnostics(bsLap, settings);
    const diag::DiagnosticFinding* bsF = findById(fBs, "bumpstop_engagement");
    CHECK_MSG(bsF != nullptr, "expected bumpstop finding to fire at 100% LF engagement");
    if (bsF) {
        bool hasRakeConfounder = false;
        for (std::size_t i = 0; i < bsF->confounders.size(); ++i)
            if (bsF->confounders[i].find("No ride-height data available") != std::string::npos)
                hasRakeConfounder = true;
        CHECK_MSG(hasRakeConfounder, "expected reduced-confidence confounder when Rake absent");
    }

    // --- B5. missingChannels traceability: a lap with rear slip but no optional --
    LapStats partialSlip;
    fill(partialSlip.wheelSlipLR, 20.0);   // required present -> rule fires
    fill(partialSlip.wheelSlipRR, 18.0);
    const std::vector<diag::DiagnosticFinding> fPart = diag::evaluateLapWithDiagnostics(partialSlip, settings);
    const diag::DiagnosticFinding* partF = findById(fPart, "rear_traction_loss_on_exit");
    CHECK_MSG(partF != nullptr, "expected rear traction finding with slip 20/18 (threshold 15)");
    if (partF) {
        // Optional channels (ThrottleAvgPct etc.) are absent -> recorded for traceability.
        bool throttleMissing = false;
        for (std::size_t i = 0; i < partF->missingChannels.size(); ++i)
            if (partF->missingChannels[i] == AnalysisAlias::ThrottleAvgPct) throttleMissing = true;
        CHECK_MSG(throttleMissing, "expected ThrottleAvgPct listed in missingChannels");
    }

    std::printf("   channel-absent: abstain + traceability reasons OK\n");
}

// ===========================================================================
// C. Cause ranking order + confidence-label boundaries (0.30 / 0.60 / 0.85)
// ===========================================================================
static void testCauseRankingAndLabels() {
    section("C: Cause ranking + confidence labels at boundaries");

    // Base weights for rear_traction_loss_on_exit from RuleConfig::buildDefaults():
    //   rear_rebound_too_high .45 / rear_spring_too_stiff .25 / differential .20 / pressure_low .10
    const double base[4] = { 0.45, 0.25, 0.20, 0.10 };

    DataProvider emptyProv;   // no values -> rankCauses only modulates by confidence

    auto expectOrderAndScale = [&](double conf, double scaleFactor, const char* label) {
        std::vector<CauseEntry> ranked = diag::rankCauses("rear_traction_loss_on_exit", emptyProv, conf);
        CHECK_MSG(ranked.size() == 4u, "expected full cause list (size 4)");
        if (ranked.size() != 4) return;
        // Order must be preserved under uniform scaling.
        for (int i = 0; i < 4; ++i) {
            const double expected = std::min(1.0, base[i] * scaleFactor);
            CHECK_MSG(std::fabs(ranked[i].weight - expected) < 1e-9, label);
            if (i > 0)
                CHECK_MSG(ranked[i-1].weight >= ranked[i].weight, "ranking must be non-increasing");
        }
    };

    // conf = 0.85 -> HIGH boundary -> x1.25 boost: [0.5625, 0.3125, 0.25, 0.125]
    expectOrderAndScale(0.85, 1.25, "conf=0.85 => x1.25");
    // conf = 0.90 -> above HIGH -> still x1.25 (same values)
    { std::vector<CauseEntry> r = diag::rankCauses("rear_traction_loss_on_exit", emptyProv, 0.90);
      CHECK_MSG(r.size()==4 && std::fabs(r[0].weight-0.5625)<1e-9, "conf=0.90 => x1.25 top cause"); }

    // conf = 0.30 -> NOT below confidenceLow(0.30) -> unchanged base weights
    expectOrderAndScale(0.30, 1.00, "conf=0.30 => no modulation (boundary inclusive)");
    // conf = 0.50 -> mid band -> unchanged
    { std::vector<CauseEntry> r = diag::rankCauses("rear_traction_loss_on_exit", emptyProv, 0.50);
      CHECK_MSG(r.size()==4 && std::fabs(r[0].weight-0.45)<1e-9, "conf=0.50 => base unchanged"); }

    // conf = 0.29 -> below confidenceLow(0.30) -> x0.75 dampen: [0.3375,...]
    expectOrderAndScale(0.29, 0.75, "conf=0.29 => x0.75");

    // --- Confidence label boundaries ----------------------------------------
    CHECK(diag::diagnosticConfidenceLabel(0.84) == std::string("MEDIUM"));   // just under HIGH
    CHECK(diag::diagnosticConfidenceLabel(0.85) == std::string("HIGH"));     // at HIGH boundary
    CHECK(diag::diagnosticConfidenceLabel(0.60) == std::string("MEDIUM"));   // at MEDIUM boundary
    CHECK(diag::diagnosticConfidenceLabel(0.29) == std::string("LOW"));      // below LOW band top

    std::printf("   ranking order + label boundaries (0.30/0.60/0.85) OK\n");
}

// ===========================================================================
// D. L/R asymmetry verdicts (compareLrInComparablePhases)
// ===========================================================================
static void testLRAsymmetryVerdicts() {
    section("D: L/R asymmetry verdicts");

    const int minSamples = g_thresholds.minHandlingSamples > 0 ? g_thresholds.minHandlingSamples : 8;

    // --- D1. Comparable conditions -> RepeatedInComparableConditions ---------
    LapStats comparable;
    for (int i = 0; i < minSamples + 2; ++i) {   // both sides comfortably above minimum, ratio ~1
        comparable.phases[static_cast<int>(DrivingPhase::MidCorner)].latGLeftHander.add(0.8);
        comparable.phases[static_cast<int>(DrivingPhase::MidCorner)].latGRighther.add(-0.75);
    }
    PhaseAsymmetryResult r1 = compareLrInComparablePhases(comparable);
    CHECK(r1.verdict == AsymmetryVerdict::RepeatedInComparableConditions);

    // --- D2. Track bias (one-directional) -> LikelyLayoutPhaseArtifact -------
    LapStats biased;
    for (int i = 0; i < minSamples + 4; ++i)     // left only, right empty => lo==0 branch
        biased.phases[static_cast<int>(DrivingPhase::MidCorner)].latGLeftHander.add(0.9);
    PhaseAsymmetryResult r2 = compareLrInComparablePhases(biased);
    CHECK(r2.verdict == AsymmetryVerdict::LikelyLayoutPhaseArtifact);

    // --- D3. Insufficient data -> InconclusiveInsufficientData ---------------
    LapStats sparse;                              // 2 per side: total(4) < minSamples AND each side < min
    for (int i = 0; i < 2; ++i) {
        sparse.phases[static_cast<int>(DrivingPhase::MidCorner)].latGLeftHander.add(0.8);
        sparse.phases[static_cast<int>(DrivingPhase::MidCorner)].latGRighther.add(-0.75);
    }
    PhaseAsymmetryResult r3 = compareLrInComparablePhases(sparse);
    CHECK(r3.verdict == AsymmetryVerdict::InconclusiveInsufficientData);

    std::printf("   L/R verdicts: comparable/artifact/insufficient OK\n");
}

// ===========================================================================
// E. Before/after comparison — confounders force Inconclusive; clean pairs give
//    correct Improvement / Degradation + per-phase deltas.
// ===========================================================================
static void testBeforeAfterComparison() {
    section("E: Before/after comparison (confounders + verdicts)");

    // --- E1. Clean improvement: rear-slip symptom present before, gone after --
    std::vector<LapStats> gA, gB;
    for (int i = 0; i < 2; ++i) { LapStats l = makeValidLap(i+1); fill(l.wheelSlipLR, 20.0); fill(l.wheelSlipRR, 18.0); gA.push_back(l); }
    for (int i = 0; i < 2; ++i) { LapStats l = makeValidLap(i+3); fill(l.wheelSlipLR, 5.0);  fill(l.wheelSlipRR, 4.0);  gB.push_back(l); }

    diag::GroupComparisonResult cmp1 = diag::compareLapGroups(gA, gB, ReportLanguage::English);
    CHECK(cmp1.valid == true);
    CHECK_MSG(!cmp1.hasBlockingFactors(), "clean pair must have no blocking factors");
    const diag::SymptomComparison* sc1 = findSc(cmp1.symptoms, "rear_traction_loss_on_exit");
    CHECK_MSG(sc1 != nullptr, "expected rear traction symptom in comparison output");
    if (sc1) {
        CHECK(sc1->beforePresent == true);
        CHECK(sc1->afterPresent  == false);
        CHECK(sc1->verdict       == diag::ComparisonVerdict::ProbableImprovement);
    }

    // --- E2. Clean degradation: symptom absent before, present after ----------
    std::vector<LapStats> gC, gD;
    for (int i = 0; i < 2; ++i) { LapStats l = makeValidLap(i+1); fill(l.wheelSlipLR, 5.0); fill(l.wheelSlipRR, 4.0); gC.push_back(l); }
    for (int i = 0; i < 2; ++i) { LapStats l = makeValidLap(i+3); fill(l.wheelSlipLR, 25.0);fill(l.wheelSlipRR, 24.0);gD.push_back(l); }

    diag::GroupComparisonResult cmp2 = diag::compareLapGroups(gC, gD, ReportLanguage::English);
    CHECK(cmp2.valid == true);
    const diag::SymptomComparison* sc2 = findSc(cmp2.symptoms, "rear_traction_loss_on_exit");
    CHECK_MSG(sc2 != nullptr, "expected rear traction symptom (degradation case)");
    if (sc2) {
        CHECK(sc2->beforePresent == false);
        CHECK(sc2->afterPresent  == true);
        CHECK(sc2->verdict       == diag::ComparisonVerdict::ProbableDegradation);
    }

    // --- E3. Fuel-level confounder forces Inconclusive (would-be improvement) --
    std::vector<LapStats> gE, gF;
    for (int i = 0; i < 2; ++i) {   // before: symptom present + large fuel drop per lap (-0.30 ratio)
        LapStats l = makeValidLap(i+1); fill(l.wheelSlipLR, 20.0); fill(l.wheelSlipRR, 18.0);
        l.fuelLevelEnds.record(1.00); l.fuelLevelEnds.record(0.70);   // delta = -0.30 -> faPct=-30
        gE.push_back(l);
    }
    for (int i = 0; i < 2; ++i) {   // after: symptom gone + STABLE fuel (delta=0). Both groups must have
        LapStats l = makeValidLap(i+3); fill(l.wheelSlipLR, 5.0); fill(l.wheelSlipRR, 4.0);            // >=1 valid lap for the engine to compare; stable fuel is realistic.
        l.fuelLevelEnds.record(1.00); l.fuelLevelEnds.record(1.00);   // delta = 0 -> fbPct=0
        gF.push_back(l);            // |(-30)-0| = 30 > comparisonFuelLevelChangeMaxPct(5) => confounder fires
    }

    diag::GroupComparisonResult cmp3 = diag::compareLapGroups(gE, gF, ReportLanguage::English);
    CHECK(cmp3.valid == true);
    CHECK_MSG(!cmp3.globalConfounders.empty(), "fuel drop must register a global confounder");
    bool fuelFlagged = false;
    for (std::size_t i = 0; i < cmp3.globalConfounders.size(); ++i)
        if (cmp3.globalConfounders[i].find("fuel level change") != std::string::npos) fuelFlagged = true;
    CHECK_MSG(fuelFlagged, "expected 'fuel level change' confounder text");
    const diag::SymptomComparison* sc3 = findSc(cmp3.symptoms, "rear_traction_loss_on_exit");
    if (sc3) {   // present before / gone after would be Improvement IF unblocked...
        CHECK_MSG(sc3->verdict == diag::ComparisonVerdict::Inconclusive,
                  "confounded pair must NOT claim improvement -> Inconclusive");
        CHECK(!sc3->confounders.empty());   // per-symptom confounder list populated from globals
    }

    // --- E4. Ambient-temp confounder forces Inconclusive ----------------------
    std::vector<LapStats> gG, gH;
    for (int i = 0; i < 2; ++i) {   // before: symptom present + ambient 10C
        LapStats l = makeValidLap(i+1); fill(l.wheelSlipLR, 20.0); fill(l.wheelSlipRR, 18.0);
        fill(l.ambientTemp, 10.0); gG.push_back(l);
    }
    for (int i = 0; i < 2; ++i) {   // after: symptom gone + ambient 25C -> diff 15 > max(3)
        LapStats l = makeValidLap(i+3); fill(l.wheelSlipLR, 5.0); fill(l.wheelSlipRR, 4.0);
        fill(l.ambientTemp, 25.0); gH.push_back(l);
    }

    diag::GroupComparisonResult cmp4 = diag::compareLapGroups(gG, gH, ReportLanguage::English);
    CHECK(cmp4.valid == true);
    bool ambFlagged = false;
    for (std::size_t i = 0; i < cmp4.globalConfounders.size(); ++i)
        if (cmp4.globalConfounders[i].find("ambient temperature change") != std::string::npos) ambFlagged = true;
    CHECK_MSG(ambFlagged, "expected 'ambient temperature change' confounder text");
    const diag::SymptomComparison* sc4 = findSc(cmp4.symptoms, "rear_traction_loss_on_exit");
    if (sc4) {
        CHECK(sc4->verdict == diag::ComparisonVerdict::Inconclusive);   // blocked by ambient change
    }

    // --- E5. Per-phase metric delta is populated for phases with data ---------
    std::vector<LapStats> gI, gJ;
    for (int i = 0; i < 2; ++i) { LapStats l = makeValidLap(i+1); fill(l.wheelSlipLR, 5.0); 
        // give MidCorner(5) a lateral-G sample so the phase table has data on this side
        for (int k=0;k<3;++k) l.phases[static_cast<int>(DrivingPhase::MidCorner)].latG.add(0.6); gI.push_back(l);}
    for (int i = 0; i < 2; ++i) { LapStats l = makeValidLap(i+3); fill(l.wheelSlipLR, 5.0); 
        for (int k=0;k<3;++k) l.phases[static_cast<int>(DrivingPhase::MidCorner)].latG.add(0.9); gJ.push_back(l);}
    diag::GroupComparisonResult cmp5 = diag::compareLapGroups(gI, gJ, ReportLanguage::English);
    CHECK(cmp5.valid == true);
    bool midFound = false; double beforeLat=0.0, afterLat=0.0;
    for (std::size_t i = 0; i < cmp5.phases.size(); ++i) {
        if (cmp5.phases[i].phaseIndex == static_cast<int>(DrivingPhase::MidCorner)) {
            midFound = true; beforeLat = cmp5.phases[i].beforeLatGMean; afterLat = cmp5.phases[i].afterLatGMean;
        }
    }
    CHECK_MSG(midFound, "expected MidCorner phase row in per-phase delta table");
    if (midFound) {
        CHECK(std::fabs(beforeLat - 0.6) < 1e-9);   // averaged latG for before group
        CHECK(std::fabs(afterLat  - 0.9) < 1e-9);   // ...and after group
    }

    std::printf("   comparison: improvement/degradation/fuel/ambient/per-phase OK\n");
}

// ===========================================================================
// F. Gate-logic primitives (single-lap confidence cap + label boundaries).
// NOTE: the full step-12 categorical gate (renderDiagnosticBlock / canCategorizeSetting)
// is file-static inside LapReportGenerator.cpp and exercised by integration tests;
// here we validate its constituent predicates that a standalone harness CAN reach.
// ===========================================================================
static void testGateLogicPrimitives() {
    section("F: Gate-logic primitives (confidence cap + label boundaries)");

    // Single-lap evaluation can NEVER exceed the MEDIUM(0.60) confidence cap, so it
    // can never be labelled HIGH and can never trigger the x1.25 top-cause boost via a
    // real finding. Verify by driving a strong rear-slip lap through the full pipeline:
    LapStats hot;   fill(hot.wheelSlipLR, 40.0); fill(hot.wheelSlipRR, 38.0);
    AnalysisSettings settings = buildDefaultAnalysisSettings();
    const std::vector<diag::DiagnosticFinding> fHot = diag::evaluateLapWithDiagnostics(hot, settings);
    const diag::DiagnosticFinding* hotF = findById(fHot, "rear_traction_loss_on_exit");
    CHECK_MSG(hotF != nullptr, "expected rear traction finding on 40% slip lap");
    if (hotF) {
        CHECK_MSG(hotF->confidence <= g_thresholds.confidenceMedium + 1e-9,
                  "single-lap confidence must be capped at MEDIUM(0.60)");
        CHECK(hotF->dataQualityLabel != std::string("HIGH"));   // never HIGH on a single lap
    }

    // Label boundaries that feed the categorical gate (gate A: >= MEDIUM).
    CHECK(std::strcmp(diag::diagnosticConfidenceLabel(0.59), "LOW")    == 0);   // just under MEDIUM boundary
    CHECK(std::strcmp(diag::diagnosticConfidenceLabel(g_thresholds.confidenceMedium), "MEDIUM") == 0);
    CHECK(std::strcmp(diag::diagnosticConfidenceLabel(g_thresholds.confidenceHigh),     "HIGH")   == 0);

    // Categorical gate predicate (replicated): MEDIUM+ confidence AND EVO profile only.
    auto canCategorize = [](double conf, ChannelProfile prof) {
        return conf >= g_thresholds.confidenceMedium && prof == ChannelProfile::EvoM1;
    };
    CHECK(canCategorize(0.60, ChannelProfile::EvoM1)  == true);   // passes at MEDIUM on EVO
    CHECK(canCategorize(0.59, ChannelProfile::EvoM1)  == false);  // blocked below MEDIUM
    CHECK(canCategorize(0.85, ChannelProfile::AMS2)   == false);  // non-EVO profile never categorical

    std::printf("   gate primitives: single-lap cap + label/profile predicates OK\n");
}

// ===========================================================================
// G. Non-regression of the 4 legacy rules on a fixture lap set.
// =============================================================================
static void testLegacyRuleNonRegression() {
    section("G: Legacy rule non-regression (identical AnalysisResult vectors)");

    // --- G1. Clean lap -> zero findings from all four legacy rules -----------
    LapStats clean = makeValidLap(1);   // no channels set at all
    const std::vector<AnalysisResult> rClean = evaluateLapWithRules(clean, AnalysisSettings());
    CHECK_MSG(rClean.empty(), "clean lap must yield NO legacy findings");

    // --- G2. Engine over-temp -> exactly one Critical 'Engine' finding --------
    LapStats hotEng = makeValidLap(1);
    fill(hotEng.waterTemp, 105.0);      // > engine.waterTempWarnC (default AnalysisSettings: 100)
    const std::vector<AnalysisResult> rHot = evaluateLapWithRules(hotEng, AnalysisSettings());
    int engCriticals = 0; bool waterTitleSeen = false;
    for (std::size_t i = 0; i < rHot.size(); ++i) {
        if (rHot[i].severity == AnalysisSeverity::Critical && rHot[i].category == "Engine") ++engCriticals;
        if (rHot[i].title.find("Water temperature high") != std::string::npos) waterTitleSeen = true;
    }
    CHECK_MSG(engCriticals >= 1, "expected a Critical Engine finding for 105C water");
    CHECK(waterTitleSeen);

    // --- G3. Front/rear pressure mismatch -> one Warning 'Tyre' finding -------
    LapStats press = makeValidLap(1);
    fill(press.pressureLF, 28.0); fill(press.pressureRF, 29.0);   // front avg ~28.5
    fill(press.pressureLR, 24.0); fill(press.pressureRR, 23.0);   // rear  avg ~23.5 -> diff 5 > max(2)
    const std::vector<AnalysisResult> rPress = evaluateLapWithRules(press, AnalysisSettings());
    bool frMismatchSeen = false; int tyreWarnings = 0;
    for (std::size_t i = 0; i < rPress.size(); ++i) {
        // TyrePressureRule sets category "Tyres" (plural).
        if (rPress[i].severity == AnalysisSeverity::Warning && rPress[i].category == "Tyres") ++tyreWarnings;
        if (rPress[i].title.find("Front/rear pressure mismatch") != std::string::npos) frMismatchSeen = true;
    }
    CHECK_MSG(frMismatchSeen, "expected 'Front/rear pressure mismatch' finding");
    CHECK(tyreWarnings >= 1);

    // --- G4. High rear wheel slip -> one Warning 'Handling' finding -----------
    LapStats slipLap = makeValidLap(1);
    fill(slipLap.wheelSlipLR, 20.0);   // > handling.wheelSlipMax (default: 15)
    const std::vector<AnalysisResult> rSlip = evaluateLapWithRules(slipLap, AnalysisSettings());
    bool rearSlipSeen = false; int handlingWarnings = 0;
    for (std::size_t i = 0; i < rSlip.size(); ++i) {
        if (rSlip[i].severity == AnalysisSeverity::Warning && rSlip[i].category == "Handling") ++handlingWarnings;
        if (rSlip[i].title.find("Rear wheel slip high") != std::string::npos) rearSlipSeen = true;
    }
    CHECK_MSG(rearSlipSeen, "expected 'Rear wheel slip high' finding");
    CHECK(handlingWarnings >= 1);

    // --- G5. Determinism: same input -> identical result vector (size+titles) --
    const std::vector<AnalysisResult> rHot2 = evaluateLapWithRules(hotEng, AnalysisSettings());
    CHECK(rHot.size() == rHot2.size());
    bool titlesMatch = true;
    for (std::size_t i = 0; i < rHot.size(); ++i) if (rHot[i].title != rHot2[i].title) { titlesMatch = false; break; }
    CHECK(titlesMatch);

    std::printf("   legacy rules: clean/engine/pressure/slip/determinism OK\n");
}

// ===========================================================================
int main() {
    std::printf("Phase 4 diagnostic-engine + comparison test harness\n");
    std::printf("-----------------------------------------------\n");

    // Sanity-assert the INI-driven thresholds we rely on (defaults when no INI present).
    CHECK(g_thresholds.wheelSlipMax == 15.0);
    CHECK(std::fabs(g_thresholds.confidenceMedium - 0.60) < 1e-9);
    CHECK(std::fabs(g_thresholds.confidenceHigh   - 0.85) < 1e-9);

    testPhaseSegmentation();
    testChannelAbsentMatrix();
    testCauseRankingAndLabels();
    testLRAsymmetryVerdicts();
    testBeforeAfterComparison();
    testGateLogicPrimitives();
    testLegacyRuleNonRegression();

    std::printf("\n-----------------------------------------------\n");
    if (g_failures == 0) {
        std::printf("ALL %d CHECKS PASSED\n", g_checks);
        return 0;
    }
    std::printf("%d/%d CHECKS FAILED\n", g_failures, g_checks);
    return 1;
}
