// ===============================
// RecommendationEngine (Phase 2 of the refactoring plan)
// Sole owner of computeConfidence / dataQualityLabel and evidence strings.
// Ported verbatim from analyze_motec_setup.cpp — output stays byte-compatible.
// ===============================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include "RecommendationEngine.h"

// ---------- Shared formatting helpers (moved out of the parser file) ----------

std::string formatDouble(double value, int precision) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return std::string(buf);
}

std::string formatTime(double seconds) {
    int totalSeconds = (int)seconds;
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;
    int millisecs = (int)((seconds - totalSeconds) * 1000);

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << minutes << ":"
       << std::setfill('0') << std::setw(2) << secs << "."
       << std::setfill('0') << std::setw(3) << millisecs;
    return ss.str();
}

// ===============================
// Confidence scoring
// ===============================
double computeConfidence(double value, double threshold, int sampleCount,
                         int lapsReproduced, int channelsAvailable, int channelsNeeded) {
    if (channelsNeeded <= 0) channelsNeeded = 1;
    if (channelsAvailable <= 0) return 0.0;

    double ratio = (threshold > 0.0) ? std::abs(value) / threshold : 1.0;
    double proximity = std::min(1.0, (ratio - 1.0) / 1.0);
    if (proximity < 0.0) proximity = 0.0;
    double scoreProximity = proximity * 0.40;

    double scoreSamples = std::min(1.0, static_cast<double>(sampleCount) / 100.0) * 0.25;

    int minLaps = g_thresholds.minLapsForReproducibility;
    double scoreRepro = std::min(1.0, static_cast<double>(lapsReproduced) / static_cast<double>(minLaps)) * 0.20;

    double scoreChannels = (static_cast<double>(channelsAvailable) / static_cast<double>(channelsNeeded)) * 0.15;

    double total = scoreProximity + scoreSamples + scoreRepro + scoreChannels;
    if (total < 0.0) total = 0.0;
    if (total > 1.0) total = 1.0;
    return total;
}

std::string dataQualityLabel(double confidence, ReportLanguage language) {
    auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
        return language == ReportLanguage::French ? fr : en;
    };
    if (confidence >= g_thresholds.confidenceHigh)
        return tr("HIGH", "ÉLEVÉE");
    if (confidence >= g_thresholds.confidenceMedium)
        return tr("MEDIUM", "MOYENNE");
    if (confidence >= g_thresholds.confidenceLow)
        return tr("LOW", "FAIBLE");
    return tr("INSUFFICIENT", "INSUFFISANTE");
}

// ===============================
// Symptom rules: cause -> adjustment matrix.
// Les règles vivent dans RuleConfig (valeurs par défaut intégrées, fusion optionnelle
// depuis MotecDataAnalyser.rules.json — DEVELOPMENT_PLAN.md étape 1).
// ===============================
static const RuleSymptom* findSymptomRule(const std::string& symptom) {
    return RuleConfig::instance().findRule(symptom);
}

static void applyRuleToRecommendation(Recommendation& rec, const std::string& symptomKey,
                                      const char* fallbackSymptom = NULL) {
    const RuleSymptom* rule = findSymptomRule(symptomKey);
    if (!rule && fallbackSymptom)
        rule = findSymptomRule(fallbackSymptom);
    if (rule) {
        for (std::size_t i = 0; i < rule->causes.size(); ++i)
            rec.possibleCauses.push_back(rule->causes[i]);
        for (std::size_t i = 0; i < rule->validationChannels.size(); ++i)
            rec.validationChannels.push_back(rule->validationChannels[i]);
    }
}

// ===============================
// Damper-velocity recommendation (structured)
// ===============================

struct DamperFinding {
    bool issue = false;
    std::string text;
};

// Evaluates one corner's damper-velocity histogram against an ideal symmetric bell shape
// (low/high-speed bump vs rebound split at ~50 mm/s, the common sim-racing damper cutoff).
static DamperFinding evaluateDamperCorner(const DamperCornerStats& stats,
                                         const std::string& cornerLabelEn,
                                         const std::string& cornerLabelFr,
                                         ReportLanguage language) {
    auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
        return language == ReportLanguage::French ? fr : en;
    };

    DamperFinding finding;
    const VelocityHistogram& h = stats.histogram;
    if (h.totalCount < g_thresholds.minDamperSamples)
        return finding;

    const double lowSpeedThreshold = g_thresholds.damperLowSpeedThreshold; // mm/s (~2 in/s)
    double lowBump    = h.percentInRange(0.0, lowSpeedThreshold);
    double highBump   = h.percentInRange(lowSpeedThreshold, VelocityHistogram::rangeMax() + 1.0);
    double lowRebound  = h.percentInRange(-lowSpeedThreshold, 0.0);
    double highRebound = h.percentInRange(-VelocityHistogram::rangeMax() - 1.0, -lowSpeedThreshold);

    double asymmetry      = (lowBump + highBump) - (lowRebound + highRebound);
    double highSpeedShare = highBump + highRebound;
    double lowSpeedShare  = lowBump + lowRebound;

    std::string label = tr(cornerLabelEn, cornerLabelFr);

    if (std::abs(asymmetry) > g_thresholds.damperAsymmetryThreshold) {
        finding.issue = true;
        if (asymmetry > 0.0) {
            finding.text = label + tr(
                ": spends more time compressing than rebounding; increase rebound damping (slow and fast) slightly, or soften bump damping, so the shock returns faster.",
                " : passe plus de temps en compression qu'en detente ; augmentez legerement l'amortissement en detente (lent et rapide), ou assouplissez la compression, pour que l'amortisseur revienne plus vite.");
        } else {
            finding.text = label + tr(
                ": spends more time extending than compressing; soften rebound damping slightly, or increase bump damping, to rebalance the curve.",
                " : passe plus de temps en detente qu'en compression ; assouplissez legerement l'amortissement en detente, ou augmentez la compression, pour reequilibrer la courbe.");
        }
        return finding;
    }

    if (highSpeedShare > g_thresholds.damperHighSpeedShareMax) {
        finding.issue = true;
        finding.text = label + tr(
            ": a large share of movement happens at high shaft speed; increase high-speed bump/rebound damping to round off the velocity curve toward a bell shape.",
            " : une grande part du mouvement se produit a haute vitesse de tige ; augmentez l'amortissement haute vitesse (compression/detente) pour arrondir la courbe vers une forme de cloche.");
        return finding;
    }

    if (lowSpeedShare > g_thresholds.damperLowSpeedShareMin && h.stdDev() < g_thresholds.damperLowSpeedStdDevMax) {
        finding.issue = true;
        const std::string pct = formatDouble(lowSpeedShare, 0);
        finding.text = label + tr(
            (": the suspension barely moves (" + pct + "% of the time below 50 mm/s); low-speed damping is likely too stiff, holding the wheel down and preventing it from absorbing bumps. Soften low-speed bump/rebound slightly so the suspension can work.")
            , " : la suspension bouge tres peu (" + pct + "% du temps a moins de 50 mm/s) ; l'amortissement basse vitesse est probablement trop raide et empeche la roue d'absorber les irregularites. Assouplissez legerement la compression/detente basse vitesse pour laisser la suspension travailler.");
        return finding;
    }

    finding.issue = false;
    finding.text = label + tr(
        ": velocity distribution looks close to a balanced bell shape; no damper change indicated.",
        " : la distribution de vitesse est proche d'une forme de cloche equilibree ; aucun changement d'amortisseur necessaire.");
    return finding;
}

bool hasDamperIssue(const DamperCornerStats& stats) {
    DamperFinding finding = evaluateDamperCorner(stats, "LF", "AV-G", ReportLanguage::English);
    return finding.issue;
}

// Builds a structured Recommendation from damper-velocity analysis of one corner.
Recommendation buildDamperRecommendation(const DamperCornerStats& stats,
                                         const std::string& cornerLabelEn,
                                         const std::string& cornerLabelFr,
                                         ReportLanguage language) {
    auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
        return language == ReportLanguage::French ? fr : en;
    };

    Recommendation rec;
    const VelocityHistogram& h = stats.histogram;
    if (h.totalCount < g_thresholds.minDamperSamples)
        return rec;

    const double lowSpeedThreshold = g_thresholds.damperLowSpeedThreshold;
    double lowBump    = h.percentInRange(0.0, lowSpeedThreshold);
    double highBump   = h.percentInRange(lowSpeedThreshold, VelocityHistogram::rangeMax() + 1.0);
    double lowRebound  = h.percentInRange(-lowSpeedThreshold, 0.0);
    double highRebound = h.percentInRange(-VelocityHistogram::rangeMax() - 1.0, -lowSpeedThreshold);

    double asymmetry      = (lowBump + highBump) - (lowRebound + highRebound);
    double highSpeedShare = highBump + highRebound;
    double lowSpeedShare  = lowBump + lowRebound;

    std::string label = tr(cornerLabelEn, cornerLabelFr);
    int sampleCount = static_cast<int>(h.totalCount);

    if (std::abs(asymmetry) > g_thresholds.damperAsymmetryThreshold) {
        rec.issue = true;
        rec.confidence = computeConfidence(std::abs(asymmetry), g_thresholds.damperAsymmetryThreshold,
                                           sampleCount, 1, 1, 1);
        rec.dataQuality = dataQualityLabel(rec.confidence, language);
        rec.evidence.push_back(tr("asymmetry: ", "asymétrie : ") + formatDouble(asymmetry, 1) +
                               tr("% (threshold: ", "% (seuil : ") +
                               formatDouble(g_thresholds.damperAsymmetryThreshold, 1) + "%)");
        rec.evidence.push_back(tr("bump share: ", "part compression : ") + formatDouble(lowBump + highBump, 1) + "%");
        rec.evidence.push_back(tr("rebound share: ", "part détente : ") + formatDouble(lowRebound + highRebound, 1) + "%");

        if (asymmetry > 0.0) {
            rec.symptom = "damper_asymmetry_bump";
            rec.text = label + tr(": spends more time compressing than rebounding.",
                                  " : passe plus de temps en compression qu'en détente.");
            rec.direction = tr("increase rebound / soften bump", "augmenter détente / assouplir compression");
            rec.suggestedChange = tr("1 click", "1 clic");
        } else {
            rec.symptom = "damper_asymmetry_rebound";
            rec.text = label + tr(": spends more time extending than compressing.",
                                  " : passe plus de temps en détente qu'en compression.");
            rec.direction = tr("increase bump / soften rebound", "augmenter compression / assouplir détente");
            rec.suggestedChange = tr("1 click", "1 clic");
        }

        applyRuleToRecommendation(rec, rec.symptom);
        return rec;
    }

    if (highSpeedShare > g_thresholds.damperHighSpeedShareMax) {
        rec.issue = true;
        rec.confidence = computeConfidence(highSpeedShare, g_thresholds.damperHighSpeedShareMax,
                                           sampleCount, 1, 1, 1);
        rec.dataQuality = dataQualityLabel(rec.confidence, language);
        rec.evidence.push_back(tr("high-speed share: ", "part haute vitesse : ") + formatDouble(highSpeedShare, 1) +
                               tr("% (threshold: ", "% (seuil : ") +
                               formatDouble(g_thresholds.damperHighSpeedShareMax, 1) + "%)");
        rec.evidence.push_back(tr("std dev: ", "écart-type : ") + formatDouble(h.stdDev(), 1) + " mm/s");

        rec.symptom = "damper_high_speed_share";
        rec.text = label + tr(": a large share of movement happens at high shaft speed.",
                              " : une grande part du mouvement se produit à haute vitesse de tige.");
        rec.direction = tr("stiffer high-speed damping", "amortissement haute vitesse plus raide");
        rec.suggestedChange = tr("1-2 clicks", "1-2 clics");

        applyRuleToRecommendation(rec, rec.symptom);
        return rec;
    }

    if (lowSpeedShare > g_thresholds.damperLowSpeedShareMin && h.stdDev() < g_thresholds.damperLowSpeedStdDevMax) {
        rec.issue = true;
        rec.confidence = computeConfidence(lowSpeedShare, g_thresholds.damperLowSpeedShareMin,
                                           sampleCount, 1, 1, 1);
        rec.dataQuality = dataQualityLabel(rec.confidence, language);
        rec.evidence.push_back(tr("low-speed share: ", "part basse vitesse : ") + formatDouble(lowSpeedShare, 1) +
                               tr("% (threshold: ", "% (seuil : ") +
                               formatDouble(g_thresholds.damperLowSpeedShareMin, 1) + "%)");
        rec.evidence.push_back(tr("std dev: ", "écart-type : ") + formatDouble(h.stdDev(), 1) +
                               tr(" mm/s (max: ", " mm/s (max : ") +
                               formatDouble(g_thresholds.damperLowSpeedStdDevMax, 1) + " mm/s)");

        rec.symptom = "damper_low_speed_stiff";
        rec.text = label + tr(": the suspension barely moves; low-speed damping is likely too stiff.",
                              " : la suspension bouge très peu ; l'amortissement basse vitesse est probablement trop raide.");
        rec.direction = tr("softer low-speed damping", "amortissement basse vitesse plus souple");
        rec.suggestedChange = tr("1 click", "1 clic");

        applyRuleToRecommendation(rec, rec.symptom);
        return rec;
    }

    rec.issue = false;
    rec.text = label + tr(": velocity distribution looks close to a balanced bell shape; no damper change indicated.",
                          " : la distribution de vitesse est proche d'une forme de cloche équilibrée ; aucun changement nécessaire.");
    return rec;
}

// ===============================
// Handling phase recommendation (structured)
// ===============================
Recommendation buildHandlingRecommendation(const HandlingPhaseStats& stats,
                                           bool entryPhase,
                                           ReportLanguage language) {
    auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
        return language == ReportLanguage::French ? fr : en;
    };

    Recommendation rec;
    if (stats.sampleCount < g_thresholds.minHandlingSamples)
        return rec;

    const double understeerRatio = static_cast<double>(stats.understeerIndicators) /
                                   static_cast<double>(stats.sampleCount);
    const double oversteerRatio = static_cast<double>(stats.oversteerIndicators) /
                                  static_cast<double>(stats.sampleCount);
    const std::string phase = entryPhase
        ? tr("corner entry", "entrée de courbe")
        : tr("corner exit", "sortie de courbe");

    int sampleCount = static_cast<int>(stats.sampleCount);
    int channelsAvailable = 4; // steer, yaw, latG, throttle
    int channelsNeeded = 4;

    if (understeerRatio >= g_thresholds.understeerRatioMin && stats.understeerIndicators > stats.oversteerIndicators) {
        rec.issue = true;
        rec.symptom = entryPhase ? "front_understeer_entry" : "front_understeer_exit";
        rec.context = phase;
        rec.confidence = computeConfidence(understeerRatio, g_thresholds.understeerRatioMin,
                                           sampleCount, 0, channelsAvailable, channelsNeeded);
        rec.dataQuality = dataQualityLabel(rec.confidence, language);
        rec.evidence.push_back(tr("understeer ratio: ", "ratio sous-virage: ") +
                               formatDouble(understeerRatio * 100.0, 1) + "%");
        rec.evidence.push_back(tr("indicators: ", "indicateurs: ") +
                               std::to_string(stats.understeerIndicators) + "/" +
                               std::to_string(stats.sampleCount));
        rec.evidence.push_back(tr("samples: ", "échantillons: ") + std::to_string(sampleCount));

        applyRuleToRecommendation(rec, rec.symptom, "front_understeer_entry");

        rec.direction = tr("reduce front grip demand", "réduire la demande de grip avant");
        rec.suggestedChange = tr("adjust front camber / roll stiffness / tyre pressure",
                                 "ajuster carrossage avant / raideur de roulis / pression pneus");
        rec.text = tr("Understeer tendency at ", "Tendance au sous-virage à ") + phase +
                   tr("; steering demand grows without a matching yaw response.",
                      " ; la demande de volant augmente sans réponse yaw équivalente.");
        return rec;
    }

    if (oversteerRatio >= g_thresholds.oversteerRatioMin && stats.oversteerIndicators > stats.understeerIndicators) {
        rec.issue = true;
        rec.symptom = entryPhase ? "rear_oversteer_entry" : "rear_traction_loss_on_exit";
        rec.context = phase;
        rec.confidence = computeConfidence(oversteerRatio, g_thresholds.oversteerRatioMin,
                                           sampleCount, 0, channelsAvailable, channelsNeeded);
        rec.dataQuality = dataQualityLabel(rec.confidence, language);
        rec.evidence.push_back(tr("oversteer ratio: ", "ratio survirage: ") +
                               formatDouble(oversteerRatio * 100.0, 1) + "%");
        rec.evidence.push_back(tr("indicators: ", "indicateurs: ") +
                               std::to_string(stats.oversteerIndicators) + "/" +
                               std::to_string(stats.sampleCount));
        rec.evidence.push_back(tr("samples: ", "échantillons: ") + std::to_string(sampleCount));

        applyRuleToRecommendation(rec, rec.symptom, "rear_traction_loss_on_exit");

        rec.direction = tr("increase rear stability", "augmenter la stabilité arrière");
        rec.suggestedChange = tr("adjust rear roll stiffness / differential / rear tyre load",
                                 "ajuster raideur de roulis arrière / différentiel / charge pneus arrière");
        rec.text = tr("Oversteer tendency at ", "Tendance au survirage à ") + phase +
                   tr("; yaw increases while steering demand is stable or decreasing.",
                      " ; le yaw augmente alors que la demande de volant est stable ou diminue.");
        return rec;
    }

    rec.issue = false;
    rec.text = tr("No significant handling bias detected at ", "Aucun biais de comportement significatif détecté à ") + phase + ".";
    return rec;
}
