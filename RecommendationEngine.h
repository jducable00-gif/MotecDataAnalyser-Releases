// ===============================
// RecommendationEngine (Phase 2 of the refactoring plan)
// Sole owner of confidence scoring, data-quality labels and evidence strings.
// Maps a structured finding + symptom key to a fully populated Recommendation
// via RuleConfig (cause -> adjustment matrix). Rendering is NOT done here —
// that lives in LapReportGenerator / formatRecommendationBlock.
// Ported verbatim from analyze_motec_setup.cpp; output stays byte-compatible.
// ===============================

#ifndef RECOMMENDATIONENGINE_H
#define RECOMMENDATIONENGINE_H

#include <string>
#include "MotecSetupAnalyzer.h" // declares formatDouble / formatTime
#include "RuleConfig.h"


// ---------- Confidence / data quality ----------
double computeConfidence(double value, double threshold, int sampleCount,
                         int lapsReproduced, int channelsAvailable, int channelsNeeded);
std::string dataQualityLabel(double confidence, ReportLanguage language);

// ---------- Structured recommendation builders ----------
// One corner's damper-velocity histogram -> Recommendation (symptom:
// damper_asymmetry_bump / damper_asymmetry_rebound / damper_high_speed_share /
// damper_low_speed_stiff). Returns an empty, non-issue rec when samples are low.
Recommendation buildDamperRecommendation(const DamperCornerStats& stats,
                                         const std::string& cornerLabelEn,
                                         const std::string& cornerLabelFr,
                                         ReportLanguage language);

// Corner entry/exit handling evidence -> Recommendation (symptom:
// front_understeer_entry / front_understeer_exit / rear_oversteer_entry /
// rear_traction_loss_on_exit). Returns an empty, non-issue rec when samples are low.
Recommendation buildHandlingRecommendation(const HandlingPhaseStats& stats,
                                           bool entryPhase,
                                           ReportLanguage language);

// True when a corner's damper-velocity distribution indicates an issue
// (asymmetry, excessive high-speed share, or a suspension that barely moves).
bool hasDamperIssue(const DamperCornerStats& stats);

#endif // RECOMMENDATIONENGINE_H
