//---------------------------------------------------------------------------
// MotecSetupAnalyzer.h — Types et API d'analyse de la télémétrie MoTeC.
//
// Définit les structures de données (LapStats, histogrammes de butées,
// statistiques de comportement), les enums (ChannelProfile, ReportLanguage),
// la structure Thresholds (seuils configurables) et déclare les fonctions
// d'analyse (parseMotecCsv / parseMotecLd) et de génération de rapport
// (buildLapReport / buildBestThreeLapReport).
//
// Les implémentations se trouvent dans analyze_motec_setup.cpp.
//---------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LapReportGenerator.h"

enum class ChannelProfile {
    EvoM1 = 0,
    AMS2 = 1,
    Generic = AMS2
};

enum class ReportLanguage {
    English = 0,
    French = 1
};

// ===============================
// Thresholds (configurable via MotecDataAnalyser.thresholds.ini)
// Loaded once at startup; changes to the file only take effect on restart.
// ===============================
struct Thresholds {
    // General (minimum sample counts)
    int minDamperSamples = 20;
    int minHandlingSamples = 8;
    int minSteerYawSamples = 20;
    int handlingConfidenceSamples = 30;

    // Tyre pressure (psi) - common across channel profiles
    double pressureLowPsi = 23.0;
    double pressureHighPsi = 33.0;
    double pressureSideDiffMax = 1.5;
    double pressureFrontRearDiffMax = 2.0;   // front/rear axle average difference (psi)

    // Camber target band (C): inner - outer should be within [min, max]
    double camberMinSpread = 3.5;
    double camberMaxSpread = 6.5;

    // Tyre temperature left/right imbalance (C)
    double tyreCentreTempLrDiffMax = 8.0;   // centre surface temp L/R difference
    double tyreAirTempLrDiffMax = 5.0;      // air temp L/R difference (EVO TYRE_TAIR_XX)

    // Differential locking torque - per profile
    double lsdHighEvo = 500.0;
    double lsdLowEvo = 80.0;
    double lsdHighAms2 = 450.0;
    double lsdLowAms2 = 100.0;

    // Damper velocity (mm/s)
    double damperLowSpeedThreshold = 50.0;
    double damperAsymmetryThreshold = 8.0;
    double damperHighSpeedShareMax = 25.0;
    double damperLowSpeedShareMin = 92.0;
    double damperLowSpeedStdDevMax = 25.0;

    // Damper 4-zone targets (%) — HSC/LSC/LSR/HSR
    double damperHscTargetMin = 10.0;
    double damperHscTargetMax = 20.0;
    double damperLscTargetMin = 30.0;
    double damperLscTargetMax = 40.0;
    double damperLsrTargetMin = 30.0;
    double damperLsrTargetMax = 40.0;
    double damperHsrTargetMin = 10.0;
    double damperHsrTargetMax = 20.0;
    // Severity band: points outside target before escalating NORMAL→MODERATE→IMPORTANT
    double damperZoneModerateBand = 5.0;

    // Handling detection
    double brakePedalMin = 10.0;
    double throttleMaxBraking = 35.0;
    double throttleMinAccelerating = 25.0;
    double steerMinCornering = 5.0;
    double latGMinCornering = 0.25;
    double steerChangeMin = 0.5;
    double yawChangeMin = 1.0;
    double latGMaxUndersteer = 1.5;
    double yawMinOversteer = 15.0;
    double steerChangeMaxOversteer = 1.0;
    double understeerRatioMin = 0.35;
    double oversteerRatioMin = 0.35;

    // Thermal (C)
    double waterTempMax = 95.0;
    double oilTempMax = 115.0;

    // Suspension
    double suspensionTravelMax = 8.0;
    double rideHeightDiffMax = 4.0;
    double suspensionVelocityMax = 5.0;
    double suspensionAvgTravelFrontMax = 50.0;
    double suspensionAvgTravelRearMax = 50.0;

    // Brake
    double brakeTempDiffMax = 25.0;
    double brakeBiasMax = 18.0;
    double brakeBiasFrontAvgMin = 90.0;
    double brakeTorqueMin = 1.0;
    double brakeChannelMismatchMax = 100.0;
    double brakeTorqueFrontShareMax = 65.0;
    double brakeTorqueFrontShareMin = 40.0;
    double brakeTorqueLrDiffRatio = 0.15;
    double brakeBiasSettingFrontMax = 60.0;
    double brakeBiasSettingRearMin = 40.0;

    // Channel mismatch detection
    double pressureChannelMismatchMax = 4.0;
    double pressureChannelMismatchMin = 2.0;
    double suspensionChannelMismatchMax = 5.0;
    double suspensionChannelMismatchMin = 2.0;

    // TCS / ABS
    double tcsActiveMax = 10.0;
    double absActiveMax = 10.0;

    // Wheel slip
    double wheelSlipMax = 15.0;

    // Wheel speed imbalance (km/h)
    double wheelSpeedFrontDiffMax = 6.0;
    double wheelSpeedRearDiffMax = 6.0;
    double wheelSpeedTempDiffMax = 8.0;

    // Traction loss
    double tractionRearDiffMax = 7.0;
    double tractionRearTempDiffMax = 8.0;
    double tractionLongGMax = 1.4;
    double tractionFrontDiffMax = 7.0;
    double tractionRearDiffMin = 5.0;
    double tractionRearTempSpreadMax = 10.0;
    double tractionRearPressureSpreadMax = 2.0;

    // Bumpstop
    double bumpStopMin = 0.1;

    // Aero
    double aeroBalanceMax = 8.0;
    double aeroHeightDiffMax = 5.0;
    double aeroSpeedMin = 280.0;

    // Steering / yaw correlation
    double steerYawCorrUndersteer = 0.30;
    double steerYawCorrOversteer = 0.75;
    double speedLowMax = 80.0;
    double speedMediumMax = 140.0;

    // Stability score
    double stabilityScoreMin = 70.0;
    double stabilityCorrUndersteer = 0.25;
    double stabilityCorrOversteer = 0.75;
    double stabilityWheelSpeedDiffMax = 6.0;
    double stabilityPressureDiffMax = 1.5;
    double stabilityTempDiffMax = 8.0;

    // Lap selection
    double lapTimeGapMax = 10.0;
    double minValidLapTimeMultiplier = 0.7; // Valid lap window: best * multiplier (lower bound)
    double maxValidLapTimeMultiplier = 1.3; // Valid lap window: best * multiplier (upper bound)

    // Confidence scoring
    double confidenceLow = 0.30;
    double confidenceMedium = 0.60;
    double confidenceHigh = 0.85;
    int minLapsForReproducibility = 3;
    double histogramYOffset = 5.0; // Percentage offset for Y-axis max (e.g., 5% above highest bin)
	double damperSlowSpeedRange = 25.0; // Symmetrical range around 0 to highlight slow damper section

    // Per-profile accessors
    double pressureLow(ChannelProfile) const {
        return pressureLowPsi;
    }
    double pressureHigh(ChannelProfile) const {
        return pressureHighPsi;
    }
    double lsdHigh(ChannelProfile profile) const {
        return (profile == ChannelProfile::AMS2) ? lsdHighAms2 : lsdLowEvo;
    }
    double lsdLow(ChannelProfile profile) const {
        return (profile == ChannelProfile::AMS2) ? lsdLowAms2 : lsdLowEvo;
    }
};

extern Thresholds g_thresholds;

// ===============================
// AnalysisSettings: centralizes the thresholds used by the analysis engine and
// keeps the rule layer independent from the UI and simulator-specific code.
// ===============================
struct TyreThresholds {
    double camberMinSpread = 3.5;
    double camberMaxSpread = 6.5;
    double innerOuterSpreadC = 5.0;
    double sideImbalanceMaxC = 8.0;
    double pressureFrontRearDiffPsiMax = 2.0;
};

struct EngineThresholds {
    double waterTempWarnC = 100.0;
    double oilTempWarnC = 110.0;
};

struct HandlingThresholds {
    double wheelSlipMax = 15.0;
    double steerMinCornering = 5.0;
    double yawChangeMin = 1.0;
    double yawMinOversteer = 15.0;
    double steerChangeMin = 0.5;
};

struct AnalysisSettings {
    TyreThresholds tyres;
    EngineThresholds engine;
    HandlingThresholds handling;
    bool tyreAnalysisEnabled = true;
    bool engineAnalysisEnabled = true;
    bool pressureAnalysisEnabled = true;
    ReportLanguage language = ReportLanguage::English;
};

enum class AnalysisAlias {
    WaterTemp,
    OilTemp,
    SteerAngleAvg,
    YawRateAvg,
    WheelSlipFrontAvg,
    WheelSlipRearAvg,
    TyrePressFL,
    TyrePressFR,
    TyrePressRL,
    TyrePressRR,
    TyreTempFLInner,
    TyreTempFLMiddle,
    TyreTempFLOuter,
    TyreTempFRInner,
    TyreTempFRMiddle,
    TyreTempFROuter,
    TyreTempRLInner,
    TyreTempRLMiddle,
    TyreTempRLOuter,
    TyreTempRRInner,
    TyreTempRRMiddle,
    TyreTempRROuter,
    RideHeightFL,
    RideHeightFR,
    RideHeightRL,
    RideHeightRR,
    Rake
};

enum class VirtualChannelOperation {
    DifferenceOfAverages
};

enum class MappingSource {
    MotecFile,
    LiveSharedMemory,
    Virtual
};

enum class ChannelMappingType {
    Raw,
    Virtual
};

struct ChannelMapping {
    std::string alias;
    ChannelMappingType type = ChannelMappingType::Raw;
    ChannelProfile profile = ChannelProfile::Generic;
    MappingSource source = MappingSource::MotecFile;
    std::string channel;
    std::string standardUnit;
    std::string sourceUnit;
    double factor = 1.0;
    double offset = 0.0;
    AnalysisAlias virtualAlias = AnalysisAlias::Rake;
    VirtualChannelOperation operation = VirtualChannelOperation::DifferenceOfAverages;
    std::vector<AnalysisAlias> positiveInputs;
    std::vector<AnalysisAlias> negativeInputs;
};

// ===============================
// DataProvider: single access gateway for channel values. This is the first
// abstraction layer of the refactor plan and allows rules to request aliases or
// virtual channels independently from the simulator-specific mapping.
// ===============================
class DataProvider {
public:
    void clear() {
        m_values.clear();
        m_virtualMappings.clear();
    }

    void setValue(AnalysisAlias alias, double value) {
        m_values[alias] = value;
    }

    void setChannelMappings(const std::vector<ChannelMapping>& mappings) {
        m_virtualMappings.clear();
        for (std::size_t i = 0; i < mappings.size(); ++i) {
            if (mappings[i].type == ChannelMappingType::Virtual)
                m_virtualMappings[mappings[i].virtualAlias] = mappings[i];
        }
    }

    bool hasValue(AnalysisAlias alias) const {
        double value = 0.0;
        return getValue(alias, value);
    }

    bool getValue(AnalysisAlias alias, double& outValue) const {
        return getValueInternal(alias, outValue, 0);
    }

    const std::map<AnalysisAlias, double>& values() const {
        return m_values;
    }

private:
    bool getValueInternal(AnalysisAlias alias, double& outValue, std::size_t depth) const {
        if (depth > 16)
            return false;

        std::map<AnalysisAlias, double>::const_iterator it = m_values.find(alias);
        if (it != m_values.end()) {
            outValue = it->second;
            return true;
        }

        std::map<AnalysisAlias, ChannelMapping>::const_iterator definition = m_virtualMappings.find(alias);
        if (definition == m_virtualMappings.end() ||
            definition->second.operation != VirtualChannelOperation::DifferenceOfAverages ||
            definition->second.positiveInputs.empty() || definition->second.negativeInputs.empty())
            return false;

        double positiveSum = 0.0;
        for (std::size_t i = 0; i < definition->second.positiveInputs.size(); ++i) {
            double value = 0.0;
            if (!getValueInternal(definition->second.positiveInputs[i], value, depth + 1))
                return false;
            positiveSum += value;
        }

        double negativeSum = 0.0;
        for (std::size_t i = 0; i < definition->second.negativeInputs.size(); ++i) {
            double value = 0.0;
            if (!getValueInternal(definition->second.negativeInputs[i], value, depth + 1))
                return false;
            negativeSum += value;
        }

        outValue = positiveSum / definition->second.positiveInputs.size() -
                   negativeSum / definition->second.negativeInputs.size();
        return std::isfinite(outValue);
    }

    std::map<AnalysisAlias, double> m_values;
    std::map<AnalysisAlias, ChannelMapping> m_virtualMappings;
};

enum class AnalysisSeverity {
    Info = 0,
    Warning = 1,
    Critical = 2
};

struct AnalysisResult {
    AnalysisSeverity severity = AnalysisSeverity::Info;
    std::string category;
    std::string title;
    std::string description;
    std::string recommendation;
};

inline std::string normalizeCategoryName(const std::string& category) {
    std::string normalized = category;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return normalized;
}

inline ReportSection mapAnalysisResultToReportSection(const AnalysisResult& result) {
    const std::string category = normalizeCategoryName(result.category);
    if (category == "ENGINE" || category == "ENGINECOOLING" || category == "COOLING")
        return ReportSection::EngineCooling;
    if (category == "TYRES" || category == "TYRE")
        return ReportSection::Tyre;
    if (category == "BRAKES" || category == "BRAKE")
        return ReportSection::Brake;
    if (category == "SUSPENSION" || category == "DAMPER")
        return ReportSection::Suspension;
    if (category == "HANDLING" || category == "TRACTION" || category == "STEERING")
        return ReportSection::Handling;
    return ReportSection::General;
}

class AnalysisRule {
public:
    virtual ~AnalysisRule() {}
    virtual std::vector<AnalysisAlias> requiredInputs() const = 0;
    virtual std::vector<AnalysisResult> evaluate(const DataProvider& provider,
                                                const AnalysisSettings& settings) const = 0;
};

class AnalysisRegistry {
public:
    void addRule(std::unique_ptr<AnalysisRule> rule) {
        if (rule)
            m_rules.push_back(std::move(rule));
    }

    std::vector<AnalysisResult> evaluateAll(const DataProvider& provider,
                                           const AnalysisSettings& settings) const {
        std::vector<AnalysisResult> results;
        for (std::size_t i = 0; i < m_rules.size(); ++i) {
            const std::vector<AnalysisResult> ruleResults = m_rules[i]->evaluate(provider, settings);
            results.insert(results.end(), ruleResults.begin(), ruleResults.end());
        }
        return results;
    }

private:
    std::vector<std::unique_ptr<AnalysisRule> > m_rules;
};

class EngineTemperatureRule : public AnalysisRule {
public:
    std::vector<AnalysisAlias> requiredInputs() const override {
        return std::vector<AnalysisAlias>{ AnalysisAlias::WaterTemp, AnalysisAlias::OilTemp };
    }

    std::vector<AnalysisResult> evaluate(const DataProvider& provider,
                                        const AnalysisSettings& settings) const override {
        std::vector<AnalysisResult> results;
        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        if (settings.engineAnalysisEnabled) {
            double waterTemp = 0.0;
            if (provider.getValue(AnalysisAlias::WaterTemp, waterTemp) && waterTemp > settings.engine.waterTempWarnC) {
                AnalysisResult result;
                result.severity = AnalysisSeverity::Critical;
                result.category = "Engine";
                result.title = tr("Water temperature high", "Température d'eau élevée");
                result.description = tr("Water temperature exceeds the warning threshold.", "La température de l'eau dépasse le seuil d'alerte.");
                result.recommendation = tr("Check radiator flow, coolant level and fan operation.", "Vérifier le flux du radiateur, le niveau de liquide de refroidissement et le fonctionnement du ventilateur.");
                results.push_back(result);
            }

            double oilTemp = 0.0;
            if (provider.getValue(AnalysisAlias::OilTemp, oilTemp) && oilTemp > settings.engine.oilTempWarnC) {
                AnalysisResult result;
                result.severity = AnalysisSeverity::Critical;
                result.category = "Engine";
                result.title = tr("Oil temperature high", "Température d'huile élevée");
                result.description = tr("Oil temperature exceeds the warning threshold.", "La température de l'huile dépasse le seuil d'alerte.");
                result.recommendation = tr("Verify oil cooling and reduce sustained load.", "Vérifier le refroidissement de l'huile et réduire la charge soutenue.");
                results.push_back(result);
            }
        }

        return results;
    }
};

class TyreTemperatureRule : public AnalysisRule {
public:
    std::vector<AnalysisAlias> requiredInputs() const override {
        return std::vector<AnalysisAlias>{
            AnalysisAlias::TyreTempFLInner, AnalysisAlias::TyreTempFLMiddle, AnalysisAlias::TyreTempFLOuter,
            AnalysisAlias::TyreTempFRInner, AnalysisAlias::TyreTempFRMiddle, AnalysisAlias::TyreTempFROuter,
            AnalysisAlias::TyreTempRLInner, AnalysisAlias::TyreTempRLMiddle, AnalysisAlias::TyreTempRLOuter,
            AnalysisAlias::TyreTempRRInner, AnalysisAlias::TyreTempRRMiddle, AnalysisAlias::TyreTempRROuter
        };
    }

    std::vector<AnalysisResult> evaluate(const DataProvider& provider,
                                        const AnalysisSettings& settings) const override {
        std::vector<AnalysisResult> results;

        if (!settings.tyreAnalysisEnabled)
            return results;

        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        static const char* positions[] = { "FL", "FR", "RL", "RR" };
        static const AnalysisAlias innerAliases[] = {
            AnalysisAlias::TyreTempFLInner, AnalysisAlias::TyreTempFRInner,
            AnalysisAlias::TyreTempRLInner, AnalysisAlias::TyreTempRRInner
        };
        static const AnalysisAlias middleAliases[] = {
            AnalysisAlias::TyreTempFLMiddle, AnalysisAlias::TyreTempFRMiddle,
            AnalysisAlias::TyreTempRLMiddle, AnalysisAlias::TyreTempRRMiddle
        };
        static const AnalysisAlias outerAliases[] = {
            AnalysisAlias::TyreTempFLOuter, AnalysisAlias::TyreTempFROuter,
            AnalysisAlias::TyreTempRLOuter, AnalysisAlias::TyreTempRROuter
        };

        for (std::size_t i = 0; i < 4; ++i) {
            const std::string pos = positions[i];
            double inner = 0.0;
            double middle = 0.0;
            double outer = 0.0;

            const bool hasInner = provider.getValue(innerAliases[i], inner);
            const bool hasMiddle = provider.getValue(middleAliases[i], middle);
            const bool hasOuter = provider.getValue(outerAliases[i], outer);

			if (hasInner && hasOuter) {
				if (inner <= outer+g_thresholds.camberMinSpread) {
					AnalysisResult result;
					result.severity = AnalysisSeverity::Warning;
					result.category = "Tyres";
					result.title = pos + tr(" insufficient negative camber", " carrossage négatif insuffisant");
					result.description = pos + tr(" tyre inner band is not hotter than the outer band.", " pneu : la bande interne n'est pas plus chaude que la bande externe.");
					result.recommendation = tr("Increase negative camber slightly and re-check the same corner on comparable laps.", "Augmenter légèrement le carrossage négatif et revérifier le même virage sur des tours comparables.");
					results.push_back(result);
				}
			}

			if (hasInner && hasOuter) {
				if (inner >= outer+g_thresholds.camberMaxSpread) {
					AnalysisResult result;
					result.severity = AnalysisSeverity::Warning;
					result.category = "Tyres";
					result.title = pos + tr(" too much negative camber", " carrossage négatif insuffisant");
					result.description = pos + tr(" tyre inner band is too much hotter than the outer band.", " pneu : la bande interne est trop chaude par rapport à la bande externe.");
					result.recommendation = tr("decrease negative camber slightly and re-check the same corner on comparable laps.", "Diminuer légèrement le carrossage négatif et revérifier le même virage sur des tours comparables.");
					results.push_back(result);
				}
			}

			if (hasInner && hasMiddle && hasOuter) {
                const double shoulderMin = std::min(inner, outer);
                const double shoulderMax = std::max(inner, outer);

                if (middle <= shoulderMin) {
                    AnalysisResult result;
                    result.severity = AnalysisSeverity::Warning;
                    result.category = "Tyres";
                    result.title = pos + tr(" center band below shoulders", " bande centrale sous les épaules");
                    result.description = pos + tr(" tyre center band is not strictly between the inner and outer bands; both shoulders are as hot or hotter.", " pneu : la bande centrale n'est pas strictement entre les bandes interne et externe ; les deux épaules sont aussi chaudes ou plus chaudes.");
                    result.recommendation = tr("Raise the starting pressure slightly and re-check the hot tyre pressure and temperature profile.", "Augmenter légèrement la pression de départ et revérifier la pression et le profil de température du pneu à chaud.");
                    results.push_back(result);
                } else if (middle >= shoulderMax) {
                    AnalysisResult result;
                    result.severity = AnalysisSeverity::Warning;
                    result.category = "Tyres";
                    result.title = pos + tr(" center band above shoulders", " bande centrale au-dessus des épaules");
                    result.description = pos + tr(" tyre center band is not strictly between the inner and outer bands; the center is as hot or hotter.", " pneu : la bande centrale n'est pas strictement entre les bandes interne et externe ; le centre est aussi chaud ou plus chaud.");
                    result.recommendation = tr("Reduce the starting pressure slightly and re-check the hot tyre pressure and temperature profile.", "Réduire légèrement la pression de départ et revérifier la pression et le profil de température du pneu à chaud.");
                    results.push_back(result);
                }
            }
        }

        return results;
    }
};

class TyrePressureRule : public AnalysisRule {
public:
    std::vector<AnalysisAlias> requiredInputs() const override {
        return std::vector<AnalysisAlias>{
            AnalysisAlias::TyrePressFL, AnalysisAlias::TyrePressFR,
            AnalysisAlias::TyrePressRL, AnalysisAlias::TyrePressRR
        };
    }

    std::vector<AnalysisResult> evaluate(const DataProvider& provider,
                                        const AnalysisSettings& settings) const override {
        std::vector<AnalysisResult> results;

        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        if (!settings.tyreAnalysisEnabled || !settings.pressureAnalysisEnabled)
            return results;

        double fl = 0.0;
        double fr = 0.0;
        double rl = 0.0;
        double rr = 0.0;
        bool hasFl = provider.getValue(AnalysisAlias::TyrePressFL, fl);
        bool hasFr = provider.getValue(AnalysisAlias::TyrePressFR, fr);
        bool hasRl = provider.getValue(AnalysisAlias::TyrePressRL, rl);
        bool hasRr = provider.getValue(AnalysisAlias::TyrePressRR, rr);

        if (!hasFl || !hasFr || !hasRl || !hasRr)
            return results;

        // L/R pressure split is intentionally used to compensate contact patch vs camber — no imbalance warning.

        const double frontAvg = (fl + fr) * 0.5;
        const double rearAvg = (rl + rr) * 0.5;
        if (std::abs(frontAvg - rearAvg) > settings.tyres.pressureFrontRearDiffPsiMax) {
            AnalysisResult result;
            result.severity = AnalysisSeverity::Warning;
            result.category = "Tyres";
            result.title = tr("Front/rear pressure mismatch", "Désaccord de pression avant/arrière");
            result.description = tr("Front and rear axle pressures are unevenly distributed.",
                                    "Les pressions des essieux avant et arrière sont inégalement réparties.");
            result.recommendation = tr("Reduce the axle pressure difference to improve balance.",
                                       "Réduire l'écart de pression entre les essieux pour améliorer l'équilibre.");
            results.push_back(result);
        }

        return results;
    }
};

class HandlingBalanceRule : public AnalysisRule {
public:
    std::vector<AnalysisAlias> requiredInputs() const override {
        return std::vector<AnalysisAlias>{
            AnalysisAlias::SteerAngleAvg, AnalysisAlias::YawRateAvg,
            AnalysisAlias::WheelSlipRearAvg, AnalysisAlias::WheelSlipFrontAvg
        };
    }

    std::vector<AnalysisResult> evaluate(const DataProvider& provider,
                                        const AnalysisSettings& settings) const override {
        std::vector<AnalysisResult> results;

        auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
            return settings.language == ReportLanguage::French ? fr : en;
        };

        double steer = 0.0;
        double yaw = 0.0;
        double rearSlip = 0.0;
        double frontSlip = 0.0;

        const bool hasSteer = provider.getValue(AnalysisAlias::SteerAngleAvg, steer);
        const bool hasYaw = provider.getValue(AnalysisAlias::YawRateAvg, yaw);
        const bool hasRearSlip = provider.getValue(AnalysisAlias::WheelSlipRearAvg, rearSlip);
        const bool hasFrontSlip = provider.getValue(AnalysisAlias::WheelSlipFrontAvg, frontSlip);

        if (hasRearSlip && rearSlip > settings.handling.wheelSlipMax) {
            AnalysisResult result;
            result.severity = AnalysisSeverity::Warning;
            result.category = "Handling";
            result.title = tr("Rear wheel slip high", "Patinage des roues arrière élevé");
            result.description = tr("Rear wheels are spending too much time slipping compared to the handling baseline.",
                                    "Les roues arrière patinent trop par rapport à la référence de comportement.");
            result.recommendation = tr("Reduce rear slip with more rear grip, a softer differential setup or better traction.",
                                       "Réduire le patinage arrière avec plus d'adhérence arrière, un différentiel plus souple ou une meilleure traction.");
            results.push_back(result);
        }

        if (hasFrontSlip && frontSlip > settings.handling.wheelSlipMax) {
            AnalysisResult result;
            result.severity = AnalysisSeverity::Warning;
            result.category = "Handling";
            result.title = tr("Front wheel slip high", "Patinage des roues avant élevé");
            result.description = tr("Front wheels are slipping more than expected under cornering or braking.",
                                    "Les roues avant patinent plus que prévu en virage ou au freinage.");
            result.recommendation = tr("Check brake bias, front tyre load and front grip balance.",
                                       "Vérifier le répartiteur de freinage, la charge des pneus avant et l'équilibre d'adhérence avant.");
            results.push_back(result);
        }

        if (hasSteer && hasYaw) {
            if (std::abs(steer) > settings.handling.steerMinCornering && std::abs(yaw) < settings.handling.yawChangeMin) {
                AnalysisResult result;
                result.severity = AnalysisSeverity::Info;
                result.category = "Handling";
                result.title = tr("Potential understeer", "Sous-virage potentiel");
                result.description = tr("Steering input is present while yaw response remains low, suggesting understeer.",
                                        "Une sollicitation du volant est présente tandis que la réponse en lacet reste faible, suggérant un sous-virage.");
                result.recommendation = tr("Increase front load or reduce front grip imbalance to improve turn-in.",
                                           "Augmenter la charge avant ou réduire le déséquilibre d'adhérence avant pour améliorer l'entrée en virage.");
                results.push_back(result);
            }

            if (std::abs(yaw) > settings.handling.yawMinOversteer && std::abs(steer) < settings.handling.steerChangeMin) {
                AnalysisResult result;
                result.severity = AnalysisSeverity::Warning;
                result.category = "Handling";
                result.title = tr("Potential oversteer", "Survirage potentiel");
                result.description = tr("The car is rotating faster than expected for the steering input.",
                                        "La voiture tourne plus vite que prévu pour la sollicitation du volant.");
                result.recommendation = tr("Reduce rear slip or add rear grip to improve stability.",
                                           "Réduire le patinage arrière ou ajouter de l'adhérence arrière pour améliorer la stabilité.");
                results.push_back(result);
            }
        }

        return results;
    }
};

// ===============================
// CauseEntry: one possible cause for a detected symptom, with a weight (0-1)
// and the suggested adjustment direction.
// ===============================
struct CauseEntry {
    std::string cause;        // e.g. "rear_rebound_too_high"
    double weight;            // 0.0 – 1.0 (relative probability)
    std::string adjustment;   // e.g. "reduce_rear_rebound"
    std::string direction;    // e.g. "softer", "stiffer", "reduce", "increase"
};

// ===============================
// Recommendation: structured diagnostic output replacing the old flat RecItem.
// Contains confidence, evidence, possible causes, and validation channels.
// ===============================
struct Recommendation {
    std::string symptom;              // e.g. "rear_traction_loss_on_exit"
    std::string text;                 // display text (localized)
    bool issue = false;               // true = problem detected
    double confidence = 0.0;          // 0.0 – 1.0
    std::string dataQuality;          // "HIGH" / "MEDIUM" / "LOW" / "INSUFFICIENT"
    std::vector<std::string> evidence;   // e.g. ["rear wheel slip +14%", "rebound velocity 320 mm/s"]
    std::vector<CauseEntry> possibleCauses;
    std::string direction;            // "softer" / "stiffer" / "reduce" / "increase"
    std::string suggestedChange;      // "-1 click" or "slightly" if amplitude uncertain
    std::vector<std::string> validationChannels; // e.g. ["rear_wheel_slip", "exit_speed"]
    std::string context;              // e.g. "corner exit, throttle > 70%"
};

// ===============================
// StatSummary (robuste)
// ===============================
struct StatSummary {
    double sum  = 0.0;
    double min  = std::numeric_limits<double>::infinity();
    double max  = -std::numeric_limits<double>::infinity();
    std::size_t count = 0;

    void add(double value) {
        if (!std::isfinite(value)) return;
        sum += value;
        if (value < min) min = value;
        if (value > max) max = value;
        ++count;
    }

    double average() const {
        if (count == 0) return 0.0;
        return sum / static_cast<double>(count);
    }
};

// ===============================
// VelocityHistogram: distribution of damper shaft speed samples (mm/s),
// used to evaluate whether the curve approaches an ideal bell shape.
// ===============================
struct VelocityHistogram {
    static const int kBinCount = 41; // -205..+205 mm/s in 10 mm/s bins, center bin at [-5, 5]

    std::size_t bins[kBinCount] = {};
    std::size_t totalCount = 0;
    double sum = 0.0;
    double sumSq = 0.0;

    static double binWidth() { return 10.0; }
    static double rangeMax() { return 205.0; }

    void add(double velocityMmPerSec) {
        if (!std::isfinite(velocityMmPerSec)) return;

        // Filter out extreme noise spikes (e.g., > 500 mm/s) to avoid distorting stats.
        if (std::abs(velocityMmPerSec) > 500.0) return;

        totalCount++;
        sum += velocityMmPerSec;
        sumSq += velocityMmPerSec * velocityMmPerSec;

        // Only increment bins for values within the visible range to avoid outer-bin bloating.
        if (velocityMmPerSec >= -rangeMax() && velocityMmPerSec <= rangeMax()) {
            int bin = static_cast<int>(std::floor((velocityMmPerSec + rangeMax()) / binWidth()));
            if (bin < 0) bin = 0;
            if (bin >= kBinCount) bin = kBinCount - 1;
            bins[bin]++;
        }
    }

    double mean() const {
        return totalCount == 0 ? 0.0 : sum / static_cast<double>(totalCount);
    }

    double stdDev() const {
        if (totalCount < 2) return 0.0;
        double m = mean();
        double variance = sumSq / static_cast<double>(totalCount) - m * m;
        return variance > 0.0 ? std::sqrt(variance) : 0.0;
    }

    // Percentage of samples whose velocity falls in [loInclusive, hiExclusive)
    double percentInRange(double loInclusive, double hiExclusive) const {
        if (totalCount == 0) return 0.0;
        std::size_t c = 0;
        for (int i = 0; i < kBinCount; ++i) {
            double binCenter = -rangeMax() + (i + 0.5) * binWidth();
            if (binCenter >= loInclusive && binCenter < hiExclusive)
                c += bins[i];
        }
        return 100.0 * static_cast<double>(c) / static_cast<double>(totalCount);
    }

    void merge(const VelocityHistogram& other) {
        for (int i = 0; i < kBinCount; ++i)
            bins[i] += other.bins[i];
        totalCount += other.totalCount;
        sum += other.sum;
        sumSq += other.sumSq;
    }
};

// ===============================
// DamperCornerStats: derives shaft velocity from consecutive suspension
// travel samples (dTravel/dt) and accumulates it into a VelocityHistogram.
// ===============================
struct DamperCornerStats {
    VelocityHistogram histogram;
    double lastTravel = std::numeric_limits<double>::quiet_NaN();
    double lastTime = std::numeric_limits<double>::quiet_NaN();

    void addSample(double travelMm, double timeSeconds) {
        if (std::isfinite(lastTravel) && std::isfinite(lastTime)) {
            double dt = timeSeconds - lastTime;
            if (dt > 0.0005) // ignore duplicate/near-zero timestamps
                histogram.add((travelMm - lastTravel) / dt);
        }
        lastTravel = travelMm;
        lastTime = timeSeconds;
    }

    void merge(const DamperCornerStats& other) {
        histogram.merge(other.histogram);
    }
};

// ===============================
// Damper 4-zone analysis (HSC/LSC/LSR/HSR)
// Zone split at ±damperSlowSpeedRange mm/s:
//   v < -B = HSC, -B <= v < 0 = LSC, 0 <= v <= B = LSR, v > B = HSR
// ===============================
enum class DamperZoneSeverity { Normal, Moderate, Important };

struct DamperZoneResult {
    double pct = 0.0;                    // measured percentage of samples in zone
    DamperZoneSeverity severity = DamperZoneSeverity::Normal;
    bool aboveTarget = false;            // true if above target max, false if below target min
};

struct DamperZoneReport {
    DamperZoneResult hsc;
    DamperZoneResult lsc;
    DamperZoneResult lsr;
    DamperZoneResult hsr;

    // Global balances (diagnostic only)
    double compressionPct = 0.0;         // HSC + LSC
    double reboundPct = 0.0;             // LSR + HSR
    double lowSpeedPct = 0.0;            // LSC + LSR
    double highSpeedPct = 0.0;           // HSC + HSR

    bool hasData = false;                // enough samples to evaluate
    std::string text;                    // localized recommendation (empty if all normal)

    bool anyIssue() const {
        return hsc.severity != DamperZoneSeverity::Normal ||
               lsc.severity != DamperZoneSeverity::Normal ||
               lsr.severity != DamperZoneSeverity::Normal ||
               hsr.severity != DamperZoneSeverity::Normal;
    }
};

struct HandlingPhaseStats {
    std::size_t sampleCount = 0;
    std::size_t understeerIndicators = 0;
    std::size_t oversteerIndicators = 0;
    std::size_t brakingSamples = 0;
    std::size_t accelerationSamples = 0;
    double suspensionAsymmetrySum = 0.0;

    void addIndicator(bool understeer, bool oversteer, bool braking,
                      bool accelerating, double suspensionAsymmetry) {
        ++sampleCount;
        if (understeer) ++understeerIndicators;
        if (oversteer) ++oversteerIndicators;
        if (braking) ++brakingSamples;
        if (accelerating) ++accelerationSamples;
        if (std::isfinite(suspensionAsymmetry))
            suspensionAsymmetrySum += suspensionAsymmetry;
    }

    void merge(const HandlingPhaseStats& other) {
        sampleCount += other.sampleCount;
        understeerIndicators += other.understeerIndicators;
        oversteerIndicators += other.oversteerIndicators;
        brakingSamples += other.brakingSamples;
        accelerationSamples += other.accelerationSamples;
        suspensionAsymmetrySum += other.suspensionAsymmetrySum;
    }
};

enum class RideHeightUnit {
    Millimeters,
    Centimeters
};

// ===============================
// LapStats (blind� + coh�rent MoTeC)
// ===============================
struct LapStats {
    int lapNumber = 0;
    double startTime = 0.0;
    double endTime = 0.0;
    std::size_t rowCount = 0;
    RideHeightUnit rideHeightUnit = RideHeightUnit::Millimeters;

    std::string driverName;
    std::string vehicleName;
    std::string trackName;

    // Core driving channels
    StatSummary speed;
    StatSummary throttle;
    StatSummary brakePedal;
    StatSummary rpm;
    StatSummary steerAngle;
    StatSummary yawRate;
    StatSummary latG;
    StatSummary longG;

    // Wheel speeds
    StatSummary wheelSpeedLF;
    StatSummary wheelSpeedRF;
    StatSummary wheelSpeedLR;
    StatSummary wheelSpeedRR;

    // Brake temps
    StatSummary brakeTempLF;
    StatSummary brakeTempRF;
    StatSummary brakeTempLR;
    StatSummary brakeTempRR;

    // Tyre pressures
    StatSummary pressureLF;
    StatSummary pressureRF;
    StatSummary pressureLR;
    StatSummary pressureRR;

    // Tyre centre temps (MoTeC CSV only provides C_SURF_TEMP_XX)
    StatSummary surfTempCFL;
    StatSummary surfTempCRF;
    StatSummary surfTempCLR;
    StatSummary surfTempCRR;

    // Tyre shoulder temps (inner / centre / outer)
    StatSummary tyreInnerTempLF;
    StatSummary tyreCentreTempLF;
    StatSummary tyreOuterTempLF;
    StatSummary tyreInnerTempRF;
    StatSummary tyreCentreTempRF;
    StatSummary tyreOuterTempRF;
    StatSummary tyreInnerTempLR;
    StatSummary tyreCentreTempLR;
    StatSummary tyreOuterTempLR;
    StatSummary tyreInnerTempRR;
    StatSummary tyreCentreTempRR;
    StatSummary tyreOuterTempRR;

    // Tyre air temps (EVO TYRE_TAIR_XX)
    StatSummary tyreAirTempLF;
    StatSummary tyreAirTempRF;
    StatSummary tyreAirTempLR;
    StatSummary tyreAirTempRR;

    // Brake torque (EVO BRAKE_TORQUE_XX)
    StatSummary brakeTorqueLF;
    StatSummary brakeTorqueRF;
    StatSummary brakeTorqueLR;
    StatSummary brakeTorqueRR;

    // Wheel slip ratio (EVO WHEEL_SR_XX)
    StatSummary wheelSlipLF;
    StatSummary wheelSlipRF;
    StatSummary wheelSlipLR;
    StatSummary wheelSlipRR;

    // Direct suspension velocity (AMS2 Susp Vel XX)
    VelocityHistogram suspVelLF;
    VelocityHistogram suspVelRF;
    VelocityHistogram suspVelLR;
    VelocityHistogram suspVelRR;

    // Brake bias setting (AMS2 Brake Bias Setting)
    StatSummary brakeBiasSetting;

    // TCS / ABS activation (EVO TC/ABS, AMS2 TCS Active/ABS Active)
    StatSummary tcsActive;
    StatSummary absActive;

    // Suspension
    StatSummary susTravelLF;
    StatSummary susTravelRF;
    StatSummary susTravelLR;
    StatSummary susTravelRR;

    StatSummary bumpStopLF;
    StatSummary bumpStopRF;
    StatSummary bumpStopLR;
    StatSummary bumpStopRR;

    // Damper shaft velocity histograms, derived from suspension travel
    DamperCornerStats damperLF;
    DamperCornerStats damperRF;
    DamperCornerStats damperLR;
    DamperCornerStats damperRR;

    // Entry and exit handling evidence derived from the synchronized samples.
    HandlingPhaseStats cornerEntry;
    HandlingPhaseStats cornerExit;
    bool handlingHasPrevious = false;
    double previousHandlingSteer = 0.0;
    double previousHandlingYaw = 0.0;

    // Aero
    StatSummary aeroHeight0;
    StatSummary aeroHeight1;
    StatSummary aeroHeight2;
    StatSummary aeroHeight3;
    StatSummary aeroBalance;

    // Differential
    StatSummary lsdTorque;

    // Environmental / fuel / thermal monitoring
    StatSummary oilTemp;
    StatSummary waterTemp;
    StatSummary fuelLevel;
    StatSummary fuelPressure;
    StatSummary trackTemp;
    StatSummary ambientTemp;

    // Steering/Yaw correlation
    double steerYawCov = 0.0;
    double steerSum = 0.0;
    double yawSum = 0.0;
    double steerSqSum = 0.0;
    double yawSqSum = 0.0;
    std::size_t steerYawCount = 0;   // IMPORTANT : compteur ddi

    void addHandlingSample(double steer, double yaw, double latG, double longG,
                           double brake, double throttle, double suspensionAsymmetry) {
        if (!std::isfinite(steer) || !std::isfinite(yaw) ||
            !std::isfinite(latG) || !std::isfinite(longG))
            return;

        const bool braking = brake > g_thresholds.brakePedalMin && throttle < g_thresholds.throttleMaxBraking;
        const bool accelerating = throttle > g_thresholds.throttleMinAccelerating && brake < g_thresholds.brakePedalMin;
        const bool cornering = std::abs(steer) > g_thresholds.steerMinCornering && std::abs(latG) > g_thresholds.latGMinCornering;
        if (handlingHasPrevious && cornering) {
            const double steerChange = std::abs(steer) - std::abs(previousHandlingSteer);
            const double yawChange = std::abs(yaw) - std::abs(previousHandlingYaw);
            const bool steeringDemandGrowing = steerChange > g_thresholds.steerChangeMin;
            const bool yawResponseGrowing = yawChange > g_thresholds.yawChangeMin;
            const bool understeer = steeringDemandGrowing && !yawResponseGrowing && std::abs(latG) < g_thresholds.latGMaxUndersteer;
            const bool oversteer = yawResponseGrowing && std::abs(yaw) > g_thresholds.yawMinOversteer &&
                                   std::abs(steer) <= std::abs(previousHandlingSteer) + g_thresholds.steerChangeMaxOversteer;

            if (braking)
                cornerEntry.addIndicator(understeer, oversteer, true, false, suspensionAsymmetry);
            if (accelerating)
                cornerExit.addIndicator(understeer, oversteer, false, true, suspensionAsymmetry);
        }

        previousHandlingSteer = steer;
        previousHandlingYaw = yaw;
        handlingHasPrevious = true;
    }

    // ===============================
    // addSteerYaw (blind)
    // ===============================
    void addSteerYaw(double steer, double yaw) {
        if (!std::isfinite(steer) || !std::isfinite(yaw))
            return;

        steerSum   += steer;
        yawSum     += yaw;
        steerSqSum += steer * steer;
        yawSqSum   += yaw * yaw;
        steerYawCov += steer * yaw;
        ++steerYawCount;
    }

    // ===============================
    // steerYawCorrelation (blind)
    // ===============================
    double steerYawCorrelation() const {
        std::size_t n = steerYawCount;
        if (n < 2)
            return 0.0;

        double sSum   = steerSum;
        double ySum   = yawSum;
        double sSqSum = steerSqSum;
        double ySqSum = yawSqSum;
        double cov    = steerYawCov;

        double num = cov * n - sSum * ySum;
        double denLeft  = sSqSum * n - sSum * sSum;
        double denRight = ySqSum * n - ySum * ySum;

        if (denLeft <= 0.0 || denRight <= 0.0)
            return 0.0;

        double den = std::sqrt(denLeft * denRight);
        if (den == 0.0)
            return 0.0;

        return num / den;
    }
};

// ===============================
// Analysis pipeline helpers: convert the legacy LapStats data model to the new
// rule-based contract without forcing the UI layer to know anything about the
// simulator-specific channel names.
// ===============================
inline AnalysisSettings buildDefaultAnalysisSettings() {
    AnalysisSettings settings;
    settings.tyres.camberMinSpread = g_thresholds.camberMinSpread;
    settings.tyres.camberMaxSpread = g_thresholds.camberMaxSpread;
    settings.tyres.innerOuterSpreadC = g_thresholds.camberMaxSpread;
    settings.tyres.sideImbalanceMaxC = g_thresholds.tyreCentreTempLrDiffMax;
    settings.tyres.pressureFrontRearDiffPsiMax = g_thresholds.pressureFrontRearDiffMax;
    settings.engine.waterTempWarnC = g_thresholds.waterTempMax;
    settings.engine.oilTempWarnC = g_thresholds.oilTempMax;
    settings.handling.wheelSlipMax = g_thresholds.wheelSlipMax;
    settings.handling.steerMinCornering = g_thresholds.steerMinCornering;
    settings.handling.yawChangeMin = g_thresholds.yawChangeMin;
    settings.handling.yawMinOversteer = g_thresholds.yawMinOversteer;
    settings.handling.steerChangeMin = g_thresholds.steerChangeMin;
    return settings;
}

inline void addSummaryValue(DataProvider& provider, AnalysisAlias alias,
                           const StatSummary& summary) {
    if (summary.count > 0)
        provider.setValue(alias, summary.average());
}

inline DataProvider buildDataProviderFromLap(const LapStats& lap) {
    DataProvider provider;

    addSummaryValue(provider, AnalysisAlias::WaterTemp, lap.waterTemp);
    addSummaryValue(provider, AnalysisAlias::OilTemp, lap.oilTemp);
    addSummaryValue(provider, AnalysisAlias::SteerAngleAvg, lap.steerAngle);
    addSummaryValue(provider, AnalysisAlias::YawRateAvg, lap.yawRate);
    addSummaryValue(provider, AnalysisAlias::WheelSlipFrontAvg, lap.wheelSlipLF);
    addSummaryValue(provider, AnalysisAlias::WheelSlipRearAvg, lap.wheelSlipLR);
    addSummaryValue(provider, AnalysisAlias::TyrePressFL, lap.pressureLF);
    addSummaryValue(provider, AnalysisAlias::TyrePressFR, lap.pressureRF);
    addSummaryValue(provider, AnalysisAlias::TyrePressRL, lap.pressureLR);
    addSummaryValue(provider, AnalysisAlias::TyrePressRR, lap.pressureRR);
    addSummaryValue(provider, AnalysisAlias::TyreTempFLInner, lap.tyreInnerTempLF);
    addSummaryValue(provider, AnalysisAlias::TyreTempFLMiddle, lap.tyreCentreTempLF);
    addSummaryValue(provider, AnalysisAlias::TyreTempFLOuter, lap.tyreOuterTempLF);
    addSummaryValue(provider, AnalysisAlias::TyreTempFRInner, lap.tyreInnerTempRF);
    addSummaryValue(provider, AnalysisAlias::TyreTempFRMiddle, lap.tyreCentreTempRF);
    addSummaryValue(provider, AnalysisAlias::TyreTempFROuter, lap.tyreOuterTempRF);
    addSummaryValue(provider, AnalysisAlias::TyreTempRLInner, lap.tyreInnerTempLR);
    addSummaryValue(provider, AnalysisAlias::TyreTempRLMiddle, lap.tyreCentreTempLR);
    addSummaryValue(provider, AnalysisAlias::TyreTempRLOuter, lap.tyreOuterTempLR);
    addSummaryValue(provider, AnalysisAlias::TyreTempRRInner, lap.tyreInnerTempRR);
    addSummaryValue(provider, AnalysisAlias::TyreTempRRMiddle, lap.tyreCentreTempRR);
    addSummaryValue(provider, AnalysisAlias::TyreTempRROuter, lap.tyreOuterTempRR);

    const double rideHeightToMm = lap.rideHeightUnit == RideHeightUnit::Centimeters ? 10.0 : 1.0;
    const bool hasRearCornerHeights = lap.aeroHeight2.count > 0 && lap.aeroHeight3.count > 0;
    const bool hasAxleHeights = lap.aeroHeight0.count > 0 && lap.aeroHeight1.count > 0;
    if (hasRearCornerHeights) {
        provider.setValue(AnalysisAlias::RideHeightFL, lap.aeroHeight0.average() * rideHeightToMm);
        provider.setValue(AnalysisAlias::RideHeightFR, lap.aeroHeight1.average() * rideHeightToMm);
        provider.setValue(AnalysisAlias::RideHeightRL, lap.aeroHeight2.average() * rideHeightToMm);
        provider.setValue(AnalysisAlias::RideHeightRR, lap.aeroHeight3.average() * rideHeightToMm);
    } else if (hasAxleHeights) {
        provider.setValue(AnalysisAlias::RideHeightFL, lap.aeroHeight0.average() * rideHeightToMm);
        provider.setValue(AnalysisAlias::RideHeightFR, lap.aeroHeight0.average() * rideHeightToMm);
        provider.setValue(AnalysisAlias::RideHeightRL, lap.aeroHeight1.average() * rideHeightToMm);
        provider.setValue(AnalysisAlias::RideHeightRR, lap.aeroHeight1.average() * rideHeightToMm);
    }

    return provider;
}

inline AnalysisRegistry buildDefaultAnalysisRegistry() {
    AnalysisRegistry registry;
    registry.addRule(std::unique_ptr<AnalysisRule>(new EngineTemperatureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new TyreTemperatureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new TyrePressureRule()));
    registry.addRule(std::unique_ptr<AnalysisRule>(new HandlingBalanceRule()));
    return registry;
}

// ===============================
// Damper-velocity issue detection
// (moved from RecommendationEngine.h/.cpp)
// ===============================

namespace detail {

struct DamperFinding {
    bool issue = false;
    std::string text;
};

// Evaluates one corner's damper-velocity histogram against an ideal symmetric bell shape
// (low/high-speed bump vs rebound split at ~50 mm/s, the common sim-racing damper cutoff).
inline DamperFinding evaluateDamperCorner(const DamperCornerStats& stats,
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

// Classify a single zone percentage against its target range.
inline DamperZoneResult classifyDamperZone(double pct, double targetMin, double targetMax) {
    DamperZoneResult r;
    r.pct = pct;
    if (pct >= targetMin && pct <= targetMax) {
        r.severity = DamperZoneSeverity::Normal;
    } else {
        double deviation = (pct > targetMax) ? (pct - targetMax) : (targetMin - pct);
        r.aboveTarget = (pct > targetMax);
        r.severity = (deviation <= g_thresholds.damperZoneModerateBand)
            ? DamperZoneSeverity::Moderate
            : DamperZoneSeverity::Important;
    }
    return r;
}

// Evaluates a damper-velocity histogram against 4-zone targets.
inline DamperZoneReport evaluateDamperZones(const VelocityHistogram& h, ReportLanguage language) {
    auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
        return language == ReportLanguage::French ? fr : en;
    };

    DamperZoneReport report;
    if (h.totalCount < g_thresholds.minDamperSamples)
        return report;

    report.hasData = true;

    // Zone boundaries: ±damperSlowSpeedRange mm/s
    // Sign convention: positive velocity = compression, negative = rebound
    const double B = g_thresholds.damperSlowSpeedRange;
    const double rMax = VelocityHistogram::rangeMax();

    // Compute 4 zone percentages
    double hscPct = h.percentInRange(B, rMax + 1.0);          // v > B (high-speed compression)
    double lscPct = h.percentInRange(0.0, B + 1.0);           // 0 <= v <= B (low-speed compression)
    double lsrPct = h.percentInRange(-B - 1.0, 0.0);          // -B <= v < 0 (low-speed rebound)
    double hsrPct = h.percentInRange(-rMax - 1.0, -B);        // v < -B (high-speed rebound)

    // Classify each zone
    report.hsc = classifyDamperZone(hscPct, g_thresholds.damperHscTargetMin, g_thresholds.damperHscTargetMax);
    report.lsc = classifyDamperZone(lscPct, g_thresholds.damperLscTargetMin, g_thresholds.damperLscTargetMax);
    report.lsr = classifyDamperZone(lsrPct, g_thresholds.damperLsrTargetMin, g_thresholds.damperLsrTargetMax);
    report.hsr = classifyDamperZone(hsrPct, g_thresholds.damperHsrTargetMin, g_thresholds.damperHsrTargetMax);

    // Global balances
    report.compressionPct = hscPct + lscPct;
    report.reboundPct     = lsrPct + hsrPct;
    report.lowSpeedPct    = lscPct + lsrPct;
    report.highSpeedPct   = hscPct + hsrPct;

    // Generate localized text
    if (!report.anyIssue()) {
        report.text = tr(
            "velocity distribution is within target ranges for all 4 zones; no damper change indicated.",
            "la distribution de vitesse est dans les plages cibles pour les 4 zones ; aucun changement d'amortisseur necessaire.");
        return report;
    }

    // Human-readable zone labels (localized)
    struct ZoneInfo { const char* en; const char* fr; DamperZoneResult& r; };
    ZoneInfo zones[4] = {
        { "Hi Comp",  "Comp. Haute",  report.hsc },
        { "Lo Comp",  "Comp. Basse",  report.lsc },
        { "Lo Rebound", "Détente Basse", report.lsr },
        { "Hi Rebound", "Détente Haute", report.hsr }
    };

    // Build description of out-of-target zones + suggested action
    const char* descEn[4] = { "high-speed compression", "low-speed compression",
                              "low-speed rebound", "high-speed rebound" };
    const char* descFr[4] = { "la compression haute vitesse", "la compression basse vitesse",
                              "la détente basse vitesse", "la détente haute vitesse" };
    double tMin[4] = { g_thresholds.damperHscTargetMin, g_thresholds.damperLscTargetMin,
                       g_thresholds.damperLsrTargetMin, g_thresholds.damperHsrTargetMin };
    double tMax[4] = { g_thresholds.damperHscTargetMax, g_thresholds.damperLscTargetMax,
                       g_thresholds.damperLsrTargetMax, g_thresholds.damperHsrTargetMax };

    std::string parts;
    for (int i = 0; i < 4; ++i) {
        if (zones[i].r.severity == DamperZoneSeverity::Normal) continue;
        const char* name = (language == ReportLanguage::French) ? zones[i].fr : zones[i].en;
        const char* desc = (language == ReportLanguage::French) ? descFr[i] : descEn[i];

        std::string pctStr   = std::to_string(static_cast<int>(zones[i].r.pct + 0.5));
        std::string targetStr = std::to_string(static_cast<int>(tMin[i] + 0.5)) + "-" +
                                std::to_string(static_cast<int>(tMax[i] + 0.5));

        const char* action = zones[i].r.aboveTarget
            ? (language == ReportLanguage::French ? "raidir " : "stiffen ")
            : (language == ReportLanguage::French ? "adoucir " : "soften ");

        std::string part = std::string(name) + " " + pctStr + "% (target " + targetStr + "%) — " + action + desc;
        if (!parts.empty()) parts += ", ";
        parts += part;
    }

    report.text = tr(
        ("outside target: " + parts + "."),
        ("hors cible : " + parts + "."));

    return report;
}

} // namespace detail

// True when a corner's damper-velocity distribution indicates an issue
// (any of the 4 zones HSC/LSC/LSR/HSR is Moderate or Important).
inline bool hasDamperIssue(const VelocityHistogram& h) {
    DamperZoneReport report = detail::evaluateDamperZones(h, ReportLanguage::English);
    return report.hasData && report.anyIssue();
}

// Human-readable damper recommendation for one corner (localized).
// cornerIndex: 0=LF, 1=RF, 2=LR, 3=RR. Empty string if not enough samples.
inline std::string getDamperRecommendation(const VelocityHistogram& h, int cornerIndex, ReportLanguage language) {
    static const char* enLabels[4] = { "FL", "FR", "RL", "RR" };
    static const char* frLabels[4] = { "FL", "FR", "RL", "RR" }; // same codes as the graph, both languages
    if (cornerIndex < 0 || cornerIndex > 3) return std::string();
    DamperZoneReport report = detail::evaluateDamperZones(h, language);
    if (!report.hasData) return std::string();
    const char* label = (language == ReportLanguage::French) ? frLabels[cornerIndex] : enLabels[cornerIndex];
    return std::string(label) + " — " + report.text;
}

// Pick the best velocity histogram for a corner: prefer the direct
// suspension-velocity channel when it has data, otherwise fall back to
// the travel-derivative histogram. Mirrors the display logic in MotecSetupForm.
inline const VelocityHistogram& pickDamperHistogram(const LapStats& lap, int cornerIndex) {
    switch (cornerIndex) {
        case 0: return lap.suspVelLF.totalCount > 0 ? lap.suspVelLF : lap.damperLF.histogram;
        case 1: return lap.suspVelRF.totalCount > 0 ? lap.suspVelRF : lap.damperRF.histogram;
        case 2: return lap.suspVelLR.totalCount > 0 ? lap.suspVelLR : lap.damperLR.histogram;
        case 3: return lap.suspVelRR.totalCount > 0 ? lap.suspVelRR : lap.damperRR.histogram;
        default: return lap.damperLF.histogram;
    }
}

// ===============================
// External functions
// ===============================
LapStats buildAverageLapData(const std::vector<LapStats>& laps);

ReportResult buildAverageLapReport(const std::vector<LapStats>& laps,
                                  ReportLanguage language = ReportLanguage::English,
                                  ChannelProfile profile = ChannelProfile::EvoM1);

std::vector<AnalysisResult> evaluateLapWithRules(const LapStats& lap,
                                                const AnalysisSettings& settings = AnalysisSettings());

typedef void (*AnalysisProgressCallback)(int percent);
std::string resolveChannelKey(const std::string& rawName,
                             ChannelProfile profile = ChannelProfile::EvoM1);

bool parseMotecCsv(const std::string& path, std::vector<LapStats>& laps, std::string& errorMessage,
                   AnalysisProgressCallback progressCallback = NULL,
                   ChannelProfile profile = ChannelProfile::EvoM1);

bool parseMotecLd(const std::string& path,
                  std::vector<LapStats>& laps,
                  std::string& errorMessage,
                  AnalysisProgressCallback progressCallback = NULL,
                  ChannelProfile profile = ChannelProfile::EvoM1);

// Returns the averaged LapStats object (all but last lap).
// Requires at least 2 laps to be valid (returns empty if < 2).
LapStats buildAverageLapData(const std::vector<LapStats>& laps);

