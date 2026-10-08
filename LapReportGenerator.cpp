//---------------------------------------------------------------------------
// LapReportGenerator.cpp - Rule-only report assembly.
//---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "MotecSetupAnalyzer.h"
#include "LapReportGenerator.h"

namespace {

double lapDuration(const LapStats& lap) {
    return lap.endTime - lap.startTime;
}

// Result of lap selection: the laps that pass the validity window plus a full
// breakdown (included / excluded with reasons) for the Summary tab.
struct LapSelectionResult {
    std::vector<const LapStats*> valid;
    LapSelectionInfo info;
};

LapSelectionResult selectValidLaps(const std::vector<LapStats>& laps) {
    LapSelectionResult out;
    out.info.totalLaps = static_cast<int>(laps.size());

    // Collect positive finite lap durations (per index).
    std::vector<double> durations(laps.size(), 0.0);
    for (std::size_t i = 0; i < laps.size(); ++i) {
        const double d = lapDuration(laps[i]);
        if (!std::isfinite(d) || d <= 0.0)
            continue;
        durations[i] = d;
    }

    // Anchor 'best' to the median duration. The median is robust to a single
    // outlier on either side (short partial-lap stub OR long outlier lap),
    // as long as outliers are under 50% of laps — which they always are in
    // practice. This replaces the earlier "min-above-stub-floor" heuristic,
    // which broke when a single long lap pushed the median up and caused the
    // floor to discard every real (short) lap.
    std::vector<double> sorted;
    for (std::size_t i = 0; i < durations.size(); ++i)
        if (durations[i] > 0.0)
            sorted.push_back(durations[i]);

    double best = 0.0;
    if (!sorted.empty()) {
        std::sort(sorted.begin(), sorted.end());
        best = sorted[sorted.size() / 2]; // median
    }

    const double lo = best > 0.0 ? best * g_thresholds.minValidLapTimeMultiplier : 0.0;
    const double hi = best > 0.0 ? best * g_thresholds.maxValidLapTimeMultiplier : 0.0;

    for (std::size_t i = 0; i < laps.size(); ++i) {
        const double d = lapDuration(laps[i]);
        if (!std::isfinite(d) || d <= 0.0) {
            out.info.excludedLaps.push_back(
                LapSelectionInfo::ExcludedLap{laps[i].lapNumber, std::isfinite(d) ? d : 0.0,
                                              LapSelectionInfo::ExcludedLap::Reason::NonFinite});
            continue;
        }
        if (best > 0.0 && d >= lo && d <= hi) {
            out.valid.push_back(&laps[i]);
            out.info.includedLaps.push_back(laps[i].lapNumber);
        } else if (d < lo) {
            out.info.excludedLaps.push_back(
                LapSelectionInfo::ExcludedLap{laps[i].lapNumber, d,
                                              LapSelectionInfo::ExcludedLap::Reason::TooShort});
        } else {
            out.info.excludedLaps.push_back(
                LapSelectionInfo::ExcludedLap{laps[i].lapNumber, d,
                                              LapSelectionInfo::ExcludedLap::Reason::TooLong});
        }
    }
    return out;
}

// Re-classify a window-based selection so that "included" reflects the exact set
// of laps actually averaged (e.g. all-but-last for average mode, top-3 for best
// three). Laps that passed the window but were not used are marked ValidButUnused.
LapSelectionInfo refineSelectionInfo(const LapSelectionResult& base,
                                     const std::vector<const LapStats*>& selected) {
    LapSelectionInfo info;
    info.totalLaps = base.info.totalLaps;

    std::set<int> includedSet;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        info.includedLaps.push_back(selected[i]->lapNumber);
        includedSet.insert(selected[i]->lapNumber);
    }

    // Index the window-based exclusions by lap number for reason lookup.
    for (std::size_t i = 0; i < base.info.excludedLaps.size(); ++i) {
        const LapSelectionInfo::ExcludedLap& e = base.info.excludedLaps[i];
        if (!includedSet.count(e.lapNumber))
            info.excludedLaps.push_back(e);
    }

    // Any valid lap not in the selected set was dropped by the averaging rule.
    for (std::size_t i = 0; i < base.valid.size(); ++i) {
        const int n = base.valid[i]->lapNumber;
        if (!includedSet.count(n))
            info.excludedLaps.push_back(
                LapSelectionInfo::ExcludedLap{n, lapDuration(*base.valid[i]),
                                              LapSelectionInfo::ExcludedLap::Reason::ValidButUnused});
    }

    std::sort(info.excludedLaps.begin(), info.excludedLaps.end(),
              [](const LapSelectionInfo::ExcludedLap& a, const LapSelectionInfo::ExcludedLap& b) {
                  return a.lapNumber < b.lapNumber;
              });
    return info;
}

void averageSummary(StatSummary& target, const StatSummary& source) {
    if (source.count == 0)
        return;
    target.add(source.average());
    target.min = std::min(target.min, source.min);
    target.max = std::max(target.max, source.max);
}

LapStats averageSelectedLaps(const std::vector<const LapStats*>& selected) {
    LapStats average;
    if (selected.empty())
        return average;

    average.rideHeightUnit = selected[0]->rideHeightUnit;
    average.lapNumber = selected[0]->lapNumber;
    average.driverName  = selected[0]->driverName;
    average.vehicleName = selected[0]->vehicleName;
    average.trackName   = selected[0]->trackName;

    for (std::size_t i = 0; i < selected.size(); ++i) {
        const LapStats& lap = *selected[i];
        average.startTime += lap.startTime;
        average.endTime += lap.endTime;
        average.rowCount += lap.rowCount;

        averageSummary(average.waterTemp, lap.waterTemp);
        averageSummary(average.oilTemp, lap.oilTemp);
        averageSummary(average.steerAngle, lap.steerAngle);
        averageSummary(average.yawRate, lap.yawRate);
        averageSummary(average.wheelSlipLF, lap.wheelSlipLF);
        averageSummary(average.wheelSlipRF, lap.wheelSlipRF);
        averageSummary(average.wheelSlipLR, lap.wheelSlipLR);
        averageSummary(average.wheelSlipRR, lap.wheelSlipRR);
        averageSummary(average.pressureLF, lap.pressureLF);
        averageSummary(average.pressureRF, lap.pressureRF);
        averageSummary(average.pressureLR, lap.pressureLR);
        averageSummary(average.pressureRR, lap.pressureRR);
        averageSummary(average.tyreInnerTempLF, lap.tyreInnerTempLF);
        averageSummary(average.tyreCentreTempLF, lap.tyreCentreTempLF);
        averageSummary(average.tyreOuterTempLF, lap.tyreOuterTempLF);
        averageSummary(average.tyreInnerTempRF, lap.tyreInnerTempRF);
        averageSummary(average.tyreCentreTempRF, lap.tyreCentreTempRF);
        averageSummary(average.tyreOuterTempRF, lap.tyreOuterTempRF);
        averageSummary(average.tyreInnerTempLR, lap.tyreInnerTempLR);
        averageSummary(average.tyreCentreTempLR, lap.tyreCentreTempLR);
        averageSummary(average.tyreOuterTempLR, lap.tyreOuterTempLR);
        averageSummary(average.tyreInnerTempRR, lap.tyreInnerTempRR);
        averageSummary(average.tyreCentreTempRR, lap.tyreCentreTempRR);
        averageSummary(average.tyreOuterTempRR, lap.tyreOuterTempRR);
        averageSummary(average.aeroHeight0, lap.aeroHeight0);
        averageSummary(average.aeroHeight1, lap.aeroHeight1);
        averageSummary(average.aeroHeight2, lap.aeroHeight2);
        averageSummary(average.aeroHeight3, lap.aeroHeight3);

        // Suspension velocity histograms (direct channel + derived d(travel)/dt)
        average.damperLF.merge(lap.damperLF);
        average.damperRF.merge(lap.damperRF);
        average.damperLR.merge(lap.damperLR);
        average.damperRR.merge(lap.damperRR);
        average.suspVelLF.merge(lap.suspVelLF);
        average.suspVelRF.merge(lap.suspVelRF);
        average.suspVelLR.merge(lap.suspVelLR);
        average.suspVelRR.merge(lap.suspVelRR);

        // Suspension travel & bumpstop summaries
        averageSummary(average.susTravelLF, lap.susTravelLF);
        averageSummary(average.susTravelRF, lap.susTravelRF);
        averageSummary(average.susTravelLR, lap.susTravelLR);
        averageSummary(average.susTravelRR, lap.susTravelRR);
        averageSummary(average.bumpStopLF, lap.bumpStopLF);
        averageSummary(average.bumpStopRF, lap.bumpStopRF);
        averageSummary(average.bumpStopLR, lap.bumpStopLR);
        averageSummary(average.bumpStopRR, lap.bumpStopRR);

        // Core driving channels
        averageSummary(average.speed, lap.speed);
        averageSummary(average.throttle, lap.throttle);
        averageSummary(average.brakePedal, lap.brakePedal);
        averageSummary(average.rpm, lap.rpm);
        averageSummary(average.latG, lap.latG);
        averageSummary(average.longG, lap.longG);

        // Wheel speeds
        averageSummary(average.wheelSpeedLF, lap.wheelSpeedLF);
        averageSummary(average.wheelSpeedRF, lap.wheelSpeedRF);
        averageSummary(average.wheelSpeedLR, lap.wheelSpeedLR);
        averageSummary(average.wheelSpeedRR, lap.wheelSpeedRR);

        // Brake temps
        averageSummary(average.brakeTempLF, lap.brakeTempLF);
        averageSummary(average.brakeTempRF, lap.brakeTempRF);
        averageSummary(average.brakeTempLR, lap.brakeTempLR);
        averageSummary(average.brakeTempRR, lap.brakeTempRR);

        // Surface temps
        averageSummary(average.surfTempCFL, lap.surfTempCFL);
        averageSummary(average.surfTempCRF, lap.surfTempCRF);
        averageSummary(average.surfTempCLR, lap.surfTempCLR);
        averageSummary(average.surfTempCRR, lap.surfTempCRR);

        // Tyre air temps
        averageSummary(average.tyreAirTempLF, lap.tyreAirTempLF);
        averageSummary(average.tyreAirTempRF, lap.tyreAirTempRF);
        averageSummary(average.tyreAirTempLR, lap.tyreAirTempLR);
        averageSummary(average.tyreAirTempRR, lap.tyreAirTempRR);

        // Brake torque
        averageSummary(average.brakeTorqueLF, lap.brakeTorqueLF);
        averageSummary(average.brakeTorqueRF, lap.brakeTorqueRF);
        averageSummary(average.brakeTorqueLR, lap.brakeTorqueLR);
        averageSummary(average.brakeTorqueRR, lap.brakeTorqueRR);

        // Misc / systems
        averageSummary(average.brakeBiasSetting, lap.brakeBiasSetting);
        averageSummary(average.tcsActive, lap.tcsActive);
        averageSummary(average.absActive, lap.absActive);
        averageSummary(average.aeroBalance, lap.aeroBalance);
        averageSummary(average.lsdTorque, lap.lsdTorque);

        // Environmental / fuel
        averageSummary(average.fuelLevel, lap.fuelLevel);
        averageSummary(average.fuelPressure, lap.fuelPressure);
        averageSummary(average.trackTemp, lap.trackTemp);
        averageSummary(average.ambientTemp, lap.ambientTemp);

        // Handling phase stats
        average.cornerEntry.merge(lap.cornerEntry);
        average.cornerExit.merge(lap.cornerExit);

        // Steer/yaw correlation (pool across laps)
        average.steerYawCov += lap.steerYawCov;
        average.steerSum    += lap.steerSum;
        average.yawSum      += lap.yawSum;
        average.steerSqSum  += lap.steerSqSum;
        average.yawSqSum    += lap.yawSqSum;
        average.steerYawCount += lap.steerYawCount;
    }

    average.startTime /= static_cast<double>(selected.size());
    average.endTime /= static_cast<double>(selected.size());
    return average;
}

std::string severityLabel(AnalysisSeverity severity, ReportLanguage language) {
    if (severity == AnalysisSeverity::Critical)
        return language == ReportLanguage::French ? "[CRITIQUE] " : "[CRITICAL] ";
    if (severity == AnalysisSeverity::Warning)
        return language == ReportLanguage::French ? "[ATTENTION] " : "[WARNING] ";
    return "[INFO] ";
}

} // namespace

// Builds a human-readable list of lap numbers, e.g. "3, 4, 5, 6"
static std::string formatLapList(const std::vector<int>& laps) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < laps.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << laps[i];
    }
    return oss.str();
}

// Formats a duration in seconds as "M:SS.mmm" (e.g. 81.65 → "1:21.650").
static std::string formatLapTime(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0)
        return "";
    const int totalMs = static_cast<int>(seconds * 1000.0 + 0.5);
    const int ms  = totalMs % 1000;
    const int s   = (totalMs / 1000) % 60;
    const int m   = totalMs / 60000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d:%02d.%03d", m, s, ms);
    return std::string(buf);
}

// Returns a short label for an exclusion reason.
static std::string exclusionReasonLabel(LapSelectionInfo::ExcludedLap::Reason reason, ReportLanguage lang) {
    switch (reason) {
    case LapSelectionInfo::ExcludedLap::Reason::NonFinite:
        return lang == ReportLanguage::French ? "durée invalide" : "invalid duration";
    case LapSelectionInfo::ExcludedLap::Reason::TooShort:
        return lang == ReportLanguage::French ? "trop court (outlier)" : "too short (outlier)";
    case LapSelectionInfo::ExcludedLap::Reason::TooLong:
        return lang == ReportLanguage::French ? "trop long (outlier)" : "too long (outlier)";
    case LapSelectionInfo::ExcludedLap::Reason::ValidButUnused:
        return lang == ReportLanguage::French ? "valide mais non inclus dans la moyenne"
                                              : "valid but not used in average";
    }
    return "";
}

// Checks whether a StatSummary has any samples.
static inline bool hasData(const StatSummary& s) { return s.count > 0; }

// Builds the per-section data-availability breakdown for the Summary tab.
static std::string buildDataAvailabilityText(const LapStats& lap, ReportLanguage lang) {
    const bool fr = (lang == ReportLanguage::French);
    std::ostringstream oss;

    struct SectionCheck {
        const char* enName;
        const char* frName;
        bool available;
    };

    // Gather availability for each major data section
    const bool brakeTempOk   = hasData(lap.brakeTempLF) || hasData(lap.brakeTempRF) ||
                               hasData(lap.brakeTempLR) || hasData(lap.brakeTempRR);
    const bool tyreTempOk    = hasData(lap.tyreInnerTempLF) || hasData(lap.tyreCentreTempLF) ||
                               hasData(lap.tyreOuterTempLF) || hasData(lap.surfTempCFL);
    const bool pressureOk    = hasData(lap.pressureLF) || hasData(lap.pressureRF) ||
                               hasData(lap.pressureLR) || hasData(lap.pressureRR);
    const bool suspTravelOk  = hasData(lap.susTravelLF) || hasData(lap.susTravelRF) ||
                               hasData(lap.susTravelLR) || hasData(lap.susTravelRR);
    const bool damperOk      = lap.damperLF.histogram.totalCount > 0 || lap.damperRF.histogram.totalCount > 0 ||
                               lap.damperLR.histogram.totalCount > 0 || lap.damperRR.histogram.totalCount > 0;
    const bool wheelSlipOk   = hasData(lap.wheelSlipLF) || hasData(lap.wheelSlipRF) ||
                               hasData(lap.wheelSlipLR) || hasData(lap.wheelSlipRR);
    const bool brakeTorqueOk = hasData(lap.brakeTorqueLF) || hasData(lap.brakeTorqueRF) ||
                               hasData(lap.brakeTorqueLR) || hasData(lap.brakeTorqueRR);
    const bool engineTempOk  = hasData(lap.waterTemp) || hasData(lap.oilTemp);

    std::vector<SectionCheck> sections;
    sections.push_back({"Brake temperatures", "Températures de freins", brakeTempOk});
    sections.push_back({"Tyre temperatures", "Températures des pneus", tyreTempOk});
    sections.push_back({"Tyre pressures", "Pressions des pneus", pressureOk});
    sections.push_back({"Suspension travel", "Course de suspension", suspTravelOk});
    sections.push_back({"Damper histograms", "Histogrammes d'amortisseurs", damperOk});
    sections.push_back({"Wheel slip", "Glissement des roues", wheelSlipOk});
    sections.push_back({"Brake torque", "Couple de freinage", brakeTorqueOk});
    sections.push_back({"Engine / oil temps", "Températures moteur / huile", engineTempOk});

    // Only show the breakdown if at least one section is missing data
    bool anyMissing = false;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (!sections[i].available) { anyMissing = true; break; }
    }
    if (!anyMissing) return "";

    oss << "\n\n" << (fr ? "Données non disponibles :" : "Data not available:");
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (!sections[i].available) {
            oss << "\n  • " << (fr ? sections[i].frName : sections[i].enName);
            oss << (fr ? " — canaux absents du fichier" : " — channels not present in file");
        }
    }
    return oss.str();
}

ReportResult LapReportGenerator::generate(const LapStats& lap,
                                          ReportLanguage language, ChannelProfile profile,
                                          const LapSelectionInfo* selection) {
    (void)profile;

    ReportResult result;
    AnalysisSettings settings = buildDefaultAnalysisSettings();
    settings.language = language;
    const std::vector<AnalysisResult> findings = evaluateLapWithRules(lap, settings);
    for (std::size_t i = 0; i < findings.size(); ++i) {
        const AnalysisResult& finding = findings[i];
        const ReportSection section = mapAnalysisResultToReportSection(finding);
        SectionData& data = result[section];
        if (!data.content.empty())
            data.content += "\n";
        data.content += severityLabel(finding.severity, language) + finding.title + "\n";
        if (!finding.description.empty())
            data.content += finding.description + "\n";
        if (!finding.recommendation.empty()) {
            data.content += (language == ReportLanguage::French ? "Recommandation : " : "Recommendation: ");
            data.content += finding.recommendation + "\n";
        }
        data.hasRecommendation = true;
    }

    // --- Damper-velocity recommendations (Suspension section) ---
    {
        for (int c = 0; c < 4; ++c) {
            const VelocityHistogram& hist = pickDamperHistogram(lap, c);
            const std::string rec = getDamperRecommendation(hist, c, language);
            if (!rec.empty()) {
                SectionData& data = result[ReportSection::Suspension];
                if (!data.content.empty())
                    data.content += "\n";
                data.content += rec + "\n";
                data.hasRecommendation = true;
            }
        }
    }

    std::ostringstream summary;
    const bool fr = (language == ReportLanguage::French);

    // --- Car & Track ---
    auto displayName = [](const std::string& name) -> const char* {
        if (name.empty() || name == "Unknown vehicle" || name == "Unknown circuit")
            return "N/A";
        return name.c_str();
    };
    summary << (fr ? "Voiture : " : "Car: ") << displayName(lap.vehicleName) << "\n";
	summary << (fr ? "Circuit : " : "Track: ") << displayName(lap.trackName) << "\n\n";

    // --- Lap selection breakdown ---
    if (selection && selection->totalLaps > 0) {
        summary << (fr ? "Tours inclus : " : "Laps included: ")
                << selection->includedLaps.size() << " / " << selection->totalLaps;
        if (!selection->includedLaps.empty())
            summary << " (" << formatLapList(selection->includedLaps) << ")";

        if (!selection->excludedLaps.empty()) {
            summary << "\n" << (fr ? "Tours exclus :" : "Excluded laps:");
            for (std::size_t i = 0; i < selection->excludedLaps.size(); ++i) {
                const LapSelectionInfo::ExcludedLap& ex = selection->excludedLaps[i];
                summary << "\n  • " << (fr ? "Tour" : "Lap") << " " << ex.lapNumber;
                if (std::isfinite(ex.duration) && ex.duration > 0.0) {
                    const std::string t = formatLapTime(ex.duration);
                    if (!t.empty())
                        summary << " (" << t << ")";
                }
                summary << " — " << exclusionReasonLabel(ex.reason, language);
            }
        }
    }

    // --- Rule findings line ---
    if (findings.empty()) {
        summary << "\n\n" << (fr ? "Analyse par règles terminée. Aucun constat."
                                 : "Rule analysis complete. No findings.");
    } else {
        summary << "\n\n" << (fr ? "Analyse par règles terminée. Constats : "
                                 : "Rule analysis complete. Findings: ") << findings.size();
    }

    // --- Data availability breakdown ---
    const std::string dataAvail = buildDataAvailabilityText(lap, language);
    if (!dataAvail.empty())
        summary << dataAvail;

    result[ReportSection::Summary].content = summary.str();
    result[ReportSection::Summary].hasRecommendation = !findings.empty();
    return result;
}

ReportResult LapReportGenerator::generateAverageLaps(const std::vector<LapStats>& laps,
                                                      ReportLanguage language,
                                                      ChannelProfile profile) {
    const LapSelectionResult sel = selectValidLaps(laps);
    if (sel.valid.size() < 3) {
        ReportResult result;
        result[ReportSection::Summary].content = language == ReportLanguage::French
            ? "Au moins trois tours valides sont nécessaires pour une analyse moyenne."
            : "At least three valid laps are required for an average analysis.";
        return result;
    }

    std::vector<const LapStats*> selected;
    for (std::size_t i = 0; i < sel.valid.size(); ++i)
        selected.push_back(sel.valid[i]);
    const LapSelectionInfo info = refineSelectionInfo(sel, selected);
    return generate(averageSelectedLaps(selected), language, profile, &info);
}

ReportResult LapReportGenerator::generateBestThreeLaps(const std::vector<LapStats>& laps,
                                                        ReportLanguage language,
                                                        ChannelProfile profile) {
    const LapSelectionResult sel = selectValidLaps(laps);
    if (sel.valid.empty()) {
        ReportResult result;
        result[ReportSection::Summary].content = language == ReportLanguage::French
            ? "Aucun tour valide pour une analyse des trois meilleurs tours."
            : "No valid laps for a best-three analysis.";
        return result;
    }

    std::vector<std::pair<double, const LapStats*> > candidates;
    for (std::size_t i = 0; i < sel.valid.size(); ++i) {
        const double lapTime = lapDuration(*sel.valid[i]);
        if (lapTime > 0.0)
            candidates.push_back(std::make_pair(lapTime, sel.valid[i]));
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const std::pair<double, const LapStats*>& left,
                                                               const std::pair<double, const LapStats*>& right) {
        return left.first < right.first;
    });

    std::vector<const LapStats*> selected;
    const std::size_t selectedCount = std::min<std::size_t>(3, candidates.size());
    for (std::size_t i = 0; i < selectedCount; ++i)
        selected.push_back(candidates[i].second);
    const LapSelectionInfo info = refineSelectionInfo(sel, selected);
    return generate(averageSelectedLaps(selected), language, profile, &info);
}

LapStats LapReportGenerator::getAverageLapData(const std::vector<LapStats>& laps) {
    const LapSelectionResult sel = selectValidLaps(laps);
    if (sel.valid.size() < 2)
        return LapStats();

    std::vector<const LapStats*> selected;
    for (std::size_t i = 0; i < sel.valid.size(); ++i)
        selected.push_back(sel.valid[i]);
    return averageSelectedLaps(selected);
}

std::vector<LapStats> LapReportGenerator::getValidLaps(const std::vector<LapStats>& laps) {
    const LapSelectionResult sel = selectValidLaps(laps);
    std::vector<LapStats> result;
    for (std::size_t i = 0; i < sel.valid.size(); ++i)
        result.push_back(*sel.valid[i]);
    return result;
}