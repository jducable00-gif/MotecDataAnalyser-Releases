// ===============================
// RecommendationEngine - damper-velocity issue detection.
// evaluateDamperCorner / hasDamperIssue are the live path used by the UI.
// ===============================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "RecommendationEngine.h"

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
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%d", (int)(lowSpeedShare + 0.5));
        std::string pct = pctBuf;
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

// [REMOVED] buildDamperRecommendation + buildHandlingRecommendation were dead code.
// The structured Recommendation pipeline (computeConfidence, dataQualityLabel,
// applyRuleToRecommendation) will be re-wired when the report engine is extended.
