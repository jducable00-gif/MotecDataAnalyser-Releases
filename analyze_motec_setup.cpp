//---------------------------------------------------------------------------
// analyze_motec_setup.cpp — Cœur de l'analyse de la télémétrie MoTeC.
//
// Ce fichier contient :
//   - Normalisation des noms de canaux et résolution des alias
//     (normalizeChannelName / resolveChannelKey) selon le profil (Evo / AMS2).
//   - Conversion d'unités et routage des canaux (dispatchForChannel /
//     conversionForChannel).
//   - Évaluation des pneus, butées, comportement (sous/dévissage), stabilité,
//     freinage, traction, etc. à partir des seuils g_thresholds.
//   - Génération des rapports (buildLapReport / buildBestThreeLapReport)
//     en anglais ou français.
//   - Analyse des fichiers CSV (parseMotecCsv) et LD/LDX (parseMotecLd).
//
// NB : les chemins CSV et LD sont volontairement séparés (voir AGENTS.md).
//---------------------------------------------------------------------------

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <codecvt>
#include <locale>

#include "MotecSetupAnalyzer.h"
#include "MotecToCSV.h"
#include "RuleConfig.h"
#include "LapReportGenerator.h"
#include <IniFiles.hpp>

// Global thresholds instance (loaded from MotecDataAnalyser.thresholds.ini at startup)
Thresholds g_thresholds;



// ===============================
// Symptom weight INI save/load
// Format conservé : section [SymptomWeights], clé "symptôme|cause", valeur 0..1.
// Les règles sont dynamiques (JSON) — on itère la liste courante de RuleConfig.
// ===============================
void saveSymptomWeights(TIniFile* ini) {
    const std::vector<RuleSymptom>& rules = RuleConfig::instance().rules();
    for (std::size_t r = 0; r < rules.size(); ++r) {
        const RuleSymptom& rule = rules[r];
        for (std::size_t i = 0; i < rule.causes.size(); ++i) {
            std::string key = rule.id + "|" + rule.causes[i].cause;
            ini->WriteFloat("SymptomWeights", UTF8String(key.c_str()), rule.causes[i].weight);
        }
    }
}

void loadSymptomWeights(TIniFile* ini) {
    std::vector<RuleSymptom>& rules = RuleConfig::instance().rulesRef();
    for (std::size_t r = 0; r < rules.size(); ++r) {
        RuleSymptom& rule = rules[r];
        for (std::size_t i = 0; i < rule.causes.size(); ++i) {
            std::string key = rule.id + "|" + rule.causes[i].cause;
            double val = ini->ReadFloat("SymptomWeights", UTF8String(key.c_str()), rule.causes[i].weight);
            if (val >= 0.0 && val <= 1.0)
                rule.causes[i].weight = val;
        }
    }
}


// ===============================
// Résolution des canaux (normalisation des noms + alias par profil)
// ===============================
static std::string normalizeChannelName(std::string name) {
    while (!name.empty() && (name.back() == '\0' || name.back() == ' ' || name.back() == '\t' || name.back() == '\r' || name.back() == '\n'))
        name.pop_back();
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '\r' || name.front() == '\n'))
        name.erase(name.begin());

    std::string normalized;
    normalized.reserve(name.size());
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            normalized.push_back((char)std::toupper((unsigned char)c));
        else if (c == '_' || c == '-' || c == ' ' || c == '.' || c == '/' || c == '\\')
            normalized.push_back('_');
    }
    while (!normalized.empty() && normalized.back() == '_')
        normalized.pop_back();
    while (!normalized.empty() && normalized.front() == '_')
        normalized.erase(normalized.begin());
    return normalized;
}

std::string resolveChannelKey(const std::string& rawName,
                             ChannelProfile profile)
{
    std::string key = normalizeChannelName(rawName);
    if (key.empty())
        return key;

    // Les alias de canaux ne sont plus des tables statiques : ils vivent dans
    // RuleConfig (valeurs par défaut intégrées + fusion optionnelle depuis le JSON).
    // Ordre identique à l'ancien code : generic d'abord, puis profil EvoM1.
    std::string resolved;
    if (RuleConfig::instance().resolveAlias(profile, key, resolved))
        return resolved;


    return key;
}

static std::string normalizeUnitName(const std::string& unit) {
    std::string u = unit;
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    u.erase(std::remove_if(u.begin(), u.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0' || c == '%' || c == static_cast<unsigned char>(176);
    }), u.end());
    return u;
}

enum ChannelDispatch {
    CH_NONE = 0,
    CH_STEERANGLE,
    CH_ROTY,
    CH_LAT_G,
    CH_LONG_G,
    CH_BRAKE,
    CH_THROTTLE,
    CH_SUS_TRAVEL_LF,
    CH_SUS_TRAVEL_RF,
    CH_SUS_TRAVEL_LR,
    CH_SUS_TRAVEL_RR,
    CH_SPEED,
    CH_RPMS,
    CH_WHEEL_SPEED_LF,
    CH_WHEEL_SPEED_RF,
    CH_WHEEL_SPEED_LR,
    CH_WHEEL_SPEED_RR,
    CH_BRAKE_TEMP_LF,
    CH_BRAKE_TEMP_RF,
    CH_BRAKE_TEMP_LR,
    CH_BRAKE_TEMP_RR,
    CH_TYRE_PRESS_LF,
    CH_TYRE_PRESS_RF,
    CH_TYRE_PRESS_LR,
    CH_TYRE_PRESS_RR,
    CH_BUMPSTOPUP_TRAVEL_LF,
    CH_BUMPSTOPUP_TRAVEL_RF,
    CH_BUMPSTOPUP_TRAVEL_LR,
    CH_BUMPSTOPUP_TRAVEL_RR,
    CH_AERO_HEIGHT0,
    CH_AERO_HEIGHT1,
    CH_AERO_HEIGHT2,
    CH_AERO_HEIGHT3,
    CH_AERO_BALANCE,
    CH_LSD_LOCKING_TORQUE,
    CH_C_SURF_TEMP_LF,
    CH_C_SURF_TEMP_RF,
    CH_C_SURF_TEMP_LR,
    CH_C_SURF_TEMP_RR,
    CH_TYRE_INNER_TEMP_LF,
    CH_TYRE_CENTRE_TEMP_LF,
    CH_TYRE_OUTER_TEMP_LF,
    CH_TYRE_INNER_TEMP_RF,
    CH_TYRE_CENTRE_TEMP_RF,
    CH_TYRE_OUTER_TEMP_RF,
    CH_TYRE_INNER_TEMP_LR,
    CH_TYRE_CENTRE_TEMP_LR,
    CH_TYRE_OUTER_TEMP_LR,
    CH_TYRE_INNER_TEMP_RR,
    CH_TYRE_CENTRE_TEMP_RR,
    CH_TYRE_OUTER_TEMP_RR,
    CH_ENGINE_OIL_TEMP,
    CH_ENGINE_WATER_TEMP,
    CH_FUEL_LEVEL,
    CH_FUEL_PRES,
    CH_TRACK_TEMP,
    CH_AMBIENT_TEMP,
    CH_BRAKE_TORQUE_LF,
    CH_BRAKE_TORQUE_RF,
    CH_BRAKE_TORQUE_LR,
    CH_BRAKE_TORQUE_RR,
    CH_WHEEL_SR_LF,
    CH_WHEEL_SR_RF,
    CH_WHEEL_SR_LR,
    CH_WHEEL_SR_RR,
    CH_TYRE_TAIR_LF,
    CH_TYRE_TAIR_RF,
    CH_TYRE_TAIR_LR,
    CH_TYRE_TAIR_RR,
    CH_SUSP_VEL_LF,
    CH_SUSP_VEL_RF,
    CH_SUSP_VEL_LR,
    CH_SUSP_VEL_RR,
    CH_BRAKE_BIAS_SETTING,
    CH_TCS_ACTIVE,
    CH_ABS_ACTIVE,
    CH_LAP_BEACON
};

static ChannelDispatch dispatchForChannel(const std::string& key, ChannelProfile profile) {
    std::string lookupKey = key;
    std::string resolvedAlias;
    if (RuleConfig::instance().resolveRawToAlias(profile, MappingSource::MotecFile, key, resolvedAlias)) {
        lookupKey = resolvedAlias;
    }

    if (lookupKey == "ROTY") return CH_ROTY;
    if (lookupKey == "LAT_G") return CH_LAT_G;
    if (lookupKey == "LONG_G") return CH_LONG_G;
    if (lookupKey == "BRAKE") return CH_BRAKE;
    if (lookupKey == "THROTTLE") return CH_THROTTLE;
    if (lookupKey == "SUS_TRAVEL_LF") return CH_SUS_TRAVEL_LF;
    if (lookupKey == "SUS_TRAVEL_RF") return CH_SUS_TRAVEL_RF;
    if (lookupKey == "SUS_TRAVEL_LR") return CH_SUS_TRAVEL_LR;
    if (lookupKey == "SUS_TRAVEL_RR") return CH_SUS_TRAVEL_RR;
    if (lookupKey == "SPEED") return CH_SPEED;
    if (lookupKey == "RPMS") return CH_RPMS;
    if (lookupKey == "WHEEL_SPEED_LF") return CH_WHEEL_SPEED_LF;
    if (lookupKey == "WHEEL_SPEED_RF") return CH_WHEEL_SPEED_RF;
    if (lookupKey == "WHEEL_SPEED_LR") return CH_WHEEL_SPEED_LR;
    if (lookupKey == "WHEEL_SPEED_RR") return CH_WHEEL_SPEED_RR;
    if (lookupKey == "BRAKE_TEMP_LF") return CH_BRAKE_TEMP_LF;
    if (lookupKey == "BRAKE_TEMP_RF") return CH_BRAKE_TEMP_RF;
    if (lookupKey == "BRAKE_TEMP_LR") return CH_BRAKE_TEMP_LR;
    if (lookupKey == "BRAKE_TEMP_RR") return CH_BRAKE_TEMP_RR;
    if (lookupKey == "TYRE_PRESS_LF") return CH_TYRE_PRESS_LF;
    if (lookupKey == "TYRE_PRESS_RF") return CH_TYRE_PRESS_RF;
    if (lookupKey == "TYRE_PRESS_LR") return CH_TYRE_PRESS_LR;
    if (lookupKey == "TYRE_PRESS_RR") return CH_TYRE_PRESS_RR;
    if (lookupKey == "BUMPSTOPUP_TRAVEL_LF") return CH_BUMPSTOPUP_TRAVEL_LF;
    if (lookupKey == "BUMPSTOPUP_TRAVEL_RF") return CH_BUMPSTOPUP_TRAVEL_RF;
    if (lookupKey == "BUMPSTOPUP_TRAVEL_LR") return CH_BUMPSTOPUP_TRAVEL_LR;
    if (lookupKey == "BUMPSTOPUP_TRAVEL_RR") return CH_BUMPSTOPUP_TRAVEL_RR;
    if (lookupKey == "AERO_HEIGHT0") return CH_AERO_HEIGHT0;
    if (lookupKey == "AERO_HEIGHT1") return CH_AERO_HEIGHT1;
    if (lookupKey == "AERO_HEIGHT2") return CH_AERO_HEIGHT2;
    if (lookupKey == "AERO_HEIGHT3") return CH_AERO_HEIGHT3;
    if (lookupKey == "AERO_BALANCE") return CH_AERO_BALANCE;
    if (lookupKey == "LSD_LOCKING_TORQUE") return CH_LSD_LOCKING_TORQUE;
    if (lookupKey == "C_SURF_TEMP_LF") return CH_C_SURF_TEMP_LF;
    if (lookupKey == "C_SURF_TEMP_RF") return CH_C_SURF_TEMP_RF;
    if (lookupKey == "C_SURF_TEMP_LR") return CH_C_SURF_TEMP_LR;
    if (lookupKey == "C_SURF_TEMP_RR") return CH_C_SURF_TEMP_RR;
    if (lookupKey == "TYRE_INNER_TEMP_LF") return CH_TYRE_INNER_TEMP_LF;
    if (lookupKey == "TYRE_CENTRE_TEMP_LF") return CH_TYRE_CENTRE_TEMP_LF;
    if (lookupKey == "TYRE_OUTER_TEMP_LF") return CH_TYRE_OUTER_TEMP_LF;
    if (lookupKey == "TYRE_INNER_TEMP_RF") return CH_TYRE_INNER_TEMP_RF;
    if (lookupKey == "TYRE_CENTRE_TEMP_RF") return CH_TYRE_CENTRE_TEMP_RF;
    if (lookupKey == "TYRE_OUTER_TEMP_RF") return CH_TYRE_OUTER_TEMP_RF;
    if (lookupKey == "TYRE_INNER_TEMP_LR") return CH_TYRE_INNER_TEMP_LR;
    if (lookupKey == "TYRE_CENTRE_TEMP_LR") return CH_TYRE_CENTRE_TEMP_LR;
    if (lookupKey == "TYRE_OUTER_TEMP_LR") return CH_TYRE_OUTER_TEMP_LR;
    if (lookupKey == "TYRE_INNER_TEMP_RR") return CH_TYRE_INNER_TEMP_RR;
    if (lookupKey == "TYRE_CENTRE_TEMP_RR") return CH_TYRE_CENTRE_TEMP_RR;
	if (lookupKey == "TYRE_OUTER_TEMP_RR") return CH_TYRE_OUTER_TEMP_RR;
	if (lookupKey == "ENGINE_OIL_TEMP") return CH_ENGINE_OIL_TEMP;
    if (lookupKey == "ENGINE_WATER_TEMP") return CH_ENGINE_WATER_TEMP;
    if (lookupKey == "FUEL_LEVEL") return CH_FUEL_LEVEL;
    if (lookupKey == "FUEL_PRES") return CH_FUEL_PRES;
    if (lookupKey == "TRACK_TEMP") return CH_TRACK_TEMP;
    if (lookupKey == "AMBIENT_TEMP") return CH_AMBIENT_TEMP;
    if (lookupKey == "BRAKE_TORQUE_LF") return CH_BRAKE_TORQUE_LF;
    if (lookupKey == "BRAKE_TORQUE_RF") return CH_BRAKE_TORQUE_RF;
    if (lookupKey == "BRAKE_TORQUE_LR") return CH_BRAKE_TORQUE_LR;
    if (lookupKey == "BRAKE_TORQUE_RR") return CH_BRAKE_TORQUE_RR;
    if (lookupKey == "WHEEL_SR_LF") return CH_WHEEL_SR_LF;
    if (lookupKey == "WHEEL_SR_RF") return CH_WHEEL_SR_RF;
    if (lookupKey == "WHEEL_SR_LR") return CH_WHEEL_SR_LR;
    if (lookupKey == "WHEEL_SR_RR") return CH_WHEEL_SR_RR;
    if (lookupKey == "TYRE_TAIR_LF") return CH_TYRE_TAIR_LF;
    if (lookupKey == "TYRE_TAIR_RF") return CH_TYRE_TAIR_RF;
    if (lookupKey == "TYRE_TAIR_LR") return CH_TYRE_TAIR_LR;
    if (lookupKey == "TYRE_TAIR_RR") return CH_TYRE_TAIR_RR;
    if (lookupKey == "SUSP_VEL_LF") return CH_SUSP_VEL_LF;
    if (lookupKey == "SUSP_VEL_RF") return CH_SUSP_VEL_RF;
    if (lookupKey == "SUSP_VEL_LR") return CH_SUSP_VEL_LR;
    if (lookupKey == "SUSP_VEL_RR") return CH_SUSP_VEL_RR;
    if (lookupKey == "BRAKE_BIAS_SETTING") return CH_BRAKE_BIAS_SETTING;
    if (lookupKey == "TCS_ACTIVE") return CH_TCS_ACTIVE;
    if (lookupKey == "ABS_ACTIVE") return CH_ABS_ACTIVE;
    if (lookupKey == "LAP_BEACON") return CH_LAP_BEACON;
    return CH_NONE;
}

static void conversionForChannel(const std::string& key, const std::string& unit,
                                 double& factor, double& offset) {
    factor = 1.0;
    offset = 0.0;
    std::string u = normalizeUnitName(unit);

    if (key == "SPEED" || key.find("SPEED") != std::string::npos) {
        if (u == "MPS" || u == "M/S") factor = 3.6;
        return;
    }

    if (key == "ROTY" || key.find("ROTY") != std::string::npos) {
        if (u == "RAD/S" || u == "RADS") factor = 57.295779513;
        return;
    }

    if (key == "LAT_G" || key == "LONG_G" ||
        key == "G_LAT" || key == "G_LON" ||
        key == "LATERAL_G" || key == "LONGITUDINAL_G") {
        if (u == "M/S2" || u == "MPS2") factor = 1.0 / 9.80665;
        return;
    }

    if (key.find("WHEEL_SPEED") != std::string::npos ||
        key.find("WHEELSPEED") != std::string::npos) {
        if (u == "MPS" || u == "M/S") factor = 3.6;
        return;
    }

    if (key.find("SUSP_VEL") != std::string::npos) {
        if (u == "M/S" || u == "MPS") factor = 1000.0;
        return;
    }

    if (key.find("PRESS") != std::string::npos || key == "FUEL_PRES") {
        if (u == "KPA") factor = 1.0 / 6.894757;
        else if (u == "BAR") factor = 14.503774;
        else if (u == "PSI") factor = 1.0;
        return;
    }

    if (key.find("TEMP") != std::string::npos ||
        key == "TRACK_TEMP" || key == "AMBIENT_TEMP" ||
        key == "ENGINE_OIL_TEMP" || key == "ENGINE_WATER_TEMP") {
        if (u == "F" || u == "FAHRENHEIT") {
            factor = 5.0 / 9.0;
            offset = -32.0 * 5.0 / 9.0;
        }
        return;
    }
}

static double toDouble(const std::string& token, double fallback = 0.0) {
	if (token.empty()) {
		return fallback;
	}
    const char* start = token.c_str();
    char* end = NULL;
    double value = std::strtod(start, &end);
    if (end == start)
        return fallback;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
    return *end == '\0' ? value : fallback;
}



ReportResult buildBestThreeLapReport(const std::vector<LapStats>& laps, bool fullReport,
                                   ReportLanguage language, ChannelProfile profile) {
    return LapReportGenerator::generateBestThreeLaps(laps, fullReport, language, profile);
}

// Returns the averaged data for all but the last lap. 
// Requires at least 2 laps to be valid (returns empty if < 2).
LapStats buildAverageLapData(const std::vector<LapStats>& laps) {
    return LapReportGenerator::getAverageLapData(laps);
}

ReportResult buildAverageLapReport(const std::vector<LapStats>& laps, bool fullReport, 
                                  ReportLanguage language, ChannelProfile profile) {
    return LapReportGenerator::generateAverageLaps(laps, fullReport, language, profile);
}

ReportResult buildLapReport(const LapStats& lap, bool fullReport, ReportLanguage language,
                           ChannelProfile profile) {
    return LapReportGenerator::generate(lap, fullReport, language, profile);
}

std::vector<AnalysisResult> evaluateLapWithRules(const LapStats& lap,
                                                const AnalysisSettings& settings) {
    DataProvider provider = buildDataProviderFromLap(lap);
    provider.setChannelMappings(RuleConfig::instance().channelMappings());
    const AnalysisRegistry registry = buildDefaultAnalysisRegistry();
    return registry.evaluateAll(provider, settings);
}

std::vector<AnalysisResult> evaluateLapsWithRules(const std::vector<LapStats>& laps,
                                                 const AnalysisSettings& settings) {
    std::vector<AnalysisResult> allResults;
    for (std::size_t i = 0; i < laps.size(); ++i) {
        const std::vector<AnalysisResult> lapResults = evaluateLapWithRules(laps[i], settings);
        allResults.insert(allResults.end(), lapResults.begin(), lapResults.end());
    }
    return allResults;
}

std::vector<std::string> splitCsvRow(const std::string& row)
{
    std::vector<std::string> result;
    std::string cur;
    bool inQuotes = false;

    for (char c : row) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            result.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    result.push_back(cur);
    return result;
}

double toDoubleSafe(const std::string& s)
{
    if (s.empty()) return 0.0;

    const char* start = s.c_str();
    char* end = nullptr;
    double value = std::strtod(start, &end);
    if (end == start)
        return 0.0;

    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;

    return (*end == '\0') ? value : 0.0;
}

bool fieldExists(const std::unordered_map<std::string, size_t>& headerIndex,
                 const std::string& key)
{
    return headerIndex.find(key) != headerIndex.end();
}

int fieldIndex(const std::unordered_map<std::string, size_t>& headerIndex,
               const std::string& key)
{
    auto it = headerIndex.find(key);
    if (it == headerIndex.end()) return -1;
    return static_cast<int>(it->second);
}
// ===============================
// Analyse des fichiers LD/LDX (décodage binaire puis agrégation par tour)
// ===============================
bool parseMotecLd(const std::string& path,
                  std::vector<LapStats>& laps,
                  std::string& errorMessage,
                  AnalysisProgressCallback progressCallback,
                  ChannelProfile profile)
{
    laps.clear();
    std::vector<MotecColumnInfo> columns;
    std::vector<double> beaconMarkers;
    std::string decodeError;
    if (!DecodeMotecLd(path, columns, beaconMarkers, decodeError, progressCallback)) {
        errorMessage = decodeError;
        return false;
    }

    int masterFrequency = 1;
    double duration = 0.0;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].frequency > 0) {
            masterFrequency = std::max(masterFrequency, columns[i].frequency);
            duration = std::max(duration, (double)columns[i].values.size() / columns[i].frequency);
        }
    }
    if (masterFrequency <= 0 || duration <= 0.0) {
        errorMessage = "No usable sample timing information was found in the LD/LDX file.";
        return false;
    }

    std::size_t sampleCount = static_cast<std::size_t>(duration * masterFrequency);
    size_t nextBeaconMarker = 0;
    double previousTime = 0.0;
    double previousBeacon = -1.0;
    int lapCounter = 1;
    LapStats currentLap;
    currentLap.lapNumber = lapCounter;
    currentLap.rideHeightUnit = profile == ChannelProfile::AMS2
        ? RideHeightUnit::Centimeters : RideHeightUnit::Millimeters;

    // Precompute per-column dispatch and unit conversion (constant per column)
    std::vector<ChannelDispatch> colDispatch(columns.size(), CH_NONE);
    std::vector<double> colFactor(columns.size(), 1.0);
    std::vector<double> colOffset(columns.size(), 0.0);
    for (size_t i = 0; i < columns.size(); ++i) {
        const std::string key = resolveChannelKey(columns[i].name, profile);
        colDispatch[i] = dispatchForChannel(key, profile);
        conversionForChannel(key, columns[i].unit, colFactor[i], colOffset[i]);
    }

    // Integer resampling cursors: advance each column's sample index with exact
    // integer arithmetic (Bresenham) instead of a float multiply per cell.
    std::vector<std::size_t> colCursor(columns.size(), 0);   // current sample index per column
    std::vector<int> colAccumulator(columns.size(), 0);      // Bresenham accumulator (master-Freq units)

    for (std::size_t row = 0; row < sampleCount; ++row) {
        double timeValue = static_cast<double>(row) / static_cast<double>(masterFrequency);
        double beacon = 0.0;
        bool newLap = false;

        if (progressCallback && sampleCount > 0 && row % std::max<size_t>(1, sampleCount / 100) == 0) {
            int percent = static_cast<int>((double)(row + 1) / (double)sampleCount * 100.0);
            if (percent > 0 && percent <= 100)
                progressCallback(percent);
        }

        if (nextBeaconMarker < beaconMarkers.size() && timeValue >= beaconMarkers[nextBeaconMarker]) {
            newLap = true;
            ++nextBeaconMarker;
        }

        if (newLap && currentLap.rowCount > 0) {
            currentLap.endTime = previousTime;
            laps.push_back(currentLap);
            ++lapCounter;
            currentLap = LapStats();
            currentLap.lapNumber = lapCounter;
            currentLap.rideHeightUnit = profile == ChannelProfile::AMS2
                ? RideHeightUnit::Centimeters : RideHeightUnit::Millimeters;
        }

        if (currentLap.rowCount == 0)
            currentLap.startTime = timeValue;

        currentLap.endTime = timeValue;
        currentLap.rowCount++;

        double sampleSteer = std::numeric_limits<double>::quiet_NaN();
        double sampleYaw = std::numeric_limits<double>::quiet_NaN();
        double sampleLatG = std::numeric_limits<double>::quiet_NaN();
        double sampleLongG = std::numeric_limits<double>::quiet_NaN();
        double sampleBrake = 0.0;
        double sampleThrottle = 0.0;
        double suspensionFrontLeft = std::numeric_limits<double>::quiet_NaN();
        double suspensionFrontRight = std::numeric_limits<double>::quiet_NaN();
        double suspensionRearLeft = std::numeric_limits<double>::quiet_NaN();
        double suspensionRearRight = std::numeric_limits<double>::quiet_NaN();

        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& values = columns[i].values;
            if (values.empty())
                continue;

            // Advance integer resampling cursor (exact nearest-sample stepping,
            // no floating point). Runs at most once per row since each column's
            // frequency is <= masterFrequency.
            colAccumulator[i] += columns[i].frequency;
            while (colAccumulator[i] >= masterFrequency) {
                colAccumulator[i] -= masterFrequency;
                if (colCursor[i] + 1 < values.size()) ++colCursor[i];
            }
            std::size_t sample = colCursor[i];
            if (sample >= values.size())   // tail rows beyond this column's data
                continue;

            double value = values[sample] * colFactor[i] + colOffset[i];

            switch (colDispatch[i]) {
                case CH_STEERANGLE: sampleSteer = value; currentLap.steerAngle.add(value); break;
                case CH_ROTY: sampleYaw = value; currentLap.yawRate.add(value); break;
                case CH_LAT_G: sampleLatG = value; currentLap.latG.add(value); break;
                case CH_LONG_G: sampleLongG = value; currentLap.longG.add(value); break;
                case CH_BRAKE: sampleBrake = value; currentLap.brakePedal.add(value); break;
                case CH_THROTTLE: sampleThrottle = value; currentLap.throttle.add(value); break;
                case CH_SUS_TRAVEL_LF: suspensionFrontLeft = value; currentLap.susTravelLF.add(value); currentLap.damperLF.addSample(value, timeValue); break;
                case CH_SUS_TRAVEL_RF: suspensionFrontRight = value; currentLap.susTravelRF.add(value); currentLap.damperRF.addSample(value, timeValue); break;
                case CH_SUS_TRAVEL_LR: suspensionRearLeft = value; currentLap.susTravelLR.add(value); currentLap.damperLR.addSample(value, timeValue); break;
                case CH_SUS_TRAVEL_RR: suspensionRearRight = value; currentLap.susTravelRR.add(value); currentLap.damperRR.addSample(value, timeValue); break;
                case CH_SPEED: currentLap.speed.add(value); break;
                case CH_RPMS: currentLap.rpm.add(value); break;
                case CH_WHEEL_SPEED_LF: currentLap.wheelSpeedLF.add(value); break;
                case CH_WHEEL_SPEED_RF: currentLap.wheelSpeedRF.add(value); break;
                case CH_WHEEL_SPEED_LR: currentLap.wheelSpeedLR.add(value); break;
                case CH_WHEEL_SPEED_RR: currentLap.wheelSpeedRR.add(value); break;
                case CH_BRAKE_TEMP_LF: currentLap.brakeTempLF.add(value); break;
                case CH_BRAKE_TEMP_RF: currentLap.brakeTempRF.add(value); break;
                case CH_BRAKE_TEMP_LR: currentLap.brakeTempLR.add(value); break;
                case CH_BRAKE_TEMP_RR: currentLap.brakeTempRR.add(value); break;
                case CH_TYRE_PRESS_LF: currentLap.pressureLF.add(value); break;
                case CH_TYRE_PRESS_RF: currentLap.pressureRF.add(value); break;
                case CH_TYRE_PRESS_LR: currentLap.pressureLR.add(value); break;
                case CH_TYRE_PRESS_RR: currentLap.pressureRR.add(value); break;
                case CH_BUMPSTOPUP_TRAVEL_LF: currentLap.bumpStopLF.add(value); break;
                case CH_BUMPSTOPUP_TRAVEL_RF: currentLap.bumpStopRF.add(value); break;
                case CH_BUMPSTOPUP_TRAVEL_LR: currentLap.bumpStopLR.add(value); break;
                case CH_BUMPSTOPUP_TRAVEL_RR: currentLap.bumpStopRR.add(value); break;
                case CH_AERO_HEIGHT0: currentLap.aeroHeight0.add(value); break;
                case CH_AERO_HEIGHT1: currentLap.aeroHeight1.add(value); break;
                case CH_AERO_HEIGHT2: currentLap.aeroHeight2.add(value); break;
                case CH_AERO_HEIGHT3: currentLap.aeroHeight3.add(value); break;
                case CH_AERO_BALANCE: currentLap.aeroBalance.add(value); break;
                case CH_LSD_LOCKING_TORQUE: currentLap.lsdTorque.add(value); break;
                case CH_C_SURF_TEMP_LF: currentLap.surfTempCFL.add(value); break;
                case CH_C_SURF_TEMP_RF: currentLap.surfTempCRF.add(value); break;
                case CH_C_SURF_TEMP_LR: currentLap.surfTempCLR.add(value); break;
                case CH_C_SURF_TEMP_RR: currentLap.surfTempCRR.add(value); break;
                case CH_TYRE_INNER_TEMP_LF: currentLap.tyreInnerTempLF.add(value); break;
                case CH_TYRE_CENTRE_TEMP_LF: currentLap.tyreCentreTempLF.add(value); break;
                case CH_TYRE_OUTER_TEMP_LF: currentLap.tyreOuterTempLF.add(value); break;
                case CH_TYRE_INNER_TEMP_RF: currentLap.tyreInnerTempRF.add(value); break;
                case CH_TYRE_CENTRE_TEMP_RF: currentLap.tyreCentreTempRF.add(value); break;
                case CH_TYRE_OUTER_TEMP_RF: currentLap.tyreOuterTempRF.add(value); break;
                case CH_TYRE_INNER_TEMP_LR: currentLap.tyreInnerTempLR.add(value); break;
                case CH_TYRE_CENTRE_TEMP_LR: currentLap.tyreCentreTempLR.add(value); break;
                case CH_TYRE_OUTER_TEMP_LR: currentLap.tyreOuterTempLR.add(value); break;
                case CH_TYRE_INNER_TEMP_RR: currentLap.tyreInnerTempRR.add(value); break;
                case CH_TYRE_CENTRE_TEMP_RR: currentLap.tyreCentreTempRR.add(value); break;
                case CH_TYRE_OUTER_TEMP_RR: currentLap.tyreOuterTempRR.add(value); break;
				case CH_ENGINE_OIL_TEMP: currentLap.oilTemp.add(value); break;
                case CH_ENGINE_WATER_TEMP: currentLap.waterTemp.add(value); break;
                case CH_FUEL_LEVEL: currentLap.fuelLevel.add(value); break;
                case CH_FUEL_PRES: currentLap.fuelPressure.add(value); break;
                case CH_TRACK_TEMP: currentLap.trackTemp.add(value); break;
                case CH_AMBIENT_TEMP: currentLap.ambientTemp.add(value); break;
                case CH_BRAKE_TORQUE_LF: currentLap.brakeTorqueLF.add(value); break;
                case CH_BRAKE_TORQUE_RF: currentLap.brakeTorqueRF.add(value); break;
                case CH_BRAKE_TORQUE_LR: currentLap.brakeTorqueLR.add(value); break;
                case CH_BRAKE_TORQUE_RR: currentLap.brakeTorqueRR.add(value); break;
                case CH_WHEEL_SR_LF: currentLap.wheelSlipLF.add(value); break;
                case CH_WHEEL_SR_RF: currentLap.wheelSlipRF.add(value); break;
                case CH_WHEEL_SR_LR: currentLap.wheelSlipLR.add(value); break;
                case CH_WHEEL_SR_RR: currentLap.wheelSlipRR.add(value); break;
                case CH_TYRE_TAIR_LF: currentLap.tyreAirTempLF.add(value); break;
                case CH_TYRE_TAIR_RF: currentLap.tyreAirTempRF.add(value); break;
                case CH_TYRE_TAIR_LR: currentLap.tyreAirTempLR.add(value); break;
                case CH_TYRE_TAIR_RR: currentLap.tyreAirTempRR.add(value); break;
                case CH_SUSP_VEL_LF: currentLap.suspVelLF.add(value); break;
                case CH_SUSP_VEL_RF: currentLap.suspVelRF.add(value); break;
                case CH_SUSP_VEL_LR: currentLap.suspVelLR.add(value); break;
                case CH_SUSP_VEL_RR: currentLap.suspVelRR.add(value); break;
                case CH_BRAKE_BIAS_SETTING: currentLap.brakeBiasSetting.add(value); break;
                case CH_TCS_ACTIVE: currentLap.tcsActive.add(value); break;
                case CH_ABS_ACTIVE: currentLap.absActive.add(value); break;
                case CH_LAP_BEACON: beacon = value; break;
                case CH_NONE:
                default: break;
            }
        }

        double suspensionAsymmetry = 0.0;
        if (std::isfinite(suspensionFrontLeft) && std::isfinite(suspensionFrontRight) &&
            std::isfinite(suspensionRearLeft) && std::isfinite(suspensionRearRight)) {
            suspensionAsymmetry = std::max(
                std::abs(suspensionFrontLeft - suspensionFrontRight),
                std::abs(suspensionRearLeft - suspensionRearRight));
        }
        currentLap.addHandlingSample(sampleSteer, sampleYaw, sampleLatG, sampleLongG,
                                     sampleBrake, sampleThrottle, suspensionAsymmetry);

        previousTime = timeValue;
        previousBeacon = beacon;
    }

    if (currentLap.rowCount > 0) {
        currentLap.endTime = previousTime;
        laps.push_back(currentLap);
    }

    if (laps.empty()) {
        errorMessage = "No valid lap data was found in the LD/LDX file.";
        return false;
    }

    // Session metadata was already extracted from the header during decode
    // (no second full-file read). Take it from the first non-empty column.
    for (const MotecColumnInfo& col : columns) {
        if (!col.values.empty()) {
            for (LapStats& lap : laps) {
                lap.driverName  = col.driverName.empty()  ? "Unknown driver"  : col.driverName;
                lap.vehicleName = col.vehicleName.empty() ? "Unknown vehicle" : col.vehicleName;
                lap.trackName   = col.trackName.empty()   ? "Unknown circuit" : col.trackName;
            }
            break;
        }
    }

    return true;
}

// ===============================
// Analyse des fichiers CSV (lecture des lignes + agrégation par tour)
// ===============================
bool parseMotecCsv(const std::string& path,
                   std::vector<LapStats>& laps,
                   std::string& errorMessage,
                   AnalysisProgressCallback progressCallback,
                   ChannelProfile profile)
{
    (void)progressCallback;
    std::wstring wpath = std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(path);

    FILE* f = _wfopen(wpath.c_str(), L"rb");
    if (!f) {
        errorMessage = "Unable to open file: " + path;
        return false;
    }

    std::string line;
    line.reserve(32768);

    auto readLine = [&](std::string& out) -> bool {
        out.clear();
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (c == '\r') continue;
            if (c == '\n') return true;
            out.push_back((char)c);
        }
        return !out.empty();
    };

    fseek(f, 0, SEEK_END);
    long totalBytes = ftell(f);
    fseek(f, 0, SEEK_SET);

    bool foundHeader = false;
    bool foundUnits  = false;

    std::vector<std::string> headerFields;
    std::unordered_map<std::string, size_t> headerIndex;
    std::vector<double> beaconMarkers;
    std::vector<std::string> rowFields;
    rowFields.reserve(128);
    size_t nextBeaconMarker = 0;
    int lastProgressPercent = -1;

    double previousTime = 0.0;
    double previousBeacon = -1.0;

    int lapCounter = 1;
    LapStats currentLap;
    currentLap.lapNumber = lapCounter;
    currentLap.rideHeightUnit = profile == ChannelProfile::AMS2
        ? RideHeightUnit::Centimeters : RideHeightUnit::Millimeters;

    auto parseRowFields = [&](const std::string& row) {
        rowFields.clear();
        std::string current;
        bool inQuotes = false;

        for (char c : row) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                rowFields.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }

        rowFields.push_back(current);
    };

    int idxTime = -1;
    int idxBeacon = -1;
    int idxSpeed = -1;
    int idxThrottle = -1;
    int idxBrake = -1;
    int idxRpm = -1;
    int idxSteer = -1;
    int idxYaw = -1;
    int idxLatG = -1;
    int idxLatGAlt = -1;
    int idxLongG = -1;
    int idxLongGAlt = -1;
    int idxWheelLF = -1;
    int idxWheelRF = -1;
    int idxWheelLR = -1;
    int idxWheelRR = -1;
    int idxBrakeLF = -1;
    int idxBrakeRF = -1;
    int idxBrakeLR = -1;
    int idxBrakeRR = -1;
    int idxPressLF = -1;
    int idxPressRF = -1;
    int idxPressLR = -1;
    int idxPressRR = -1;
    int idxSusLF = -1;
    int idxSusRF = -1;
    int idxSusLR = -1;
    int idxSusRR = -1;
    int idxBumpLF = -1;
    int idxBumpRF = -1;
    int idxBumpLR = -1;
    int idxBumpRR = -1;
    int idxAero0 = -1;
    int idxAero1 = -1;
    int idxAero2 = -1;
    int idxAero3 = -1;
    int idxAeroBalance = -1;
    int idxLsd = -1;
    int idxSurfCFL = -1;
    int idxSurfCRF = -1;
    int idxSurfCLR = -1;
    int idxSurfCRR = -1;
    int idxTyreInnerLF = -1;
    int idxTyreCentreLF = -1;
    int idxTyreOuterLF = -1;
    int idxTyreInnerRF = -1;
    int idxTyreCentreRF = -1;
    int idxTyreOuterRF = -1;
    int idxTyreInnerLR = -1;
    int idxTyreCentreLR = -1;
    int idxTyreOuterLR = -1;
    int idxTyreInnerRR = -1;
    int idxTyreCentreRR = -1;
    int idxTyreOuterRR = -1;
    int idxBrakeTorqueLF = -1;
    int idxBrakeTorqueRF = -1;
    int idxBrakeTorqueLR = -1;
    int idxBrakeTorqueRR = -1;
    int idxWheelSlipLF = -1;
    int idxWheelSlipRF = -1;
    int idxWheelSlipLR = -1;
    int idxWheelSlipRR = -1;
    int idxTyreAirLF = -1;
    int idxTyreAirRF = -1;
    int idxTyreAirLR = -1;
    int idxTyreAirRR = -1;
    int idxSuspVelLF = -1;
    int idxSuspVelRF = -1;
    int idxSuspVelLR = -1;
    int idxSuspVelRR = -1;
    int idxBrakeBias = -1;
    int idxTcsActive = -1;
    int idxAbsActive = -1;
    int idxOilTemp = -1;
    int idxWaterTemp = -1;

    while (readLine(line)) {
        if (progressCallback && totalBytes > 0) {
            long currentPosition = ftell(f);
            if (currentPosition < 0)
                currentPosition = totalBytes;
            int percent = (int)(((double)currentPosition / totalBytes) * 50.0);
            if (percent != lastProgressPercent) {
                lastProgressPercent = percent;
                progressCallback(percent);
            }
        }

        if (line.empty())
            continue;

        if (!foundHeader) {
            std::vector<std::string> metadata = splitCsvRow(line);
            if (metadata.size() > 1 && metadata[0] == "Beacon Markers") {
                std::istringstream markerStream(metadata[1]);
                double marker;
                while (markerStream >> marker)
                    beaconMarkers.push_back(marker);
                continue;
            }
        }

        if (!foundHeader) {
            auto fields = splitCsvRow(line);
            if (fields.size() > 10 && fields[0] == "Time") {
                foundHeader = true;
                headerFields = fields;

                for (size_t i = 0; i < fields.size(); ++i) {
                    std::string name = fields[i];
                    if (!name.empty() && name.front() == '"' && name.back() == '"')
                        name = name.substr(1, name.size() - 2);
                    std::string resolved = resolveChannelKey(name, profile);
                    headerIndex[resolved] = i;
                }

                if (!fieldExists(headerIndex, "Time")) {
                    errorMessage = "CSV header missing Time column.";
                    fclose(f);
                    return false;
                }

                idxTime = fieldIndex(headerIndex, "Time");
                idxBeacon = fieldIndex(headerIndex, "LAP_BEACON");
                idxSpeed = fieldIndex(headerIndex, "SPEED");
                idxThrottle = fieldIndex(headerIndex, "THROTTLE");
                idxBrake = fieldIndex(headerIndex, "BRAKE");
                idxRpm = fieldIndex(headerIndex, "RPMS");
                idxSteer = fieldIndex(headerIndex, "STEERANGLE");
                idxYaw = fieldIndex(headerIndex, "ROTY");
                idxLatG = fieldIndex(headerIndex, "LAT_G");
                idxLatGAlt = fieldIndex(headerIndex, "G_LAT");
                idxLongG = fieldIndex(headerIndex, "LONG_G");
                idxLongGAlt = fieldIndex(headerIndex, "G_LON");
                idxWheelLF = fieldIndex(headerIndex, "WHEEL_SPEED_LF");
                idxWheelRF = fieldIndex(headerIndex, "WHEEL_SPEED_RF");
                idxWheelLR = fieldIndex(headerIndex, "WHEEL_SPEED_LR");
                idxWheelRR = fieldIndex(headerIndex, "WHEEL_SPEED_RR");
                idxBrakeLF = fieldIndex(headerIndex, "BRAKE_TEMP_LF");
                idxBrakeRF = fieldIndex(headerIndex, "BRAKE_TEMP_RF");
                idxBrakeLR = fieldIndex(headerIndex, "BRAKE_TEMP_LR");
                idxBrakeRR = fieldIndex(headerIndex, "BRAKE_TEMP_RR");
                idxPressLF = fieldIndex(headerIndex, "TYRE_PRESS_LF");
                idxPressRF = fieldIndex(headerIndex, "TYRE_PRESS_RF");
                idxPressLR = fieldIndex(headerIndex, "TYRE_PRESS_LR");
                idxPressRR = fieldIndex(headerIndex, "TYRE_PRESS_RR");
                idxSusLF = fieldIndex(headerIndex, "SUS_TRAVEL_LF");
                idxSusRF = fieldIndex(headerIndex, "SUS_TRAVEL_RF");
                idxSusLR = fieldIndex(headerIndex, "SUS_TRAVEL_LR");
                idxSusRR = fieldIndex(headerIndex, "SUS_TRAVEL_RR");
                idxBumpLF = fieldIndex(headerIndex, "BUMPSTOPUP_TRAVEL_LF");
                idxBumpRF = fieldIndex(headerIndex, "BUMPSTOPUP_TRAVEL_RF");
                idxBumpLR = fieldIndex(headerIndex, "BUMPSTOPUP_TRAVEL_LR");
                idxBumpRR = fieldIndex(headerIndex, "BUMPSTOPUP_TRAVEL_RR");
                idxAero0 = fieldIndex(headerIndex, "AERO_HEIGHT0");
                idxAero1 = fieldIndex(headerIndex, "AERO_HEIGHT1");
                idxAero2 = fieldIndex(headerIndex, "AERO_HEIGHT2");
                idxAero3 = fieldIndex(headerIndex, "AERO_HEIGHT3");
                idxAeroBalance = fieldIndex(headerIndex, "AERO_BALANCE");
                idxLsd = fieldIndex(headerIndex, "LSD_LOCKING_TORQUE");
                idxSurfCFL = fieldIndex(headerIndex, "C_SURF_TEMP_LF");
                idxSurfCRF = fieldIndex(headerIndex, "C_SURF_TEMP_RF");
                idxSurfCLR = fieldIndex(headerIndex, "C_SURF_TEMP_LR");
                idxSurfCRR = fieldIndex(headerIndex, "C_SURF_TEMP_RR");
                idxTyreInnerLF = fieldIndex(headerIndex, "TYRE_INNER_TEMP_LF");
                idxTyreCentreLF = fieldIndex(headerIndex, "TYRE_CENTRE_TEMP_LF");
                idxTyreOuterLF = fieldIndex(headerIndex, "TYRE_OUTER_TEMP_LF");
                idxTyreInnerRF = fieldIndex(headerIndex, "TYRE_INNER_TEMP_RF");
                idxTyreCentreRF = fieldIndex(headerIndex, "TYRE_CENTRE_TEMP_RF");
                idxTyreOuterRF = fieldIndex(headerIndex, "TYRE_OUTER_TEMP_RF");
                idxTyreInnerLR = fieldIndex(headerIndex, "TYRE_INNER_TEMP_LR");
                idxTyreCentreLR = fieldIndex(headerIndex, "TYRE_CENTRE_TEMP_LR");
                idxTyreOuterLR = fieldIndex(headerIndex, "TYRE_OUTER_TEMP_LR");
                idxTyreInnerRR = fieldIndex(headerIndex, "TYRE_INNER_TEMP_RR");
                idxTyreCentreRR = fieldIndex(headerIndex, "TYRE_CENTRE_TEMP_RR");
                idxTyreOuterRR = fieldIndex(headerIndex, "TYRE_OUTER_TEMP_RR");
                idxBrakeTorqueLF = fieldIndex(headerIndex, "BRAKE_TORQUE_LF");
                idxBrakeTorqueRF = fieldIndex(headerIndex, "BRAKE_TORQUE_RF");
                idxBrakeTorqueLR = fieldIndex(headerIndex, "BRAKE_TORQUE_LR");
                idxBrakeTorqueRR = fieldIndex(headerIndex, "BRAKE_TORQUE_RR");
                idxWheelSlipLF = fieldIndex(headerIndex, "WHEEL_SR_LF");
                idxWheelSlipRF = fieldIndex(headerIndex, "WHEEL_SR_RF");
                idxWheelSlipLR = fieldIndex(headerIndex, "WHEEL_SR_LR");
                idxWheelSlipRR = fieldIndex(headerIndex, "WHEEL_SR_RR");
                idxTyreAirLF = fieldIndex(headerIndex, "TYRE_TAIR_LF");
                idxTyreAirRF = fieldIndex(headerIndex, "TYRE_TAIR_RF");
                idxTyreAirLR = fieldIndex(headerIndex, "TYRE_TAIR_LR");
                idxTyreAirRR = fieldIndex(headerIndex, "TYRE_TAIR_RR");
                idxSuspVelLF = fieldIndex(headerIndex, "SUSP_VEL_LF");
                idxSuspVelRF = fieldIndex(headerIndex, "SUSP_VEL_RF");
                idxSuspVelLR = fieldIndex(headerIndex, "SUSP_VEL_LR");
                idxSuspVelRR = fieldIndex(headerIndex, "SUSP_VEL_RR");
                idxBrakeBias = fieldIndex(headerIndex, "BRAKE_BIAS_SETTING");
                idxTcsActive = fieldIndex(headerIndex, "TCS_ACTIVE");
                idxAbsActive = fieldIndex(headerIndex, "ABS_ACTIVE");
            }
            continue;
        }

        if (!foundUnits) {
            foundUnits = true;
            continue;
        }

        parseRowFields(line);
        if (rowFields.size() < headerFields.size())
            continue;

        if (idxTime < 0 || idxTime >= (int)rowFields.size()) {
            errorMessage = "Time column missing during parsing.";
            fclose(f);
            return false;
        }

        double timeValue = toDoubleSafe(rowFields[idxTime]);

        double beacon = 0.0;
        if (idxBeacon >= 0 && idxBeacon < (int)rowFields.size())
            beacon = toDoubleSafe(rowFields[idxBeacon]);

        bool newLap = false;

        if (nextBeaconMarker < beaconMarkers.size() && timeValue >= beaconMarkers[nextBeaconMarker]) {
            newLap = true;
            nextBeaconMarker++;
        } else if (idxBeacon >= 0 && previousBeacon >= 0.0) {
            if (beacon > previousBeacon + 0.5)
                newLap = true;
        } else if (timeValue < previousTime - 0.1) {
            newLap = true;
        }

        if (newLap && currentLap.rowCount > 0) {
            currentLap.endTime = previousTime;
            laps.push_back(currentLap);

            lapCounter++;
            currentLap = LapStats();
            currentLap.lapNumber = lapCounter;
            currentLap.rideHeightUnit = profile == ChannelProfile::AMS2
                ? RideHeightUnit::Centimeters : RideHeightUnit::Millimeters;
        }

        if (currentLap.rowCount == 0)
            currentLap.startTime = timeValue;

        currentLap.endTime = timeValue;
        currentLap.rowCount++;

        auto addAt = [&](int idx, auto& stat) {
            if (idx >= 0 && idx < (int)rowFields.size())
                stat.add(toDoubleSafe(rowFields[idx]));
        };

        addAt(idxSpeed, currentLap.speed);
        addAt(idxThrottle, currentLap.throttle);
        addAt(idxBrake, currentLap.brakePedal);
        addAt(idxRpm, currentLap.rpm);
        addAt(idxSteer, currentLap.steerAngle);
        addAt(idxYaw, currentLap.yawRate);

        if (idxLatG >= 0 && idxLatG < (int)rowFields.size())
            currentLap.latG.add(toDoubleSafe(rowFields[idxLatG]));
        else if (idxLatGAlt >= 0 && idxLatGAlt < (int)rowFields.size())
            currentLap.latG.add(toDoubleSafe(rowFields[idxLatGAlt]));

        if (idxLongG >= 0 && idxLongG < (int)rowFields.size())
            currentLap.longG.add(toDoubleSafe(rowFields[idxLongG]));
        else if (idxLongGAlt >= 0 && idxLongGAlt < (int)rowFields.size())
            currentLap.longG.add(toDoubleSafe(rowFields[idxLongGAlt]));

        addAt(idxWheelLF, currentLap.wheelSpeedLF);
        addAt(idxWheelRF, currentLap.wheelSpeedRF);
        addAt(idxWheelLR, currentLap.wheelSpeedLR);
        addAt(idxWheelRR, currentLap.wheelSpeedRR);
        addAt(idxBrakeLF, currentLap.brakeTempLF);
        addAt(idxBrakeRF, currentLap.brakeTempRF);
        addAt(idxBrakeLR, currentLap.brakeTempLR);
        addAt(idxBrakeRR, currentLap.brakeTempRR);
        addAt(idxPressLF, currentLap.pressureLF);
        addAt(idxPressRF, currentLap.pressureRF);
        addAt(idxPressLR, currentLap.pressureLR);
        addAt(idxPressRR, currentLap.pressureRR);
        addAt(idxSusLF, currentLap.susTravelLF);
        addAt(idxSusRF, currentLap.susTravelRF);
        addAt(idxSusLR, currentLap.susTravelLR);
        addAt(idxSusRR, currentLap.susTravelRR);
        auto addDamperSample = [&](int idx, DamperCornerStats& damper) {
            if (idx >= 0 && idx < (int)rowFields.size())
                damper.addSample(toDoubleSafe(rowFields[idx]), timeValue);
        };
        addDamperSample(idxSusLF, currentLap.damperLF);
        addDamperSample(idxSusRF, currentLap.damperRF);
        addDamperSample(idxSusLR, currentLap.damperLR);
        addDamperSample(idxSusRR, currentLap.damperRR);

        auto readOptional = [&](int index) -> double {
            if (index < 0 || index >= (int)rowFields.size())
                return std::numeric_limits<double>::quiet_NaN();
            return toDoubleSafe(rowFields[index]);
        };

        const double sampleSteer = readOptional(idxSteer);
        const double sampleYaw = readOptional(idxYaw);
        const double sampleLatG = idxLatG >= 0 ? readOptional(idxLatG) : readOptional(idxLatGAlt);
        const double sampleLongG = idxLongG >= 0 ? readOptional(idxLongG) : readOptional(idxLongGAlt);
        const double sampleBrake = readOptional(idxBrake);
        const double sampleThrottle = readOptional(idxThrottle);
        const double frontLeftSuspension = readOptional(idxSusLF);
        const double frontRightSuspension = readOptional(idxSusRF);
        const double rearLeftSuspension = readOptional(idxSusLR);
        const double rearRightSuspension = readOptional(idxSusRR);

        double suspensionAsymmetry = 0.0;
        if (std::isfinite(frontLeftSuspension) && std::isfinite(frontRightSuspension) &&
            std::isfinite(rearLeftSuspension) && std::isfinite(rearRightSuspension)) {
            suspensionAsymmetry = std::max(
                std::abs(frontLeftSuspension - frontRightSuspension),
                std::abs(rearLeftSuspension - rearRightSuspension));
        }
        currentLap.addHandlingSample(sampleSteer, sampleYaw, sampleLatG, sampleLongG,
                                     sampleBrake, sampleThrottle, suspensionAsymmetry);
        addAt(idxBumpLF, currentLap.bumpStopLF);
        addAt(idxBumpRF, currentLap.bumpStopRF);
        addAt(idxBumpLR, currentLap.bumpStopLR);
        addAt(idxBumpRR, currentLap.bumpStopRR);
        addAt(idxAero0, currentLap.aeroHeight0);
        addAt(idxAero1, currentLap.aeroHeight1);
        addAt(idxAero2, currentLap.aeroHeight2);
        addAt(idxAero3, currentLap.aeroHeight3);
        addAt(idxAeroBalance, currentLap.aeroBalance);
        addAt(idxLsd, currentLap.lsdTorque);
        addAt(idxSurfCFL, currentLap.surfTempCFL);
        addAt(idxSurfCRF, currentLap.surfTempCRF);
        addAt(idxSurfCLR, currentLap.surfTempCLR);
        addAt(idxSurfCRR, currentLap.surfTempCRR);
        addAt(idxTyreInnerLF, currentLap.tyreInnerTempLF);
        addAt(idxTyreCentreLF, currentLap.tyreCentreTempLF);
        addAt(idxTyreOuterLF, currentLap.tyreOuterTempLF);
        addAt(idxTyreInnerRF, currentLap.tyreInnerTempRF);
        addAt(idxTyreCentreRF, currentLap.tyreCentreTempRF);
        addAt(idxTyreOuterRF, currentLap.tyreOuterTempRF);
        addAt(idxTyreInnerLR, currentLap.tyreInnerTempLR);
        addAt(idxTyreCentreLR, currentLap.tyreCentreTempLR);
        addAt(idxTyreOuterLR, currentLap.tyreOuterTempLR);
        addAt(idxTyreInnerRR, currentLap.tyreInnerTempRR);
        addAt(idxTyreCentreRR, currentLap.tyreCentreTempRR);
        addAt(idxTyreOuterRR, currentLap.tyreOuterTempRR);
        addAt(idxBrakeTorqueLF, currentLap.brakeTorqueLF);
        addAt(idxBrakeTorqueRF, currentLap.brakeTorqueRF);
        addAt(idxBrakeTorqueLR, currentLap.brakeTorqueLR);
        addAt(idxBrakeTorqueRR, currentLap.brakeTorqueRR);
        addAt(idxWheelSlipLF, currentLap.wheelSlipLF);
        addAt(idxWheelSlipRF, currentLap.wheelSlipRF);
        addAt(idxWheelSlipLR, currentLap.wheelSlipLR);
        addAt(idxWheelSlipRR, currentLap.wheelSlipRR);
        addAt(idxTyreAirLF, currentLap.tyreAirTempLF);
        addAt(idxTyreAirRF, currentLap.tyreAirTempRF);
        addAt(idxTyreAirLR, currentLap.tyreAirTempLR);
        addAt(idxTyreAirRR, currentLap.tyreAirTempRR);
        addAt(idxSuspVelLF, currentLap.suspVelLF);
        addAt(idxSuspVelRF, currentLap.suspVelRF);
        addAt(idxSuspVelLR, currentLap.suspVelLR);
        addAt(idxSuspVelRR, currentLap.suspVelRR);
        addAt(idxBrakeBias, currentLap.brakeBiasSetting);
        addAt(idxTcsActive, currentLap.tcsActive);
        addAt(idxAbsActive, currentLap.absActive);
        addAt(idxOilTemp, currentLap.oilTemp);
        addAt(idxWaterTemp, currentLap.waterTemp);

        previousTime = timeValue;
        previousBeacon = beacon;
    }

    fclose(f);

    if (currentLap.rowCount > 0)
        laps.push_back(currentLap);

    if (laps.empty()) {
        errorMessage = "No valid lap data found.";
        return false;
    }

    return true;
}


