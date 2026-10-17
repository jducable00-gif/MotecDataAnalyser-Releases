#ifndef DIAGNOSTICENGINE_H
#define DIAGNOSTICENGINE_H
// ===============================
// Phase 1 — Diagnostic Engine: Symptom Detectors + Registry Builder
// Each detector maps to one or more RuleConfig symptom catalog entries.
// All user-facing strings use the tr() lambda pattern for EN/FR localization.
// ===============================

#include "MotecSetupAnalyzer.h"
#include "RuleConfig.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace diag {

// --- Utility: find a RuleSymptom by id in the catalog -----------------------
inline const RuleSymptom* findSymptom(const std::string& symptomId) {
    for (const auto& rule : RuleConfig::instance().rules()) {
        if (rule.id == symptomId) return &rule;
    }
    return nullptr;
}

// --- Utility: build ranked causes from catalog with basic modulation --------
/// Takes the base cause list from RuleConfig and applies simple evidence-based
/// modulation. For Phase 1, concordance is determined by whether supporting
/// aliases are present in the provider (boost) or absent/contradictory (dampen).
inline std::vector<CauseEntry> rankCauses(const std::string& symptomId,
                                          const DataProvider& provider,
                                          double confidence) {
    const RuleSymptom* sym = findSymptom(symptomId);
    if (!sym || sym->causes.empty()) return {};

    // Start with base weights from catalog (CauseEntry: cause/weight/adjustment/direction)
    std::vector<CauseEntry> ranked;
    for (const auto& c : sym->causes) {
        CauseEntry entry = c;  // copy id, weight, adjustment, direction

        // Basic Phase-1 modulation: high confidence → slight boost on top causes,
        // low confidence → dampen all. Full validation-channel concordance logic
        // will be refined in later phases as evidence channels are mapped.
        double w = entry.weight;
        if (confidence >= g_thresholds.confidenceHigh) {
            w *= 1.25;   // strong evidence: boost confidence in top causes
        } else if (confidence < g_thresholds.confidenceLow) {
            w *= 0.75;   // weak evidence: dampen all
        }

        entry.weight = std::min(1.0, w);
        ranked.push_back(entry);
    }

    // Sort by adjusted weight descending — always output full list
    std::sort(ranked.begin(), ranked.end(), [](const CauseEntry& a, const CauseEntry& b) {
        return a.weight > b.weight;
    });

    return ranked;
}

// --- Utility: format a percentage for evidence strings ----------------------
// Callers pass values already in [0..100] range; just rounds to whole percent.
inline double pct(double value) { return std::round(value); }


// ============================================================================
// Detector #1 — Rear Traction Loss on Exit (symptom id: rear_traction_loss_on_exit)
// Primary evidence: per-corner rear wheel slip exceeding threshold during exit phase
// ============================================================================
class RearTractionLossOnExitRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — without these we cannot detect the symptom at all
            { AnalysisAlias::WheelSlipLR, AnalysisAlias::WheelSlipRR },
            // Optional — improve confidence / provide context
            { AnalysisAlias::ThrottleAvgPct, AnalysisAlias::YawRateAvg, AnalysisAlias::LatGAvg,
              AnalysisAlias::TyreTempRLInner, AnalysisAlias::TyreTempRRInner, AnalysisAlias::TyrePressRL, AnalysisAlias::TyrePressRR }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double slipLR = 0.0;
        provider.getValue(AnalysisAlias::WheelSlipLR, slipLR);
        double slipRR = 0.0;
        provider.getValue(AnalysisAlias::WheelSlipRR, slipRR);
        double maxRearSlip = std::max(slipLR, slipRR);
        double threshold   = settings.handling.wheelSlipMax;  // e.g. 15%

        if (threshold <= 0 || maxRearSlip < threshold) return {};  // no symptom detected

        // --- Confidence components ---
        double proximity    = std::min(1.0, maxRearSlip / threshold - 1.0);
        int sampleCount     = g_thresholds.minHandlingSamples;  // default
        if (provider.hasValue(AnalysisAlias::HandlingSampleCountExit)) {
            double sc = 0.0;
            provider.getValue(AnalysisAlias::HandlingSampleCountExit, sc);
            sampleCount = static_cast<int>(sc);  // Single-lap: lapsReproduced = 1 (multi-lap in Phase 4)
        }
        double coverage     = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = "rear_traction_loss_on_exit";
        f.severity     = maxRearSlip > threshold * 2.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("corner exit", "sortie de virage");

        // --- Evidence strings (localized) ---
        char buf[256];
        snprintf(buf, sizeof(buf), tr("Rear wheel slip %.1f%% (threshold %d%%)",
                                      "Patinage roue arrière %.1f%% (seuil %d%%)").c_str(),
                 maxRearSlip, static_cast<int>(threshold));
        f.evidence.push_back(std::string(buf));

        if (provider.hasValue(AnalysisAlias::ThrottleAvgPct)) {
            double throttle = 0.0;
            provider.getValue(AnalysisAlias::ThrottleAvgPct, throttle);
            snprintf(buf, sizeof(buf), tr("Mean throttle %.0f%%", "Accélération moyenne %.0f%%").c_str(), pct(throttle));
            f.evidence.push_back(std::string(buf));
        }

        if (provider.hasValue(AnalysisAlias::YawRateAvg)) {
            double yaw = 0.0;
            provider.getValue(AnalysisAlias::YawRateAvg, yaw);
            snprintf(buf, sizeof(buf), tr("Yaw rate mean %+.1f °/s", "Taux de lacet moyen %+.1f °/s").c_str(), yaw);
            f.evidence.push_back(std::string(buf));
        }

        // --- Confounders: TCS active reduces confidence in mechanical cause ---
        if (provider.hasValue(AnalysisAlias::TcsActivePct)) {
            double tcs = 0.0;
            provider.getValue(AnalysisAlias::TcsActivePct, tcs);
            if (tcs > 10.0) {
                snprintf(buf, sizeof(buf), tr("TCS active on %.0f%% of samples", "TCS actif sur %.0f%% des échantillons").c_str(), pct(tcs));
                f.confounders.push_back(std::string(buf));
            }
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #2 — Front Understeer (symptoms: front_understeer_entry + front_understeer_exit)
// Primary evidence: understeer indicator counts / steer-yaw correlation deficit
// ============================================================================
class FrontUndersteerRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — need at least the handling phase stats + lateral G
            { AnalysisAlias::UndersteerIndicatorsEntryPct, AnalysisAlias::LatGAvg },
            // Optional — enrich evidence and confidence
            { AnalysisAlias::SteerYawCorrelation, AnalysisAlias::WheelSlipLF, AnalysisAlias::WheelSlipRF,
              AnalysisAlias::TyreTempFLInner, AnalysisAlias::TyreTempFRInner, AnalysisAlias::TyrePressFL, AnalysisAlias::TyrePressFR }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double understeerPctEntry = 0.0;
        provider.getValue(AnalysisAlias::UndersteerIndicatorsEntryPct, understeerPctEntry);
        double latG = 0.0;
        provider.getValue(AnalysisAlias::LatGAvg, latG);

        // Detection: significant fraction of entry-phase samples show understeer indicators,
        // AND there is meaningful lateral load (not straight-line noise)
        if (understeerPctEntry < 30.0 || std::abs(latG) < 0.2) return {};  // no symptom

        // Phase 1: only entry-phase indicator aliases exist (no UndersteerIndicatorsExitPct),
        // so detection is entry-dominant. Exit variant arrives with cross-phase segmentation.
        const char* symptomId = "front_understeer_entry";

        // --- Confidence components ---
        double proximity  = std::min(1.0, (understeerPctEntry - 30.0) / 70.0);  // 30%→0, 100%→1
        double scVal2 = 0.0;  // entry-phase handling sample count (optional channel)
        if (!provider.hasValue(AnalysisAlias::HandlingSampleCountEntry) || !provider.getValue(AnalysisAlias::HandlingSampleCountEntry, scVal2))
            return {};  // required for confidence — abstain
        int sampleCount   = static_cast<int>(scVal2);
        double coverage   = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = symptomId;
        f.severity     = understeerPctEntry > 70.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("corner entry", "entrée de virage");

        // --- Evidence strings (localized) ---
        char buf[256];
        const std::string phaseName = tr("entry", "entrée");  // local: bcc32c cannot pass non-trivial types through variadic args
        snprintf(buf, sizeof(buf), tr("Understeer indicators on %.0f%% of %s samples",
                                      "Indicateurs sous-virage sur %.0f%% des échantillons %s").c_str(),
                 pct(understeerPctEntry), phaseName.c_str());
        f.evidence.push_back(std::string(buf));

        snprintf(buf, sizeof(buf), tr("Lateral G mean %.2f g", "Accélération latérale moyenne %.2f g").c_str(), std::abs(latG) / 9.81);
        f.evidence.push_back(std::string(buf));

        if (provider.hasValue(AnalysisAlias::SteerYawCorrelation)) {
            double corr = 0.0;
            provider.getValue(AnalysisAlias::SteerYawCorrelation, corr);
            snprintf(buf, sizeof(buf), tr("Steer/yaw correlation %.2f", "Corrélation direction/lacet %.2f").c_str(), corr);
            f.evidence.push_back(std::string(buf));
        }

        // Front tyre temperature evidence (inner > outer suggests camber issue)
        if (provider.hasValue(AnalysisAlias::TyreTempFLInner) && provider.hasValue(AnalysisAlias::TyreTempFLOuter)) {
            double inner = 0.0;
            provider.getValue(AnalysisAlias::TyreTempFLInner, inner);
            double outer = 0.0;
            provider.getValue(AnalysisAlias::TyreTempFLOuter, outer);
            if (outer - inner < 0.5) {  // flat or inverted profile
                snprintf(buf, sizeof(buf), tr("Front tyre temp spread %.1f°C (flat/inverted)", "Écart température pneu avant %.1f°C (plat/inversé)").c_str(), outer - inner);
                f.evidence.push_back(std::string(buf));
            }
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #3 — Rear Oversteer (symptoms: rear_oversteer_entry + exit variant)
// Primary evidence: oversteer indicator counts with low steering input / yaw spikes
// ============================================================================
class RearOversteerRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — need the oversteer indicators + lateral G context
            { AnalysisAlias::OversteerIndicatorsExitPct, AnalysisAlias::LatGAvg },
            // Optional
            { AnalysisAlias::YawRateAvg, AnalysisAlias::SteerYawCorrelation, AnalysisAlias::WheelSlipLR, AnalysisAlias::WheelSlipRR }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double oversteerPctExit = 0.0;
        provider.getValue(AnalysisAlias::OversteerIndicatorsExitPct, oversteerPctExit);
        double latG             = 0.0;  // LatGAvg is required — checkChannelRequirements() already verified it

        // Detection: significant fraction of exit-phase samples show oversteer indicators
        if (oversteerPctExit < 25.0 || std::abs(latG) < 0.15) return {};  // no symptom

        const char* symptomId = "rear_oversteer_entry";
        // Phase-1 note: no OversteerIndicatorsEntryPct alias exists, so the detector is
        // exit-phase only; cross-phase comparison arrives with segmentation (Phase 2).

        // --- Confidence components ---
        double proximity  = std::min(1.0, (oversteerPctExit - 25.0) / 75.0);
        double scVal3 = 0.0;  // exit-phase handling sample count (optional channel)
        if (!provider.hasValue(AnalysisAlias::HandlingSampleCountExit) || !provider.getValue(AnalysisAlias::HandlingSampleCountExit, scVal3))
            return {};  // required for confidence — abstain
        int sampleCount   = static_cast<int>(scVal3);
        double coverage   = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = symptomId;
        f.severity     = oversteerPctExit > 60.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("corner exit", "sortie de virage");

        // --- Evidence strings (localized) ---
        char buf[256];
        snprintf(buf, sizeof(buf), tr("Oversteer indicators on %.0f%% of exit samples",
                                      "Indicateurs survirage sur %.0f%% des échantillons sortie").c_str(),
                 pct(oversteerPctExit));
        f.evidence.push_back(std::string(buf));

        if (provider.hasValue(AnalysisAlias::YawRateAvg)) {
            double yaw = 0.0;
            provider.getValue(AnalysisAlias::YawRateAvg, yaw);
            snprintf(buf, sizeof(buf), tr("Yaw rate mean %+.1f °/s", "Taux de lacet moyen %+.1f °/s").c_str(), yaw);
            f.evidence.push_back(std::string(buf));
        }

        // --- Confounders: ABS active during braking can mimic oversteer onset ---
        if (provider.hasValue(AnalysisAlias::AbsActivePct)) {
            double abs = 0.0;
            provider.getValue(AnalysisAlias::AbsActivePct, abs);
            if (abs > 15.0) {
                snprintf(buf, sizeof(buf), tr("ABS active on %.0f%% of braking samples", "ABS actif sur %.0f%% des échantillons de freinage").c_str(), pct(abs));
                f.confounders.push_back(std::string(buf));
            }
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #4 — Brake Instability / Lockup (symptom id: brake_instability)
// Primary evidence: per-corner wheel-speed divergence during braking.
// AMS2 note: wheel speeds are ground-speed proxies → declared optional, not required.
// ============================================================================
class BrakeLockupInstabilityRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — need brake torque data to confirm braking is happening
            { AnalysisAlias::BrakeTorqueFrontAvg },
            // Optional — axle wheel speeds (per-corner not available in Phase 0);
            // AMS2: ground-speed proxies. ABS/TCS as confounders.
            { AnalysisAlias::WheelSpeedFrontAvg, AnalysisAlias::WheelSpeedRearAvg,
              AnalysisAlias::AbsActivePct, AnalysisAlias::TcsActivePct }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double brakeTorqueFront = 0.0;
        provider.getValue(AnalysisAlias::BrakeTorqueFrontAvg, brakeTorqueFront);  // required channel
        if (brakeTorqueFront < 1.0) return {};  // not braking — no symptom to detect

        // Phase-1 limitation: per-corner wheel speeds are NOT exposed via DataProvider.
        // We use axle-level front/rear speed comparison as a proxy for instability:
        // if the rear axle is significantly slower than the front during heavy braking,
        // it suggests rear lockup tendency (or vice versa).
        bool haveFrontSpeed = provider.hasValue(AnalysisAlias::WheelSpeedFrontAvg);
        bool haveRearSpeed  = provider.hasValue(AnalysisAlias::WheelSpeedRearAvg);

        double instabilityScore = 0.0;
        std::string biasNote;

        if (haveFrontSpeed && haveRearSpeed) {
            double wsF = 0.0;
            provider.getValue(AnalysisAlias::WheelSpeedFrontAvg, wsF);
            double wsR = 0.0;
            provider.getValue(AnalysisAlias::WheelSpeedRearAvg, wsR);
            // Axle speed differential as % of front axle speed
            instabilityScore = std::abs(wsF - wsR) / (wsF > 0.1 ? wsF : 1.0) * 100.0;

            if (instabilityScore > 5.0) {
                biasNote = (wsR < wsF)
                    ? tr("Rear axle slower — possible rear lockup tendency", "Essieu arrière plus lent — tendance au blocage arrière")
                    : tr("Front axle slower — possible front lockup tendency", "Essieu avant plus lent — tendance au blocage avant");
            }
        } else {
            // No wheel speed data at all: use brake torque imbalance as weak proxy.
            // Imbalance = |F-R| / mean(F,R) * 100, same shape as the axle-speed formula above.
            if (provider.hasValue(AnalysisAlias::BrakeTorqueRearAvg)) {
                double torqueF = 0.0;
                provider.getValue(AnalysisAlias::BrakeTorqueFrontAvg, torqueF);
                double torqueR = 0.0;
                provider.getValue(AnalysisAlias::BrakeTorqueRearAvg, torqueR);
                instabilityScore = std::abs(torqueF - torqueR) / ((torqueF + torqueR) > 1.0 ? (torqueF + torqueR) * 0.5 : 1.0) * 100.0;
            } else {
                return {};  // cannot assess without any speed/torque data — abstain
            }
        }

        // Detection: >3% instability score during braking
        const double lockupThresholdPct = 3.0;
        if (instabilityScore < lockupThresholdPct) return {};  // no instability detected

        // --- Confidence components ---
        double proximity  = std::min(1.0, (instabilityScore - lockupThresholdPct) / lockupThresholdPct);  // 3%→0, 6%+→1
        int sampleCount   = g_thresholds.minHandlingSamples;  // brake samples not separately tracked in Phase 1
        double coverage   = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = "brake_instability";
        f.severity     = instabilityScore > lockupThresholdPct * 3.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("braking phase", "phase de freinage");

        // --- Evidence strings (localized) ---
        char buf[256];
        snprintf(buf, sizeof(buf), tr("Axle instability score %.1f%% during braking",
                                      "Score instabilité essieux %.1f%% en freinage").c_str(), instabilityScore);
        f.evidence.push_back(std::string(buf));

        if (!biasNote.empty()) {
            f.evidence.push_back(biasNote);
        }

        // --- Confounders: ABS/TCS active means the car is at its limit — instability may be expected ---
        if (provider.hasValue(AnalysisAlias::AbsActivePct)) {
            double abs = 0.0;
            provider.getValue(AnalysisAlias::AbsActivePct, abs);
            if (abs > 10.0) {
                snprintf(buf, sizeof(buf), tr("ABS active on %.0f%% of braking samples", "ABS actif sur %.0f%% des échantillons de freinage").c_str(), pct(abs));
                f.confounders.push_back(std::string(buf));
            }
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #5 — Bumpstop Engagement (symptom id: bumpstop_engagement)
// Primary evidence: bumpstop engagement percentage exceeding threshold.
// PMR note: no ride height / Rake → reduced confidence with traceability note.
// ============================================================================
class BumpstopEngagementRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — need at least one corner's bumpstop data
            { AnalysisAlias::BumpStopEngagementPctLF },   // if LF is missing, the rule abstains (conservative)
            // Optional — other corners + ride height context
            { AnalysisAlias::BumpStopEngagementPctRF, AnalysisAlias::BumpStopEngagementPctLR, AnalysisAlias::BumpStopEngagementPctRR, AnalysisAlias::Rake }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double bsLF = 0.0;
        provider.getValue(AnalysisAlias::BumpStopEngagementPctLF, bsLF);
        // Phase-1 constant: no bumpstop-max threshold in Thresholds yet (only bumpStopMin).
        const double bumpstopThresholdPct = 5.0;

        if (bsLF < bumpstopThresholdPct) return {};  // no excessive engagement on LF

        // Check all available corners for the worst offender
        double maxBump = bsLF;
        const char* cornerLabel = "FL";
        if (provider.hasValue(AnalysisAlias::BumpStopEngagementPctRF)) {
            double v = 0.0;
            provider.getValue(AnalysisAlias::BumpStopEngagementPctRF, v);
            if (v > maxBump) { maxBump = v; cornerLabel = "FR"; }
        }
        if (provider.hasValue(AnalysisAlias::BumpStopEngagementPctLR)) {
            double v = 0.0;
            provider.getValue(AnalysisAlias::BumpStopEngagementPctLR, v);
            if (v > maxBump) { maxBump = v; cornerLabel = "RL"; }
        }
        if (provider.hasValue(AnalysisAlias::BumpStopEngagementPctRR)) {
            double v = 0.0;
            provider.getValue(AnalysisAlias::BumpStopEngagementPctRR, v);
            if (v > maxBump) { maxBump = v; cornerLabel = "RR"; }
        }

        // --- Confidence components ---
        double proximity  = std::min(1.0, (maxBump - bumpstopThresholdPct) / bumpstopThresholdPct);
        int sampleCount   = g_thresholds.minHandlingSamples;
        double coverage   = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = "bumpstop_engagement";
        f.severity     = maxBump > bumpstopThresholdPct * 3.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("full lap", "tour complet");

        // --- Evidence strings (localized) ---
        char buf[256];
        snprintf(buf, sizeof(buf), tr("%s bumpstop engagement %.1f%% (threshold %.0f%%)",
                                      "%s appui-bout %.1f%% (seuil %.0f%%)").c_str(),
                 cornerLabel, maxBump, bumpstopThresholdPct);
        f.evidence.push_back(std::string(buf));

        // Ride height / Rake context if available
        bool haveRake = provider.hasValue(AnalysisAlias::Rake);
        if (haveRake) {
            double rake = 0.0;
            provider.getValue(AnalysisAlias::Rake, rake);
            snprintf(buf, sizeof(buf), tr("Rake %.2f mm", "Rake %.2f mm").c_str(), rake);
            f.evidence.push_back(std::string(buf));
        } else {
            // PMR: no ride height → note reduced confidence source
            f.confounders.push_back(tr("No ride-height data available (reduced confidence)",
                                       "Pas de données hauteur de caisse (confiance réduite)"));
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #6 — Damper Asymmetry (symptoms: damper_asymmetry_bump + damper_asymmetry_rebound)
// Consumes pre-computed DataProvider values from Phase 0 (DamperAsymmetryFront/Rear).
// Does NOT recode the existing detail::evaluateDamperCorner() logic.
// ============================================================================
class DamperAsymmetryRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — need at least one axle's asymmetry metric
            { AnalysisAlias::DamperAsymmetryFront },
            // Optional — rear adds context
            { AnalysisAlias::DamperAsymmetryRear }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double asymFront = 0.0;
        provider.getValue(AnalysisAlias::DamperAsymmetryFront, asymFront);  // required channel
        const double asymThresholdPts = g_thresholds.damperAsymmetryThreshold > 0 ? g_thresholds.damperAsymmetryThreshold : 8.0;

        if (std::abs(asymFront) < asymThresholdPts) return {};  // no significant asymmetry on front axle

        const char* symptomId = "damper_asymmetry_bump";
        if (asymFront < 0.0) {
            symptomId = "damper_asymmetry_rebound";             // negative → rebound-dominant bias
        }

        // --- Confidence components ---
        double proximity  = std::min(1.0, (std::abs(asymFront) - asymThresholdPts) / asymThresholdPts);
        int sampleCount   = g_thresholds.minHandlingSamples;
        double coverage   = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = symptomId;
        f.severity     = std::abs(asymFront) > asymThresholdPts * 2.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("full lap", "tour complet");

        // --- Evidence strings (localized) ---
        char buf[256];
        snprintf(buf, sizeof(buf), tr("Front damper asymmetry %+.1f pts (threshold %.0f)",
                                      "Asymétrie amortisseur avant %+.1f pts (seuil %.0f)").c_str(),
                 asymFront, asymThresholdPts);
        f.evidence.push_back(std::string(buf));

        if (provider.hasValue(AnalysisAlias::DamperAsymmetryRear)) {
            double asymRear = 0.0;
            provider.getValue(AnalysisAlias::DamperAsymmetryRear, asymRear);
            snprintf(buf, sizeof(buf), tr("Rear damper asymmetry %.1f%%", "Asymétrie amortisseur arrière %.1f%%").c_str(), asymRear);
            f.evidence.push_back(std::string(buf));
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #7 — Damper High-Speed Share (symptoms: damper_high_speed_share + damper_low_speed_stiff)
// Consumes pre-computed DataProvider values from Phase 0.
// ============================================================================
class DamperHighSpeedShareRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — need at least one axle's high-speed share metric
            { AnalysisAlias::DamperHighSpeedShareFront },
            // Optional — rear adds context
            { AnalysisAlias::DamperHighSpeedShareRear }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        ChannelRequirements req = channelRequirements();
        std::vector<AnalysisAlias> missingOptional;
        if (!checkChannelRequirements(req, provider, missingOptional)) return {};  // abstain

        double hssFront = 0.0;
        provider.getValue(AnalysisAlias::DamperHighSpeedShareFront, hssFront);  // required channel
        const double hssThresholdPct = g_thresholds.damperHighSpeedShareMax > 0 ? g_thresholds.damperHighSpeedShareMax : 25.0;

        if (hssFront < hssThresholdPct) return {};  // no excessive high-speed share on front axle

        bool isRearDominant = false;
        if (provider.hasValue(AnalysisAlias::DamperHighSpeedShareRear)) {
            double _hss = 0.0;
            provider.getValue(AnalysisAlias::DamperHighSpeedShareRear, _hss);
            isRearDominant = (_hss > hssFront * 1.3);
        }

        const char* symptomId = "damper_high_speed_share";
        if (isRearDominant) {
            // Rear-dominant high-speed share can also indicate low-speed stiffness issue
            symptomId = "damper_low_speed_stiff";
        }

        // --- Confidence components ---
        double proximity  = std::min(1.0, (hssFront - hssThresholdPct) / hssThresholdPct);
        int sampleCount   = g_thresholds.minHandlingSamples;
        double coverage   = computeChannelCoverage(req, provider);

        DiagnosticFinding f;
        f.symptomId    = symptomId;
        f.severity     = hssFront > hssThresholdPct * 2.0 ? AnalysisSeverity::Critical : AnalysisSeverity::Warning;
        f.confidence   = computeDiagnosticConfidence(proximity, sampleCount, 1, coverage);
        f.dataQualityLabel = diagnosticConfidenceLabel(f.confidence);
        f.phaseWindow  = tr("full lap", "tour complet");

        // --- Evidence strings (localized) ---
        char buf[256];
        snprintf(buf, sizeof(buf), tr("Front high-speed damper share %.1f%% (max %.0f%%)",
                                      "Partie haute vitesse amortisseur avant %.1f%% (maxi %.0f%%)").c_str(),
                 hssFront, hssThresholdPct);
        f.evidence.push_back(std::string(buf));

        if (provider.hasValue(AnalysisAlias::DamperHighSpeedShareRear)) {
            double hssRear = 0.0;
            provider.getValue(AnalysisAlias::DamperHighSpeedShareRear, hssRear);
            snprintf(buf, sizeof(buf), tr("Rear high-speed damper share %.1f%%", "Partie haute vitesse amortisseur arrière %.1f%%").c_str(), hssRear);
            f.evidence.push_back(std::string(buf));
        }

        // --- Ranked causes from catalog ---
        f.rankedCauses = rankCauses(f.symptomId, provider, f.confidence);
        f.missingChannels = missingOptional;

        return f;
    }
};


// ============================================================================
// Detector #8 — Thermal Degradation (STUB for Phase 1)
// Symptom id: thermal_degradation_across_laps
// Full implementation requires multi-lap comparison → deferred to Phase 4.
// Returns empty finding always in single-lap context.
// ============================================================================
class ThermalDegradationRule : public DiagnosticRule {
public:
    ChannelRequirements channelRequirements() const override {
        return {
            // Required — tyre temps (needed for any thermal analysis)
            { AnalysisAlias::TyreTempFLInner, AnalysisAlias::TyreTempFLOuter },
            // Optional
            { AnalysisAlias::WaterTemp, AnalysisAlias::OilTemp }
        };
    }

    DiagnosticFinding evaluate(const DataProvider& provider, const AnalysisSettings& settings) const override {
        // Phase 1 stub: single-lap evaluation cannot detect cross-lap degradation.
        // Full implementation in Phase 4 with multi-lap delta analysis.
        return {};  // always abstain for now — no silent gap, just not yet implemented
    }
};


// ============================================================================
// Registry Builder — registers all Phase-1 diagnostic detectors
// ============================================================================
inline DiagnosticRegistry buildDefaultDiagnosticRegistry() {
    DiagnosticRegistry registry;

    registry.add(std::unique_ptr<DiagnosticRule>(new RearTractionLossOnExitRule()));
    registry.add(std::unique_ptr<DiagnosticRule>(new FrontUndersteerRule()));
    registry.add(std::unique_ptr<DiagnosticRule>(new RearOversteerRule()));
    registry.add(std::unique_ptr<DiagnosticRule>(new BrakeLockupInstabilityRule()));
    registry.add(std::unique_ptr<DiagnosticRule>(new BumpstopEngagementRule()));
    registry.add(std::unique_ptr<DiagnosticRule>(new DamperAsymmetryRule()));
    registry.add(std::unique_ptr<DiagnosticRule>(new DamperHighSpeedShareRule()));
    // ThermalDegradationRule is registered but always returns empty (Phase 4 stub)
    registry.add(std::unique_ptr<DiagnosticRule>(new ThermalDegradationRule()));

    return registry;
}

// --- Convenience: evaluate all diagnostic rules against a LapStats object ----
inline std::vector<DiagnosticFinding> evaluateLapWithDiagnostics(const LapStats& lap,
                                                                  const AnalysisSettings& settings = AnalysisSettings()) {
    DataProvider provider = buildDataProviderFromLap(lap);
    DiagnosticRegistry registry = buildDefaultDiagnosticRegistry();
    return registry.evaluateAll(provider, settings);
}

} // namespace diag

#endif // DIAGNOSTICENGINE_H
