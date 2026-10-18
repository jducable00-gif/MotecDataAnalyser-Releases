//---------------------------------------------------------------------------
// ComparisonEngine.h — Phase 4: before/after lap-group comparison.
//
// Compares two groups of laps (e.g. "before a setup change" vs "after") and
// produces per-symptom verdicts with explicit confounders, following the plan
// in docs/IMPLEMENTATION_PLAN_DIAGNOSTIC_ENGINE.md step 14:
//   - alignment check (same track / car where metadata allows); mismatches are
//     reported as confounders, never silently ignored;
//   - per-phase metric comparison between the two sets;
//   - per-symptom verdict ProbableImprovement / ProbableDegradation /
//     Inconclusive with explicit confounder list (conditions, fuel level
//     change, driving style variance);
//   - when ANY global blocking factor is present (alignment issue or condition
//     change), every symptom verdict is forced to Inconclusive — a false
//     "improvement" claim must never be emitted for an uncontrolled pair of runs;
//   - the user-provided setup delta is free text: it is displayed verbatim and
//     NEVER parsed for attribution. When more than one parameter changed, a
//     no-auto-attribution notice is emitted instead of any cause claim.
//
// Header-only (inline), bcc32c-safe: C++03 constructs only, no address-of
// scoped-enum rvalues, declaration-before-use respected within this TU, all
// user-facing strings localized via inline EN/FR ternaries at render time or
// through the lang parameter. French text is deliberately accent-free (the
// harness and report writers are pure ASCII by design).
//---------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>   // std::make_pair
#include <vector>

#include "MotecSetupAnalyzer.h"   // LapStats, AnalysisSeverity, ReportLanguage, g_thresholds, buildAverageLapData()
#include "DiagnosticEngine.h"     // diag::evaluateLapWithDiagnostics(), DiagnosticFinding

namespace diag {

// ===============================
// Comparison types (Phase 4)
// ===============================

enum class ComparisonVerdict {
    ProbableImprovement = 0,   // symptom present before, gone / milder after — and no global confounder
    ProbableDegradation = 1,   // new or worse in the "after" group (no global confounder)
    Inconclusive        = 2    // not enough evidence, no clear direction, OR conditions changed between groups
};

struct SymptomComparison {
    std::string symptomId;
    bool beforePresent = false;
    bool afterPresent  = false;
    AnalysisSeverity beforeSeverity = AnalysisSeverity::Info;   // valid when beforePresent
    AnalysisSeverity afterSeverity  = AnalysisSeverity::Info;   // valid when afterPresent
    double beforeConfidence = 0.0;                              // internal score [0..1] (pre-label)
    double afterConfidence  = 0.0;
    ComparisonVerdict verdict = ComparisonVerdict::Inconclusive;
    std::vector<std::string> confounders;   // explicit list attached to this symptom's verdict

    bool empty() const { return symptomId.empty(); }
};

// Per-phase metric delta (one entry per driving phase that has data on at least one side).
struct PhaseMetricDelta {
    int phaseIndex = 0;                 // DrivingPhase value (1..6, Unknown=0 excluded)
    bool beforeHasData = false;         // lateral G samples present in "before" group average
    bool afterHasData  = false;
    double beforeLatGMean     = 0.0;    // g   — valid when beforeHasData
    double afterLatGMean      = 0.0;    // g   — valid when afterHasData
    bool hasBeforeSlipFront = false;    // front-axle slip samples present (not every profile maps them)
    bool hasAfterSlipFront  = false;
    double beforeSlipFrontPct = 0.0;    // % — valid when hasBeforeSlipFront
    double afterSlipFrontPct  = 0.0;    // % — valid when hasAfterSlipFront
};

struct GroupComparisonResult {
    bool valid = false;                 // false when a group has <2 usable laps (reason in invalidReason)
    std::string invalidReason;          // localized, only meaningful when !valid

    int beforeLapCount = 0;             // number of laps supplied per side (informational)
    int afterLapCount  = 0;

    std::vector<SymptomComparison> symptoms;   // one entry per symptom whose state changed or is present on a side
    std::vector<PhaseMetricDelta> phases;      // per-phase metric comparison
    std::vector<std::string> alignmentIssues;  // track / car metadata mismatches between groups (block all verdicts)
    std::vector<std::string> globalConfounders;// condition changes that invalidate any verdict for this pair of runs

    bool hasBlockingFactors() const { return !alignmentIssues.empty() || !globalConfounders.empty(); }
};

// ===============================
// Small helpers (also used by the standalone test harness)
// ===============================

inline int severityRank(AnalysisSeverity s) {
    switch (s) {
        case AnalysisSeverity::Critical: return 2;
        case AnalysisSeverity::Warning:  return 1;
        default:                         return 0;   // Info
    }
}

inline std::string verdictLabel(ComparisonVerdict v, ReportLanguage lang) {
    const bool fr = (lang == ReportLanguage::French);
    switch (v) {
        case ComparisonVerdict::ProbableImprovement: return fr ? "Amelioration probable" : "Probable improvement";
        case ComparisonVerdict::ProbableDegradation: return fr ? "Aggravation probable"  : "Probable degradation";
        default:                                     return fr ? "Non concluant"         : "Inconclusive";
    }
}

// Localized phase display name for the comparison table (driving phases 1..6).
inline std::string phaseName(int phaseIndex) {
    switch (phaseIndex) {
        case 1: return "StraightStableAccel";   // Straight / stable acceleration
        case 2: return "BrakingApproach";       // Braking approach
        case 3: return "InitialBrake";          // Initial brake application
        case 4: return "CornerEntryRelease";    // Corner entry (brake release)
        case 5: return "MidCorner";             // Mid-corner
        case 6: return "ExitReThrottle";        // Exit / re-throttle
        default: return "Unknown";
    }
}

// Severity word used in the comparison table.
inline std::string severityWord(AnalysisSeverity s) {
    switch (s) {
        case AnalysisSeverity::Critical: return "CRITICAL";
        case AnalysisSeverity::Warning:  return "WARNING";
        default:                         return "INFO";
    }
}

// snake_case -> Title Case for symptom ids in the comparison table.
inline std::string humanizeSymptomId(const std::string& id) {
    if (id.empty()) return id;
    std::string out;
    bool up = true;
    for (std::size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (c == '_') { up = true; continue; }
        out += (up && c >= 'a' && c <= 'z') ? static_cast<char>(static_cast<unsigned char>(c) - 32u) : c;
        up = false;
    }
    return out.empty() ? id : out;
}

// Count of distinct setup changes in the user's free-text delta note.
// Purely informational (drives the no-auto-attribution notice). The text is
// NEVER parsed for attribution itself — only comma/semicolon-separated parts
// are counted, so this can never be used to claim a cause.
inline int countSetupChanges(const std::string& setupDelta) {
    if (setupDelta.empty()) return 0;
    int nonEmpty = 0;
    std::size_t start = 0;
    for (;;) {
        const std::size_t sep = setupDelta.find_first_of(",;", start);
        const bool lastPart = (sep == std::string::npos);
        const std::size_t endPos = lastPart ? setupDelta.size() : sep;
        bool hasContent = false;
        for (std::size_t k = start; k < endPos && !hasContent; ++k) {
            const char c = setupDelta[k];
            if (c != ' ' && c != '\t' && c != ',' && c != ';') hasContent = true;
        }
        if (hasContent) ++nonEmpty;
        if (lastPart) break;
        start = sep + 1;
    }
    return nonEmpty;
}

// ===============================
// Core comparison entry point
// ===============================

inline GroupComparisonResult compareLapGroups(const std::vector<LapStats>& groupA,   // "before"
                                              const std::vector<LapStats>& groupB,   // "after"
                                              ReportLanguage lang = ReportLanguage::English) {
    auto tr = [&](const char* en, const char* fr) -> std::string {
        return (lang == ReportLanguage::French) ? fr : en;
    };

    GroupComparisonResult out;
    out.beforeLapCount = static_cast<int>(groupA.size());
    out.afterLapCount  = static_cast<int>(groupB.size());

    // --- Per-group averaging + diagnostic evaluation ------------------------
    const LapStats avgA = buildAverageLapData(groupA);   // empty (rowCount==0) when <2 usable laps
    const LapStats avgB = buildAverageLapData(groupB);
    if (avgA.rowCount == 0 || avgB.rowCount == 0) {
        out.valid = false;
        out.invalidReason = tr("Comparison needs at least 2 valid laps per group.",
                               "La comparaison exige au moins 2 tours valables par groupe.");
        return out;
    }

    // Use the same INI-driven thresholds as the main report pipeline (LapReportGenerator::generate)
    // so that e.g. handling.wheelSlipMax is never 0 and RearTractionLossOnExitRule can fire here too.
    AnalysisSettings settingsA = buildDefaultAnalysisSettings();
    settingsA.language = lang;   // localized findings: evidence text matches report language
    const std::vector<DiagnosticFinding> findingsA = evaluateLapWithDiagnostics(avgA, settingsA);
    const std::vector<DiagnosticFinding> findingsB = evaluateLapWithDiagnostics(avgB, settingsA);

    // --- Alignment check: track / car metadata where available --------------
    if (!groupA.front().trackName.empty() && !groupB.front().trackName.empty()
        && groupA.front().trackName != groupB.front().trackName) {
        out.alignmentIssues.push_back(tr("Track changed between groups", "Piste changee entre les groupes") + ": "
                                      + groupA.front().trackName + " -> " + groupB.front().trackName);
    }
    if (!groupA.front().vehicleName.empty() && !groupB.front().vehicleName.empty()
        && groupA.front().vehicleName != groupB.front().vehicleName) {
        out.alignmentIssues.push_back(tr("Car changed between groups", "Voiture changee entre les groupes") + ": "
                                      + groupA.front().vehicleName + " -> " + groupB.front().vehicleName);
    }

    // --- Global confounders --------------------------------------------------
    // Fuel level: LapDelta stores the raw ratio (0..1) recorded per frame; delta() is a RATIO.
    auto avgFuelRatio = [](const std::vector<LapStats>& g, int& nOut) {
        double sum = 0.0; int n = 0;
        for (std::size_t i = 0; i < g.size(); ++i) {
            if (g[i].fuelLevelEnds.valid()) { sum += g[i].fuelLevelEnds.delta(); ++n; }
        }
        nOut = n;
        return std::make_pair(n, static_cast<double>(sum));   // caller divides by first when >0
    };
    int na = 0, nb = 0;
    const double fuelSumA = avgFuelRatio(groupA, na).second;
    const double fuelSumB = avgFuelRatio(groupB, nb).second;
    if (na > 0 && nb > 0) {
        const double faPct = 100.0 * fuelSumA / static_cast<double>(na);   // mean per-lap consumption change (%)
        const double fbPct = 100.0 * fuelSumB / static_cast<double>(nb);
        if (std::abs(faPct - fbPct) > g_thresholds.comparisonFuelLevelChangeMaxPct) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), " (%.1f%% -> %.1f%%)", faPct, fbPct);   // numbers only: locale-safe
            out.globalConfounders.push_back(tr("fuel level change", "variation du niveau de carburant") + buf);
        }
    }

    // Ambient temperature per group (mean of lap means).
    double ambSumA = 0.0; int nAmbA = 0;
    for (std::size_t i = 0; i < groupA.size(); ++i)
        if (groupA[i].ambientTemp.count > 0) { ambSumA += groupA[i].ambientTemp.average(); ++nAmbA; }
    double ambSumB = 0.0; int nAmbB = 0;
    for (std::size_t i = 0; i < groupB.size(); ++i)
        if (groupB[i].ambientTemp.count > 0) { ambSumB += groupB[i].ambientTemp.average(); ++nAmbB; }
    if (nAmbA > 0 && nAmbB > 0) {
        const double a = ambSumA / static_cast<double>(nAmbA);
        const double b = ambSumB / static_cast<double>(nAmbB);
        if (std::abs(a - b) > g_thresholds.comparisonAmbientTempDiffMaxC) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), " (%.1f C -> %.1f C)", a, b);   // numbers only: locale-safe
            out.globalConfounders.push_back(tr("ambient temperature change", "variation de la temperature ambiante") + buf);
        }
    }

    // Driving-style variance: segmentation-confidence spread between groups.
    double confA = 0.0, confB = 0.0; int nConfA = 0, nConfB = 0;
    for (std::size_t i = 0; i < groupA.size(); ++i) { confA += groupA[i].segmentationConfidence(); ++nConfA; }
    for (std::size_t i = 0; i < groupB.size(); ++i) { confB += groupB[i].segmentationConfidence(); ++nConfB; }
    if (nConfA > 0 && nConfB > 0) {
        const double spread = std::abs(confA / static_cast<double>(nConfA) - confB / static_cast<double>(nConfB));
        if (spread >= g_thresholds.comparisonStyleVarianceMin) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), " (%.0f%% -> %.0f%%)", 100.0 * confA / static_cast<double>(nConfA),
                          100.0 * confB / static_cast<double>(nConfB));   // numbers only: locale-safe
            out.globalConfounders.push_back(tr("driving style variance", "variance de style de conduite") + buf);
        }
    }

    // --- Per-phase metric comparison -----------------------------------------
    for (int p = 1; p < kDrivingPhaseCount; ++p) {   // skip Unknown(0): never guessed, nothing to compare
        const PhaseAccumulator& pa = avgA.phases[p];
        const PhaseAccumulator& pb = avgB.phases[p];
        if (pa.latG.count == 0 && pb.latG.count == 0) continue;   // no data on either side -> skip phase

        PhaseMetricDelta d;
        d.phaseIndex         = p;
        d.beforeHasData      = (pa.latG.count > 0);
        d.afterHasData       = (pb.latG.count > 0);
        if (d.beforeHasData) {
            d.beforeLatGMean     = pa.latG.average();
            d.hasBeforeSlipFront = (pa.wheelSlipFrontAvg.count > 0);
            if (d.hasBeforeSlipFront) d.beforeSlipFrontPct = pa.wheelSlipFrontAvg.average();
        }
        if (d.afterHasData) {
            d.afterLatGMean      = pb.latG.average();
            d.hasAfterSlipFront  = (pb.wheelSlipFrontAvg.count > 0);
            if (d.hasAfterSlipFront) d.afterSlipFrontPct = pb.wheelSlipFrontAvg.average();
        }
        out.phases.push_back(d);
    }

    // --- Per-symptom verdicts --------------------------------------------------
    const bool blocked = out.hasBlockingFactors();   // any alignment issue or global confounder forces Inconclusive everywhere

    auto findFinding = [](const std::vector<DiagnosticFinding>& v, const std::string& id) -> const DiagnosticFinding* {
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i].symptomId == id) return &v[i];
        return nullptr;
    };

    // Union of symptom ids from both sides, preserving "before" order then new "after"-only ones.
    std::vector<std::string> allIds;
    for (std::size_t i = 0; i < findingsA.size(); ++i) {
        bool seen = false;
        for (std::size_t k = 0; k < allIds.size() && !seen; ++k) if (allIds[k] == findingsA[i].symptomId) seen = true;
        if (!seen) allIds.push_back(findingsA[i].symptomId);
    }
    for (std::size_t i = 0; i < findingsB.size(); ++i) {
        bool seen = false;
        for (std::size_t k = 0; k < allIds.size() && !seen; ++k) if (allIds[k] == findingsB[i].symptomId) seen = true;
        if (!seen) allIds.push_back(findingsB[i].symptomId);
    }

    for (std::size_t i = 0; i < allIds.size(); ++i) {
        const DiagnosticFinding* fa = findFinding(findingsA, allIds[i]);
        const DiagnosticFinding* fb = findFinding(findingsB, allIds[i]);
        if (!fa && !fb) continue;   // defensive: cannot happen given the union above

        SymptomComparison sc;
        sc.symptomId      = allIds[i];
        sc.beforePresent  = (fa != nullptr);
        sc.afterPresent   = (fb != nullptr);
        if (fa) { sc.beforeSeverity = fa->severity; sc.beforeConfidence = fa->confidence; }
        if (fb) { sc.afterSeverity  = fb->severity; sc.afterConfidence  = fb->confidence; }

        ComparisonVerdict v = ComparisonVerdict::Inconclusive;   // default: blocked OR no clear direction
        if (!blocked) {
            const int ra = severityRank(sc.beforePresent ? sc.beforeSeverity : AnalysisSeverity::Info);
            const int rb = severityRank(sc.afterPresent  ? sc.afterSeverity  : AnalysisSeverity::Info);
            if (sc.beforePresent && !sc.afterPresent)      v = ComparisonVerdict::ProbableImprovement;   // gone after
            else if (!sc.beforePresent && sc.afterPresent) v = ComparisonVerdict::ProbableDegradation;   // new after
            else {                                            // present on both sides: compare severity direction
                if (rb < ra)      v = ComparisonVerdict::ProbableImprovement;   // milder after
                else if (rb > ra) v = ComparisonVerdict::ProbableDegradation;   // worse after
                // equal rank -> Inconclusive ("still present in both groups")
            }
        }
        sc.verdict     = v;
        for (std::size_t k = 0; k < out.alignmentIssues.size(); ++k)    sc.confounders.push_back(out.alignmentIssues[k]);
        for (std::size_t k = 0; k < out.globalConfounders.size(); ++k)  sc.confounders.push_back(out.globalConfounders[k]);

        // Skip symptoms with no change and no blocking factor: nothing to report.
        const bool unchangedBothPresentSameSeverity =
            sc.beforePresent && sc.afterPresent && severityRank(sc.beforeSeverity) == severityRank(sc.afterSeverity);
        if (unchangedBothPresentSameSeverity && !blocked) continue;

        out.symptoms.push_back(sc);
    }

    out.valid = true;
    return out;
}

// ===============================
// Rendering helper for the report pipeline / tablet JSON
// ===============================

inline std::string buildComparisonSection(const GroupComparisonResult& cmp, const std::string& setupDelta, ReportLanguage lang) {
    auto tr = [&](const char* en, const char* fr) -> std::string {
        return (lang == ReportLanguage::French) ? fr : en;
    };

    if (!cmp.valid)
        return cmp.invalidReason.empty() ? "" : ("-- " + tr("Before/After comparison", "Comparaison avant/apres") + ": " + cmp.invalidReason);

    std::string s = "\n\n";
    s += "-- " + tr("BEFORE / AFTER COMPARISON", "COMPARAISON AVANT / APRES");
    char buf[128];
    std::snprintf(buf, sizeof(buf), " (%d vs %d laps)", cmp.beforeLapCount, cmp.afterLapCount);   // numbers only: locale-safe
    s += buf;

    if (!setupDelta.empty()) {
        s += "\n" + tr("Known setup delta (user-provided): ", "Ecart de reglage connu (fourni par l'utilisateur) : ") + setupDelta;
        const int changes = countSetupChanges(setupDelta);
        if (changes >= 2)
            s += "\n" + tr("Multiple parameters changed between runs - no automatic attribution to a single cause.",
                           "Plusieurs parametres ont change entre les passages - aucune attribution automatique a une seule cause.");
    }

    // Alignment issues / global confounders (block every verdict).
    if (!cmp.alignmentIssues.empty() || !cmp.globalConfounders.empty()) {
        s += "\n" + tr("Conditions not comparable between groups:", "Conditions non comparables entre les groupes :");
        for (std::size_t i = 0; i < cmp.alignmentIssues.size(); ++i)   s += "\n - [!] " + cmp.alignmentIssues[i];
        for (std::size_t i = 0; i < cmp.globalConfounders.size(); ++i) s += "\n - [~] " + cmp.globalConfounders[i];
    }

    // Per-phase metrics.
    if (!cmp.phases.empty()) {
        s += "\n" + tr("Per-phase comparison (mean lateral G / front slip):",
                       "Comparaison par phase (G laterale moyenne / glissement avant) :");
        for (std::size_t i = 0; i < cmp.phases.size(); ++i) {
            const PhaseMetricDelta& d = cmp.phases[i];
            char line[256];
            if (d.beforeHasData && d.afterHasData) {
                std::snprintf(line, sizeof(line), " - %s: %.2fg / ", phaseName(d.phaseIndex).c_str(), d.beforeLatGMean);   // numbers only after c_str() arg
                s += line;
                if (d.hasBeforeSlipFront && d.hasAfterSlipFront) {
                    char t[160];
                    std::snprintf(t, sizeof(t), "%.1f%%  ->  %.2fg / %.1f%%", d.beforeSlipFrontPct, d.afterLatGMean, d.afterSlipFrontPct);   // numbers only: locale-safe
                    s += t;
                } else {
                    char t[96];
                    std::snprintf(t, sizeof(t), "n/a  ->  %.2fg / n/a", d.afterLatGMean);   // numbers only: locale-safe
                    s += t;
                }
            } else if (d.beforeHasData) {
                char t[160];
                std::snprintf(t, sizeof(t), " - %s: %.2fg / n/a  ->  n/a", phaseName(d.phaseIndex).c_str(), d.beforeLatGMean);   // numbers only after c_str() arg
                s += t;
            } else {
                char t[160];
                std::snprintf(t, sizeof(t), " - %s: n/a  ->  %.2fg / n/a", phaseName(d.phaseIndex).c_str(), d.afterLatGMean);   // numbers only after c_str() arg
                s += t;
            }
        }
    }

    // Per-symptom verdicts.
    if (!cmp.symptoms.empty()) {
        s += "\n" + tr("Symptom comparison:", "Comparaison des symptomes :");
        for (std::size_t i = 0; i < cmp.symptoms.size(); ++i) {
            const SymptomComparison& sc = cmp.symptoms[i];

            std::string name = humanizeSymptomId(sc.symptomId);   // snake_case -> Title Case
            s += "\n * " + name + ": ";

            if (!sc.beforePresent && sc.afterPresent)      s += tr("new in after group",  "nouveau dans le groupe apres");
            else if (sc.beforePresent && !sc.afterPresent) s += tr("gone from after group","disparu du groupe apres");
            else                                           s += tr("present on both sides","present des deux cotes");

            char sev[128];
            std::snprintf(sev, sizeof(sev), " (%s -> %s)", severityWord(sc.beforePresent ? sc.beforeSeverity : AnalysisSeverity::Info).c_str(),
                          severityWord(sc.afterPresent  ? sc.afterSeverity  : AnalysisSeverity::Info).c_str());   // c_str() args: safe
            s += sev;

            s += ": [" + verdictLabel(sc.verdict, lang) + "]";
        }
    } else if (cmp.hasBlockingFactors()) {
        s += "\n" + tr("No per-symptom verdict: conditions changed between groups.",
                       "Aucun verdict par symptome : les conditions ont change entre les groupes.");
    }

    return s;
}

} // namespace diag
