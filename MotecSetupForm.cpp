//---------------------------------------------------------------------------
// MotecSetupForm.cpp — Implémentation de la fenêtre principale.
//
// Contient :
//   - Persistance des réglages dans MotecDataAnalyser.ini (dossiers récents,
//     rapport complet, langue, profil de canaux) via TIniFile.
//   - Chargement / création des seuils d'analyse dans
//     MotecDataAnalyser.thresholds.ini (loadThresholds / saveDefaultThresholds).
//   - Callbacks de progression (UpdateProgress / AnalysisProgress / ExportProgress).
//   - Gestion des boutons : ButtonLoad (CSV) et Button1 (LD/LDX), qui appellent
//     parseMotecCsv / parseMotecLd puis buildBestThreeLapReport.
//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop

#include <System.UITypes.hpp>
#include <System.SysUtils.hpp>
#include <IniFiles.hpp>
#include <shlobj.h>
#include <shobjidl.h>
#include <userenv.h>
#pragma comment(lib, "userenv")
#include <string>
#include <vector>

#include "MotecSetupForm.h"
#include "MotecToCSV.h"
#include "RuleConfig.h"
#include "MotecSettingsForm.h"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

// GDI+ Token for lifecycle management
ULONG_PTR gdiplusToken;

// ===============================

// Symptom weight INI persistence (defined in analyze_motec_setup.cpp)
void saveSymptomWeights(TIniFile* ini);
void loadSymptomWeights(TIniFile* ini);


#pragma package(smart_init)
#pragma resource "*.dfm"

TFormMotecSetup *FormMotecSetup;

// ===============================
// Chemins de base (Étape 2 — portabilité).
// Tous les fichiers d'application sont résolus relativement à l'exécutable,
// pour que le dossier portable fonctionne n'importe où il est décompressé.
// ===============================

String AppDirPath()
{
	return IncludeTrailingPathDelimiter(ExtractFilePath(Application->ExeName));
}

String DataPath()
{
	// Dossiers d'échantillons : <exedir>data\ (paquet portable) sinon le dossier courant.
	String p = AppDirPath() + "data\\";
	if (!DirectoryExists(p))
		p = ExtractFilePath(Application->ExeName);
	return IncludeTrailingPathDelimiter(p);
}

String SettingsFilePath()
{
	return AppDirPath() + "MotecDataAnalyser.ini";
}

String DefaultMotecFolder()
{
	// "Saved Games" is a known folder: Windows returns the localized name
	// ("Saved Games" / "Jeux enregistrés" / ...) for the current user.
	String savedGames;
	PWSTR path = NULL;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, NULL, &path)))
	{
		savedGames = String(path);
		CoTaskMemFree(path);
	}
	if (savedGames.IsEmpty())
	{
		// Fallback: user profile directory + literal "Saved Games".
		DWORD size = 0;
		if (GetUserProfileDirectory(0, NULL, &size) && size > 0)
		{
			std::vector<WCHAR> buffer(size);
			if (GetUserProfileDirectory(0, &buffer[0], &size))
				savedGames = String(&buffer[0]) + "\\Saved Games";
		}
	}
	if (savedGames.IsEmpty())
		return "";
	String folder = savedGames + "\\ACE\\MoTec";
	return DirectoryExists(folder) ? folder : "";
}

String ReadLastFolder(const String& section, const String& fallbackSection = "")
{
	TIniFile* settings = new TIniFile(SettingsFilePath());
	try {
		String folder = settings->ReadString(section, "LastFolder", "");
		if (!folder.IsEmpty() && DirectoryExists(folder))
			return folder;
		if (!fallbackSection.IsEmpty()) {
			folder = settings->ReadString(fallbackSection, "LastFolder", "");
			if (!folder.IsEmpty() && DirectoryExists(folder))
				return folder;
		}
		String def = DefaultMotecFolder();
		if (!def.IsEmpty())
			return def;
	} __finally {
		delete settings;
	}
	return ExtractFilePath(Application->ExeName);
}

String LdSectionForProfile(ChannelProfile profile)
{
	return (profile == ChannelProfile::EvoM1) ? "LdImport_Evo" : "LdImport_AMS2";
}

void SaveLastFolder(const String& section, const String& folder)
{
	TIniFile* settings = new TIniFile(SettingsFilePath());
	try {
		settings->WriteString(section, "LastFolder", folder);
	} __finally {
		delete settings;
	}
}

ReportLanguage TFormMotecSetup::ReadReportLanguageSetting()
{
	TIniFile* settings = new TIniFile(SettingsFilePath());
	try {
		int value = settings->ReadInteger("General", "ReportLanguage", (int)ReportLanguage::English);
		if (value == (int)ReportLanguage::French)
			return ReportLanguage::French;
		return ReportLanguage::English;
	} __finally {
		delete settings;
	}
}

void SaveReportLanguageSetting(ReportLanguage language)
{
	TIniFile* settings = new TIniFile(SettingsFilePath());
	try {
		settings->WriteInteger("General", "ReportLanguage", (int)language);
	} __finally {
		delete settings;
	}
}

ChannelProfile ReadChannelProfileSetting()
{
	TIniFile* settings = new TIniFile(SettingsFilePath());
	try {
		int value = settings->ReadInteger("General", "ChannelProfile", (int)ChannelProfile::EvoM1);
		if (value == (int)ChannelProfile::Generic)
			return ChannelProfile::Generic;
		return ChannelProfile::EvoM1;
	} __finally {
		delete settings;
	}
}

void SaveChannelProfileSetting(ChannelProfile profile)
{
	TIniFile* settings = new TIniFile(SettingsFilePath());
	try {
		settings->WriteInteger("General", "ChannelProfile", (int)profile);
	} __finally {
		delete settings;
	}
}
// ===============================
// Thresholds configuration
// ===============================

TFormMotecSetup::~TFormMotecSetup()
{
    Gdiplus::GdiplusShutdown(gdiplusToken);
}

String ThresholdsFilePath()
{
	return AppDirPath() + "MotecDataAnalyser.thresholds.ini";
}

// ===============================
// Rule configuration (JSON) — Étape 1 du plan de développement.
// Les règles de symptômes et les alias de canaux vivent dans RuleConfig ;
// un fichier JSON optionnel permet de les personnaliser sans recompiler.
// ===============================

String RulesFilePath()
{
	return AppDirPath() + "MotecDataAnalyser.rules.json";
}

void loadRuleConfig()
{
	String appDir = AppDirPath(); // avec séparateur final

	// 1) À côté de l'exécutable.
	String path = appDir + "MotecDataAnalyser.rules.json";
	if (!FileExists(path)) {
		// 2) Sous-répertoire config\ (paquet portable).
		path = appDir + "config\\MotecDataAnalyser.rules.json";
	}

	RuleLoadStatus status;
	if (FileExists(path)) {
		UTF8String utf8 = path;
		std::string stdPath = utf8.c_str();
		status = RuleConfig::instance().load(stdPath);
		// FileNotFound / ParseError : les valeurs par défaut intégrées restent en place.
	} else {
		RuleConfig::instance().resetToDefaults();
	}
}

void loadChannelMappingConfig()
{
    String appDir = AppDirPath();
    String path = appDir + "MotecDataAnalyser.mapping.json";
    if (!FileExists(path))
        path = appDir + "config\\MotecDataAnalyser.mapping.json";

    if (FileExists(path)) {
        UTF8String utf8 = path;
        RuleConfig::instance().loadChannelMappings(utf8.c_str());
    } else {
        RuleConfig::instance().loadChannelMappings("");
    }
}

void saveDefaultThresholds()
{
	TIniFile* ini = new TIniFile(ThresholdsFilePath());
	try {
		// General
		ini->WriteInteger("General", "MinDamperSamples", g_thresholds.minDamperSamples);
		ini->WriteInteger("General", "MinHandlingSamples", g_thresholds.minHandlingSamples);
		ini->WriteInteger("General", "MinSteerYawSamples", g_thresholds.minSteerYawSamples);
		ini->WriteInteger("General", "HandlingConfidenceSamples", g_thresholds.handlingConfidenceSamples);

		// Pressure (psi)
        ini->WriteFloat("Pressure", "Low", g_thresholds.pressureLowPsi);
        ini->WriteFloat("Pressure", "High", g_thresholds.pressureHighPsi);
		ini->WriteFloat("Pressure", "SideDiffMax", g_thresholds.pressureSideDiffMax);
		ini->WriteFloat("Pressure", "FrontRearDiffMax", g_thresholds.pressureFrontRearDiffMax);

		// Tyre temp spread (C)
		ini->WriteFloat("TyreTemp", "CamberMinSpread", g_thresholds.camberMinSpread);
		ini->WriteFloat("TyreTemp", "CamberMaxSpread", g_thresholds.camberMaxSpread);
		ini->WriteFloat("TyreTemp", "CentreLrDiffMax", g_thresholds.tyreCentreTempLrDiffMax);
		ini->WriteFloat("TyreTemp", "AirLrDiffMax", g_thresholds.tyreAirTempLrDiffMax);

		// LSD
		ini->WriteFloat("LSD", "HighEvo", g_thresholds.lsdHighEvo);
		ini->WriteFloat("LSD", "LowEvo", g_thresholds.lsdLowEvo);
		ini->WriteFloat("LSD", "HighAms2", g_thresholds.lsdHighAms2);
		ini->WriteFloat("LSD", "LowAms2", g_thresholds.lsdLowAms2);

		// Damper
		ini->WriteFloat("Damper", "LowSpeedThreshold", g_thresholds.damperLowSpeedThreshold);
		ini->WriteFloat("Damper", "AsymmetryThreshold", g_thresholds.damperAsymmetryThreshold);
		ini->WriteFloat("Damper", "HighSpeedShareMax", g_thresholds.damperHighSpeedShareMax);
		ini->WriteFloat("Damper", "LowSpeedShareMin", g_thresholds.damperLowSpeedShareMin);
		ini->WriteFloat("Damper", "LowSpeedStdDevMax", g_thresholds.damperLowSpeedStdDevMax);
		ini->WriteFloat("Damper", "HistogramYOffset", g_thresholds.histogramYOffset);

		// Handling
		ini->WriteFloat("Handling", "BrakePedalMin", g_thresholds.brakePedalMin);
		ini->WriteFloat("Handling", "ThrottleMaxBraking", g_thresholds.throttleMaxBraking);
		ini->WriteFloat("Handling", "ThrottleMinAccelerating", g_thresholds.throttleMinAccelerating);
		ini->WriteFloat("Handling", "SteerMinCornering", g_thresholds.steerMinCornering);
		ini->WriteFloat("Handling", "LatGMinCornering", g_thresholds.latGMinCornering);
		ini->WriteFloat("Handling", "SteerChangeMin", g_thresholds.steerChangeMin);
		ini->WriteFloat("Handling", "YawChangeMin", g_thresholds.yawChangeMin);
		ini->WriteFloat("Handling", "LatGMaxUndersteer", g_thresholds.latGMaxUndersteer);
		ini->WriteFloat("Handling", "YawMinOversteer", g_thresholds.yawMinOversteer);
		ini->WriteFloat("Handling", "SteerChangeMaxOversteer", g_thresholds.steerChangeMaxOversteer);
		ini->WriteFloat("Handling", "UndersteerRatioMin", g_thresholds.understeerRatioMin);
		ini->WriteFloat("Handling", "OversteerRatioMin", g_thresholds.oversteerRatioMin);

		// Thermal
		ini->WriteFloat("Thermal", "WaterTempMax", g_thresholds.waterTempMax);
		ini->WriteFloat("Thermal", "OilTempMax", g_thresholds.oilTempMax);

		// Suspension
		ini->WriteFloat("Suspension", "TravelMax", g_thresholds.suspensionTravelMax);
		ini->WriteFloat("Suspension", "RideHeightDiffMax", g_thresholds.rideHeightDiffMax);
		ini->WriteFloat("Suspension", "VelocityMax", g_thresholds.suspensionVelocityMax);
		ini->WriteFloat("Suspension", "AvgTravelFrontMax", g_thresholds.suspensionAvgTravelFrontMax);
		ini->WriteFloat("Suspension", "AvgTravelRearMax", g_thresholds.suspensionAvgTravelRearMax);

		// Brake
		ini->WriteFloat("Brake", "TempDiffMax", g_thresholds.brakeTempDiffMax);
		ini->WriteFloat("Brake", "BiasMax", g_thresholds.brakeBiasMax);
		ini->WriteFloat("Brake", "BiasFrontAvgMin", g_thresholds.brakeBiasFrontAvgMin);
		ini->WriteFloat("Brake", "TorqueMin", g_thresholds.brakeTorqueMin);
		ini->WriteFloat("Brake", "ChannelMismatchMax", g_thresholds.brakeChannelMismatchMax);
		ini->WriteFloat("Brake", "TorqueFrontShareMax", g_thresholds.brakeTorqueFrontShareMax);
		ini->WriteFloat("Brake", "TorqueFrontShareMin", g_thresholds.brakeTorqueFrontShareMin);
		ini->WriteFloat("Brake", "TorqueLrDiffRatio", g_thresholds.brakeTorqueLrDiffRatio);
		ini->WriteFloat("Brake", "BiasSettingFrontMax", g_thresholds.brakeBiasSettingFrontMax);
		ini->WriteFloat("Brake", "BiasSettingRearMin", g_thresholds.brakeBiasSettingRearMin);

		// Channel mismatch
		ini->WriteFloat("ChannelMismatch", "PressureMax", g_thresholds.pressureChannelMismatchMax);
		ini->WriteFloat("ChannelMismatch", "PressureMin", g_thresholds.pressureChannelMismatchMin);
		ini->WriteFloat("ChannelMismatch", "SuspensionMax", g_thresholds.suspensionChannelMismatchMax);
		ini->WriteFloat("ChannelMismatch", "SuspensionMin", g_thresholds.suspensionChannelMismatchMin);

		// TCS / ABS
		ini->WriteFloat("TCS", "ActiveMax", g_thresholds.tcsActiveMax);
		ini->WriteFloat("ABS", "ActiveMax", g_thresholds.absActiveMax);

		// Wheel slip
		ini->WriteFloat("WheelSlip", "Max", g_thresholds.wheelSlipMax);

		// Wheel speed imbalance
		ini->WriteFloat("WheelSpeed", "FrontDiffMax", g_thresholds.wheelSpeedFrontDiffMax);
		ini->WriteFloat("WheelSpeed", "RearDiffMax", g_thresholds.wheelSpeedRearDiffMax);
		ini->WriteFloat("WheelSpeed", "TempDiffMax", g_thresholds.wheelSpeedTempDiffMax);

		// Traction loss
		ini->WriteFloat("Traction", "RearDiffMax", g_thresholds.tractionRearDiffMax);
		ini->WriteFloat("Traction", "RearTempDiffMax", g_thresholds.tractionRearTempDiffMax);
		ini->WriteFloat("Traction", "LongGMax", g_thresholds.tractionLongGMax);
		ini->WriteFloat("Traction", "FrontDiffMax", g_thresholds.tractionFrontDiffMax);
		ini->WriteFloat("Traction", "RearDiffMin", g_thresholds.tractionRearDiffMin);
		ini->WriteFloat("Traction", "RearTempSpreadMax", g_thresholds.tractionRearTempSpreadMax);
		ini->WriteFloat("Traction", "RearPressureSpreadMax", g_thresholds.tractionRearPressureSpreadMax);

		// Bumpstop
		ini->WriteFloat("Bumpstop", "Min", g_thresholds.bumpStopMin);

		// Aero
		ini->WriteFloat("Aero", "BalanceMax", g_thresholds.aeroBalanceMax);
		ini->WriteFloat("Aero", "HeightDiffMax", g_thresholds.aeroHeightDiffMax);
		ini->WriteFloat("Aero", "SpeedMin", g_thresholds.aeroSpeedMin);

		// Steering
		ini->WriteFloat("Steering", "CorrUndersteer", g_thresholds.steerYawCorrUndersteer);
		ini->WriteFloat("Steering", "CorrOversteer", g_thresholds.steerYawCorrOversteer);
		ini->WriteFloat("Steering", "SpeedLowMax", g_thresholds.speedLowMax);
		ini->WriteFloat("Steering", "SpeedMediumMax", g_thresholds.speedMediumMax);

		// Stability
		ini->WriteFloat("Stability", "ScoreMin", g_thresholds.stabilityScoreMin);
		ini->WriteFloat("Stability", "CorrUndersteer", g_thresholds.stabilityCorrUndersteer);
		ini->WriteFloat("Stability", "CorrOversteer", g_thresholds.stabilityCorrOversteer);
		ini->WriteFloat("Stability", "WheelSpeedDiffMax", g_thresholds.stabilityWheelSpeedDiffMax);
		ini->WriteFloat("Stability", "PressureDiffMax", g_thresholds.stabilityPressureDiffMax);
		ini->WriteFloat("Stability", "TempDiffMax", g_thresholds.stabilityTempDiffMax);

		// Lap
		ini->WriteFloat("Lap", "TimeGapMax", g_thresholds.lapTimeGapMax);
		ini->WriteFloat("Lap", "MinValidTimeMult", g_thresholds.minValidLapTimeMultiplier);
		ini->WriteFloat("Lap", "MaxValidTimeMult", g_thresholds.maxValidLapTimeMultiplier);

		// Confidence scoring
		ini->WriteFloat("Confidence", "Low", g_thresholds.confidenceLow);
		ini->WriteFloat("Confidence", "Medium", g_thresholds.confidenceMedium);
		ini->WriteFloat("Confidence", "High", g_thresholds.confidenceHigh);
		ini->WriteInteger("Confidence", "MinLapsForReproducibility", g_thresholds.minLapsForReproducibility);

		// Symptom cause weights (Phase F)
		saveSymptomWeights(ini);
	} __finally {
		delete ini;
	}
}

void loadThresholdsPart1(TIniFile* ini)
{
	// General
	g_thresholds.minDamperSamples = ini->ReadInteger("General", "MinDamperSamples", g_thresholds.minDamperSamples);
	g_thresholds.minHandlingSamples = ini->ReadInteger("General", "MinHandlingSamples", g_thresholds.minHandlingSamples);
	g_thresholds.minSteerYawSamples = ini->ReadInteger("General", "MinSteerYawSamples", g_thresholds.minSteerYawSamples);
	g_thresholds.handlingConfidenceSamples = ini->ReadInteger("General", "HandlingConfidenceSamples", g_thresholds.handlingConfidenceSamples);

	// Pressure (psi)
    g_thresholds.pressureLowPsi = ini->ReadFloat("Pressure", "Low",
        ini->ReadFloat("Pressure", "LowEvo", ini->ReadFloat("Pressure", "LowAms2", g_thresholds.pressureLowPsi)));
    g_thresholds.pressureHighPsi = ini->ReadFloat("Pressure", "High",
        ini->ReadFloat("Pressure", "HighEvo", ini->ReadFloat("Pressure", "HighAms2", g_thresholds.pressureHighPsi)));
	g_thresholds.pressureSideDiffMax = ini->ReadFloat("Pressure", "SideDiffMax", g_thresholds.pressureSideDiffMax);
	g_thresholds.pressureFrontRearDiffMax = ini->ReadFloat("Pressure", "FrontRearDiffMax", g_thresholds.pressureFrontRearDiffMax);

	// Tyre temp spread (C)
	g_thresholds.camberMinSpread = ini->ReadFloat("TyreTemp", "CamberMinSpread", g_thresholds.camberMinSpread);
	g_thresholds.camberMaxSpread = ini->ReadFloat("TyreTemp", "CamberMaxSpread", g_thresholds.camberMaxSpread);
	g_thresholds.tyreCentreTempLrDiffMax = ini->ReadFloat("TyreTemp", "CentreLrDiffMax", g_thresholds.tyreCentreTempLrDiffMax);
	g_thresholds.tyreAirTempLrDiffMax = ini->ReadFloat("TyreTemp", "AirLrDiffMax", g_thresholds.tyreAirTempLrDiffMax);

	// LSD
	g_thresholds.lsdHighEvo = ini->ReadFloat("LSD", "HighEvo", g_thresholds.lsdHighEvo);
	g_thresholds.lsdLowEvo = ini->ReadFloat("LSD", "LowEvo", g_thresholds.lsdLowEvo);
	g_thresholds.lsdHighAms2 = ini->ReadFloat("LSD", "HighAms2", g_thresholds.lsdHighAms2);
	g_thresholds.lsdLowAms2 = ini->ReadFloat("LSD", "LowAms2", g_thresholds.lsdLowAms2);

	// Damper
	g_thresholds.damperLowSpeedThreshold = ini->ReadFloat("Damper", "LowSpeedThreshold", g_thresholds.damperLowSpeedThreshold);
	g_thresholds.damperAsymmetryThreshold = ini->ReadFloat("Damper", "AsymmetryThreshold", g_thresholds.damperAsymmetryThreshold);
	g_thresholds.damperHighSpeedShareMax = ini->ReadFloat("Damper", "HighSpeedShareMax", g_thresholds.damperHighSpeedShareMax);
	g_thresholds.damperLowSpeedShareMin = ini->ReadFloat("Damper", "LowSpeedShareMin", g_thresholds.damperLowSpeedShareMin);
	g_thresholds.damperLowSpeedStdDevMax = ini->ReadFloat("Damper", "LowSpeedStdDevMax", g_thresholds.damperLowSpeedStdDevMax);
	g_thresholds.histogramYOffset = ini->ReadFloat("Damper", "HistogramYOffset", g_thresholds.histogramYOffset);
	g_thresholds.damperSlowSpeedRange = ini->ReadFloat("Damper", "SlowSpeedRange", g_thresholds.damperSlowSpeedRange);

	// Damper 4-zone targets (%)
	g_thresholds.damperHscTargetMin = ini->ReadFloat("DamperZones", "HscTargetMin", g_thresholds.damperHscTargetMin);
	g_thresholds.damperHscTargetMax = ini->ReadFloat("DamperZones", "HscTargetMax", g_thresholds.damperHscTargetMax);
	g_thresholds.damperLscTargetMin = ini->ReadFloat("DamperZones", "LscTargetMin", g_thresholds.damperLscTargetMin);
	g_thresholds.damperLscTargetMax = ini->ReadFloat("DamperZones", "LscTargetMax", g_thresholds.damperLscTargetMax);
	g_thresholds.damperLsrTargetMin = ini->ReadFloat("DamperZones", "LsrTargetMin", g_thresholds.damperLsrTargetMin);
	g_thresholds.damperLsrTargetMax = ini->ReadFloat("DamperZones", "LsrTargetMax", g_thresholds.damperLsrTargetMax);
	g_thresholds.damperHsrTargetMin = ini->ReadFloat("DamperZones", "HsrTargetMin", g_thresholds.damperHsrTargetMin);
	g_thresholds.damperHsrTargetMax = ini->ReadFloat("DamperZones", "HsrTargetMax", g_thresholds.damperHsrTargetMax);
	g_thresholds.damperZoneModerateBand = ini->ReadFloat("DamperZones", "ModerateBand", g_thresholds.damperZoneModerateBand);

	// Handling
	g_thresholds.brakePedalMin = ini->ReadFloat("Handling", "BrakePedalMin", g_thresholds.brakePedalMin);
	g_thresholds.throttleMaxBraking = ini->ReadFloat("Handling", "ThrottleMaxBraking", g_thresholds.throttleMaxBraking);
	g_thresholds.throttleMinAccelerating = ini->ReadFloat("Handling", "ThrottleMinAccelerating", g_thresholds.throttleMinAccelerating);
	g_thresholds.steerMinCornering = ini->ReadFloat("Handling", "SteerMinCornering", g_thresholds.steerMinCornering);
	g_thresholds.latGMinCornering = ini->ReadFloat("Handling", "LatGMinCornering", g_thresholds.latGMinCornering);
	g_thresholds.steerChangeMin = ini->ReadFloat("Handling", "SteerChangeMin", g_thresholds.steerChangeMin);
	g_thresholds.yawChangeMin = ini->ReadFloat("Handling", "YawChangeMin", g_thresholds.yawChangeMin);
	g_thresholds.latGMaxUndersteer = ini->ReadFloat("Handling", "LatGMaxUndersteer", g_thresholds.latGMaxUndersteer);
	g_thresholds.yawMinOversteer = ini->ReadFloat("Handling", "YawMinOversteer", g_thresholds.yawMinOversteer);
	g_thresholds.steerChangeMaxOversteer = ini->ReadFloat("Handling", "SteerChangeMaxOversteer", g_thresholds.steerChangeMaxOversteer);
	g_thresholds.understeerRatioMin = ini->ReadFloat("Handling", "UndersteerRatioMin", g_thresholds.understeerRatioMin);
	g_thresholds.oversteerRatioMin = ini->ReadFloat("Handling", "OversteerRatioMin", g_thresholds.oversteerRatioMin);
}

void loadThresholdsPart2(TIniFile* ini)
{
	// Thermal
	g_thresholds.waterTempMax = ini->ReadFloat("Thermal", "WaterTempMax", g_thresholds.waterTempMax);
	g_thresholds.oilTempMax = ini->ReadFloat("Thermal", "OilTempMax", g_thresholds.oilTempMax);

	// Suspension
	g_thresholds.suspensionTravelMax = ini->ReadFloat("Suspension", "TravelMax", g_thresholds.suspensionTravelMax);
	g_thresholds.rideHeightDiffMax = ini->ReadFloat("Suspension", "RideHeightDiffMax", g_thresholds.rideHeightDiffMax);
	g_thresholds.suspensionVelocityMax = ini->ReadFloat("Suspension", "VelocityMax", g_thresholds.suspensionVelocityMax);
	g_thresholds.suspensionAvgTravelFrontMax = ini->ReadFloat("Suspension", "AvgTravelFrontMax", g_thresholds.suspensionAvgTravelFrontMax);
	g_thresholds.suspensionAvgTravelRearMax = ini->ReadFloat("Suspension", "AvgTravelRearMax", g_thresholds.suspensionAvgTravelRearMax);

	// Brake
	g_thresholds.brakeTempDiffMax = ini->ReadFloat("Brake", "TempDiffMax", g_thresholds.brakeTempDiffMax);
	g_thresholds.brakeBiasMax = ini->ReadFloat("Brake", "BiasMax", g_thresholds.brakeBiasMax);
	g_thresholds.brakeBiasFrontAvgMin = ini->ReadFloat("Brake", "BiasFrontAvgMin", g_thresholds.brakeBiasFrontAvgMin);
	g_thresholds.brakeTorqueMin = ini->ReadFloat("Brake", "TorqueMin", g_thresholds.brakeTorqueMin);
	g_thresholds.brakeChannelMismatchMax = ini->ReadFloat("Brake", "ChannelMismatchMax", g_thresholds.brakeChannelMismatchMax);
	g_thresholds.brakeTorqueFrontShareMax = ini->ReadFloat("Brake", "TorqueFrontShareMax", g_thresholds.brakeTorqueFrontShareMax);
	g_thresholds.brakeTorqueFrontShareMin = ini->ReadFloat("Brake", "TorqueFrontShareMin", g_thresholds.brakeTorqueFrontShareMin);
	g_thresholds.brakeTorqueLrDiffRatio = ini->ReadFloat("Brake", "TorqueLrDiffRatio", g_thresholds.brakeTorqueLrDiffRatio);
	g_thresholds.brakeBiasSettingFrontMax = ini->ReadFloat("Brake", "BiasSettingFrontMax", g_thresholds.brakeBiasSettingFrontMax);
	g_thresholds.brakeBiasSettingRearMin = ini->ReadFloat("Brake", "BiasSettingRearMin", g_thresholds.brakeBiasSettingRearMin);

	// Channel mismatch
	g_thresholds.pressureChannelMismatchMax = ini->ReadFloat("ChannelMismatch", "PressureMax", g_thresholds.pressureChannelMismatchMax);
	g_thresholds.pressureChannelMismatchMin = ini->ReadFloat("ChannelMismatch", "PressureMin", g_thresholds.pressureChannelMismatchMin);
	g_thresholds.suspensionChannelMismatchMax = ini->ReadFloat("ChannelMismatch", "SuspensionMax", g_thresholds.suspensionChannelMismatchMax);
	g_thresholds.suspensionChannelMismatchMin = ini->ReadFloat("ChannelMismatch", "SuspensionMin", g_thresholds.suspensionChannelMismatchMin);

	// TCS / ABS
	g_thresholds.tcsActiveMax = ini->ReadFloat("TCS", "ActiveMax", g_thresholds.tcsActiveMax);
	g_thresholds.absActiveMax = ini->ReadFloat("ABS", "ActiveMax", g_thresholds.absActiveMax);

	// Wheel slip
	g_thresholds.wheelSlipMax = ini->ReadFloat("WheelSlip", "Max", g_thresholds.wheelSlipMax);

	// Wheel speed imbalance
	g_thresholds.wheelSpeedFrontDiffMax = ini->ReadFloat("WheelSpeed", "FrontDiffMax", g_thresholds.wheelSpeedFrontDiffMax);
	g_thresholds.wheelSpeedRearDiffMax = ini->ReadFloat("WheelSpeed", "RearDiffMax", g_thresholds.wheelSpeedRearDiffMax);
	g_thresholds.wheelSpeedTempDiffMax = ini->ReadFloat("WheelSpeed", "TempDiffMax", g_thresholds.wheelSpeedTempDiffMax);

	// Traction loss
	g_thresholds.tractionRearDiffMax = ini->ReadFloat("Traction", "RearDiffMax", g_thresholds.tractionRearDiffMax);
	g_thresholds.tractionRearTempDiffMax = ini->ReadFloat("Traction", "RearTempDiffMax", g_thresholds.tractionRearTempDiffMax);
	g_thresholds.tractionLongGMax = ini->ReadFloat("Traction", "LongGMax", g_thresholds.tractionLongGMax);
	g_thresholds.tractionFrontDiffMax = ini->ReadFloat("Traction", "FrontDiffMax", g_thresholds.tractionFrontDiffMax);
	g_thresholds.tractionRearDiffMin = ini->ReadFloat("Traction", "RearDiffMin", g_thresholds.tractionRearDiffMin);
	g_thresholds.tractionRearTempSpreadMax = ini->ReadFloat("Traction", "RearTempSpreadMax", g_thresholds.tractionRearTempSpreadMax);
	g_thresholds.tractionRearPressureSpreadMax = ini->ReadFloat("Traction", "RearPressureSpreadMax", g_thresholds.tractionRearPressureSpreadMax);

	// Bumpstop
	g_thresholds.bumpStopMin = ini->ReadFloat("Bumpstop", "Min", g_thresholds.bumpStopMin);

	// Aero
	g_thresholds.aeroBalanceMax = ini->ReadFloat("Aero", "BalanceMax", g_thresholds.aeroBalanceMax);
	g_thresholds.aeroHeightDiffMax = ini->ReadFloat("Aero", "HeightDiffMax", g_thresholds.aeroHeightDiffMax);
	g_thresholds.aeroSpeedMin = ini->ReadFloat("Aero", "SpeedMin", g_thresholds.aeroSpeedMin);

	// Steering
	g_thresholds.steerYawCorrUndersteer = ini->ReadFloat("Steering", "CorrUndersteer", g_thresholds.steerYawCorrUndersteer);
	g_thresholds.steerYawCorrOversteer = ini->ReadFloat("Steering", "CorrOversteer", g_thresholds.steerYawCorrOversteer);
	g_thresholds.speedLowMax = ini->ReadFloat("Steering", "SpeedLowMax", g_thresholds.speedLowMax);
	g_thresholds.speedMediumMax = ini->ReadFloat("Steering", "SpeedMediumMax", g_thresholds.speedMediumMax);

	// Stability
	g_thresholds.stabilityScoreMin = ini->ReadFloat("Stability", "ScoreMin", g_thresholds.stabilityScoreMin);
	g_thresholds.stabilityCorrUndersteer = ini->ReadFloat("Stability", "CorrUndersteer", g_thresholds.stabilityCorrUndersteer);
	g_thresholds.stabilityCorrOversteer = ini->ReadFloat("Stability", "CorrOversteer", g_thresholds.stabilityCorrOversteer);
	g_thresholds.stabilityWheelSpeedDiffMax = ini->ReadFloat("Stability", "WheelSpeedDiffMax", g_thresholds.stabilityWheelSpeedDiffMax);
	g_thresholds.stabilityPressureDiffMax = ini->ReadFloat("Stability", "PressureDiffMax", g_thresholds.stabilityPressureDiffMax);
	g_thresholds.stabilityTempDiffMax = ini->ReadFloat("Stability", "TempDiffMax", g_thresholds.stabilityTempDiffMax);

	// Lap
	g_thresholds.lapTimeGapMax = ini->ReadFloat("Lap", "TimeGapMax", g_thresholds.lapTimeGapMax);
	g_thresholds.minValidLapTimeMultiplier = ini->ReadFloat("Lap", "MinValidTimeMult", g_thresholds.minValidLapTimeMultiplier);
	g_thresholds.maxValidLapTimeMultiplier = ini->ReadFloat("Lap", "MaxValidTimeMult", g_thresholds.maxValidLapTimeMultiplier);

	// Confidence scoring
	g_thresholds.confidenceLow = ini->ReadFloat("Confidence", "Low", g_thresholds.confidenceLow);
	g_thresholds.confidenceMedium = ini->ReadFloat("Confidence", "Medium", g_thresholds.confidenceMedium);
	g_thresholds.confidenceHigh = ini->ReadFloat("Confidence", "High", g_thresholds.confidenceHigh);
	g_thresholds.minLapsForReproducibility = ini->ReadInteger("Confidence", "MinLapsForReproducibility", g_thresholds.minLapsForReproducibility);
}

void loadThresholds()
{
	String path = ThresholdsFilePath();
	if (!FileExists(path)) {
		saveDefaultThresholds();
		return;
	}

	TIniFile* ini = new TIniFile(path);
	try {
		loadThresholdsPart1(ini);
		loadThresholdsPart2(ini);
		loadSymptomWeights(ini);
	} __finally {
		delete ini;
	}
}


void UpdateProgress(int percent, const String& message)
{
	static int lastPercent = -1;
	if (percent == lastPercent)
		return;
	lastPercent = percent;

	if (FormMotecSetup == NULL)
		return;

	FormMotecSetup->ProgressBarExport->Position = percent;
	//FormMotecSetup->StatusBar1->SimpleText = message + " " + IntToStr(percent) + "%";
	FormMotecSetup->ProgressBarExport->Update();
	//FormMotecSetup->StatusBar1->Update();

	if (percent % 5 == 0)
		Application->ProcessMessages();
}

void AnalysisProgress(int percent)
{
	if (percent < 50)
		UpdateProgress(percent, "Analyzing data...");
	else
		UpdateProgress(percent, "Building lap summary...");
}

 void DetectMotecInstallation(void);

__fastcall TFormMotecSetup::TFormMotecSetup(TComponent* Owner)
    : TForm(Owner)
{
	// Initialize GDI+ subsystem immediately to prevent access violations in Paint events
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	// Load rule configuration from MotecDataAnalyser.rules.json (optional).
	// Do this BEFORE loadThresholds() so the [SymptomWeights] INI values can
	// override any weights that came from the JSON file.
	loadRuleConfig();
    loadChannelMappingConfig();

	// Load analysis thresholds from MotecDataAnalyser.thresholds.ini
	// (creates the file with defaults if it does not exist yet).
	loadThresholds();

	if (LanguageCombo != NULL) {
		LanguageCombo->Items->Clear();
		LanguageCombo->Items->Add("English");
		LanguageCombo->Items->Add("Français");
		LanguageCombo->ItemIndex = (int)ReadReportLanguageSetting();
		LanguageCombo->OnChange = LanguageComboChange;
	}

	if (ChannelProfileCombo != NULL) {
		ChannelProfileCombo->Items->Clear();
		ChannelProfileCombo->Items->Add("Evo");
		ChannelProfileCombo->Items->Add("AMS2");
		ChannelProfileCombo->ItemIndex = (int)ReadChannelProfileSetting();
		ChannelProfileCombo->OnChange = ChannelProfileComboChange;
    }
	TranslateUI();
}

void __fastcall TFormMotecSetup::TranslateUI()
{
    ReportLanguage lang = ReadReportLanguageSetting();
    bool isFr = (lang == ReportLanguage::French);

    this->Caption = isFr ? "AC EVO - Analyseur de Setup MoTeC" : "AC EVO - MoTeC Setup Analyzer";
    LabelTitle->Caption = isFr ? "Analyseur de Setup MoTeC" : "MoTeC Setup Analyzer";
    LabelSubtitle->Caption = isFr ? "Analyser les données de télémétrie MoTeC" : "Analyze MoTeC telemetry data";
    LabelProfile->Caption = isFr ? "Profil :" : "Profile:";
    LabelLanguage->Caption = isFr ? "Langue :" : "Language:";
    ButtonLoad->Caption = isFr ? "Charger CSV MoTeC" : "Load MoTeC CSV";
    Button1->Caption = isFr ? "Importer Données Motec" : "Import Motec Data";
    ButtonSettings->Caption = isFr ? "Réglages" : "Settings";
    LiveCaptureCheckBox->Caption = isFr ? "Capture Live" : "Live Capture";

    TabSummary->Caption = isFr ? "Résumé" : "Summary";
    TabGeneral->Caption = isFr ? "Général" : "General";
    TabTyre->Caption = isFr ? "Pneus" : "Tyre";
    TabBrake->Caption = isFr ? "Freins" : "Brake";
    TabSuspension->Caption = isFr ? "Suspensions" : "Suspension";
    TabEngineCooling->Caption = isFr ? "Moteur & Refroidissement" : "Engine & Cooling";
    TabHandling->Caption = isFr ? "Comportement" : "Handling";

    // Re-apply ● indicator for tabs that have content (preserves dots across language switch)
    auto applyDot = [](TTabSheet* tab, TMemo* memo) {
        if (tab == NULL || memo == NULL) return;
        if (!memo->Text.IsEmpty())
            tab->Caption = String((System::WideChar)0x25CF) + " " + tab->Caption;
    };
    applyDot(TabGeneral, MemoGeneral);
    applyDot(TabTyre, MemoTyre);
    applyDot(TabBrake, MemoBrake);
    applyDot(TabSuspension, MemoSuspension);
    applyDot(TabEngineCooling, MemoEngineCooling);
    applyDot(TabHandling, MemoHandling);

    // Combo boxes items are usually set in DFM or constructor, 
    // but we can update them here if they were populated.

	OnResize = FormResize;
	FormResize(this);
	MemoSummary->Lines->Add("Select a Motec file to analyze.");
	// DetectMotecInstallation();
}

void __fastcall TFormMotecSetup::ButtonSettingsClick(TObject *Sender)
{
	TFormMotecSettings *settingsForm = new TFormMotecSettings(this);
	try {
		if (settingsForm->ShowModal() == mrOk) {
			// Thresholds are already updated in g_thresholds and the .ini file 
			// by ButtonSaveClick in MotecSettingsForm.cpp
		}
	} __finally {
		delete settingsForm;
	}
}

void __fastcall TFormMotecSetup::LanguageComboChange(TObject *Sender)
{
	if (LanguageCombo == NULL)
		return;

	ReportLanguage language = (ReportLanguage)LanguageCombo->ItemIndex;
	SaveReportLanguageSetting(language);
	TranslateUI();
	MemoSummary->Lines->Add(language == ReportLanguage::French ? "Langue changée en français." : "Language changed to English.");
}

void __fastcall TFormMotecSetup::ChannelProfileComboChange(TObject *Sender)
{
	if (ChannelProfileCombo == NULL)
		return;

	ChannelProfile profile = (ChannelProfile)ChannelProfileCombo->ItemIndex;
	SaveChannelProfileSetting(profile);
	MemoSummary->Lines->Add("Channel profile changed to " + String(profile == ChannelProfile::EvoM1 ? "Evo" : "AMS2") + ".");

    // Reactive Live Capture logic: If live is active, reconnect to the new profile immediately.
    if (LiveCaptureCheckBox != NULL && LiveCaptureCheckBox->Checked) {
        liveActive = true;
        liveConnected = false;
        liveInPit = true;
        liveLapInProgress = false;
        liveLaps.clear();
        liveLapNumber = 0;
        sessionLapCounter = 0;
        lastCompletedLaps = 0;
        liveCurrentLap = LapStats();
        liveWasOnTrack = false;

        // Update mode based on the newly selected profile
        liveMode = (profile == ChannelProfile::EvoM1) ? LIVE_EVO : LIVE_AMS2;

        // LED red = not connected yet (reconnecting)
        LiveLed->Brush->Color = clRed;
        LabelLiveStatus->Caption = "Connecting...";

        // Start the connection timer to begin looking for shared memory/process
        LiveConnectTimer->Enabled = true;
    }
}


void __fastcall TFormMotecSetup::FormResize(TObject *Sender)
{
    if (PageControlReport == NULL)
        return;

    const int top = 170; // Clear progress bar (ends at 162)
    const int gap = 10;
    const int tireBoxWidth = 280;
    const int tireBoxHeight = 310;

    // 1. Tire Temp Box - Positioned on the Left
    if (TireTempPaintBox != NULL) {
        TireTempPaintBox->Left = 20;
        TireTempPaintBox->Top = top;
        TireTempPaintBox->Width = tireBoxWidth;
        TireTempPaintBox->Height = tireBoxHeight;
    }

    // 1.5 Brake Temp Box - Positioned between Tire Temps and Dampers
    if (BrakeTempPaintBox != NULL) {
        int areaLeft = tireBoxWidth + 30;
        int brakeBoxWidth = 300; // Wide enough for brake discs + water/oil gauges on the right
        int brakeBoxHeight = tireBoxHeight;

        BrakeTempPaintBox->Left = areaLeft;
        BrakeTempPaintBox->Top = top;
        BrakeTempPaintBox->Width = brakeBoxWidth;
        BrakeTempPaintBox->Height = brakeBoxHeight;

        // Shift Damper Histograms to the right of Brake Temp Box
        int damperAreaLeft = areaLeft + brakeBoxWidth + 10;
        int availableDamperWidth = ClientWidth - damperAreaLeft - 20;
        int chartWidth = (availableDamperWidth - gap) / 2;
        int chartHeight = (tireBoxHeight - gap) / 2;

        if (DamperFLPaintBox != NULL && DamperFRPaintBox != NULL && DamperRLPaintBox != NULL && DamperRRPaintBox != NULL) {
            int areaTopDampers = top;

            DamperFLPaintBox->Left = damperAreaLeft;
            DamperFLPaintBox->Top = areaTopDampers;
            DamperFLPaintBox->Width = chartWidth;
            DamperFLPaintBox->Height = chartHeight;

            DamperFRPaintBox->Left = damperAreaLeft + chartWidth + gap;
            DamperFRPaintBox->Top = areaTopDampers;
            DamperFRPaintBox->Width = chartWidth;
            DamperFRPaintBox->Height = chartHeight;

            DamperRLPaintBox->Left = damperAreaLeft;
            DamperRLPaintBox->Top = areaTopDampers + chartHeight + gap;
            DamperRLPaintBox->Width = chartWidth;
            DamperRLPaintBox->Height = chartHeight;

            DamperRRPaintBox->Left = damperAreaLeft + chartWidth + gap;
            DamperRRPaintBox->Top = areaTopDampers + chartHeight + gap;
            DamperRRPaintBox->Width = chartWidth;
            DamperRRPaintBox->Height = chartHeight;
        }
    } else {
        // Fallback if BrakeTempPaintBox is missing: use original layout
        if (DamperFLPaintBox != NULL && DamperFRPaintBox != NULL && DamperRLPaintBox != NULL && DamperRRPaintBox != NULL) {
            int areaLeft = tireBoxWidth + 30;
            int areaTop = top;
            int availableWidth = ClientWidth - areaLeft - 20;
            int chartWidth = (availableWidth - gap) / 2;
            int chartHeight = (tireBoxHeight - gap) / 2;

            DamperFLPaintBox->Left = areaLeft;
            DamperFLPaintBox->Top = areaTop;
            DamperFLPaintBox->Width = chartWidth;
            DamperFLPaintBox->Height = chartHeight;

            DamperFRPaintBox->Left = areaLeft + chartWidth + gap;
            DamperFRPaintBox->Top = areaTop;
            DamperFRPaintBox->Width = chartWidth;
            DamperFRPaintBox->Height = chartHeight;

            DamperRLPaintBox->Left = areaLeft;
            DamperRLPaintBox->Top = areaTop + chartHeight + gap;
            DamperRLPaintBox->Width = chartWidth;
            DamperRLPaintBox->Height = chartHeight;

            DamperRRPaintBox->Left = areaLeft + chartWidth + gap;
            DamperRRPaintBox->Top = areaTop + chartHeight + gap;
            DamperRRPaintBox->Width = chartWidth;
            DamperRRPaintBox->Height = chartHeight;
        }
    }

    // 3. Report Area - Positioned Below the Panels, spanning full width
    PageControlReport->Left = 20;
    PageControlReport->Top = top + tireBoxHeight + 20;
    PageControlReport->Width = ClientWidth - 40;
    PageControlReport->Height = ClientHeight - PageControlReport->Top - 40;
}

// Remove this method entirely as it is no longer used
void __fastcall TFormMotecSetup::BrakeTempPaintBoxPaint(TObject *Sender)
{
    if (BrakeTempPaintBox == NULL) return;

    TCanvas* canvas = BrakeTempPaintBox->Canvas;
    int width = BrakeTempPaintBox->Width;
    int height = BrakeTempPaintBox->Height;

    // Initialize GDI+ Graphics from VCL Canvas
    Gdiplus::Graphics graphics(canvas->Handle);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 1. Background: Deep Charcoal
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 26, 26, 26));
    graphics.FillRectangle(&bgBrush, 0, 0, width, height);

    if (!hasBrakeTempData) {
        // Draw placeholder text using GDI+ (avoids mixing VCL canvas after Graphics init).
        Gdiplus::Font phFont(L"Segoe UI", 10.0f);
        Gdiplus::SolidBrush phTextBrush(Gdiplus::Color(255, 180, 180, 180)); // Light grey on charcoal
        Gdiplus::StringFormat phFmt;
        phFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        phFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect(0.0f, 0.0f, (Gdiplus::REAL)width, (Gdiplus::REAL)height);
        graphics.DrawString(L"No brake data", -1, &phFont, layoutRect, &phFmt, &phTextBrush);
        return;
    }

    // Layout constants. Box is ~240 wide x 310 tall (set in FormResize).
    const int iconSize = 46;      // disc circle diameter
    const int colGap   = 18;      // horizontal gap between the two columns
    const int leftPad  = 20;      // left margin before first column

    // Vertical budget per row: label(20) + gap + caliper/rotor(46) + Avg/Max lines(~32).
    // We give each row a fixed pitch so temps of one row never collide with the next.
    const int labelH   = 20;      // corner-label height above its disc (extra clearance for caliper)
    const int tempBlock= 34;      // space below disc for Avg + Max lines
    const int rowPitch = labelH + iconSize + tempBlock + 30; // ~136px per row (incl. inter-row gap)

    int colW   = iconSize + colGap;        // pitch per column
    int topY0  = 48;                       // y of first row's disc (leaves room for label above caliper)
    int topY1  = topY0 + rowPitch;         // y of second row's disc

    struct IconPos { int x, y; String label; };
    IconPos pos[4] = {
        { leftPad,          topY0, "FL" }, // 0: FL (top-left)
        { leftPad + colW,   topY0, "FR" }, // 1: FR (top-right)
        { leftPad,          topY1, "RL" }, // 2: RL (bottom-left)
        { leftPad + colW,   topY1, "RR" }  // 3: RR (bottom-right)
    };

    // --- Disk-brake rendering brushes/pens (shared across the four corners) ---
    Gdiplus::SolidBrush rotorFill(Gdiplus::Color(255, 96, 100, 108));   // steel-grey disc face
    Gdiplus::Pen       rimPen     (Gdiplus::Color(255, 175, 180, 188), 2.0f); // bright outer edge
    Gdiplus::Pen       groovePen  (Gdiplus::Color(90, 60, 63, 68),      1.0f); // faint concentric grooves
    Gdiplus::SolidBrush hubFill   (Gdiplus::Color(255, 70, 74, 80));     // darker central hub
    Gdiplus::Pen       hubPen     (Gdiplus::Color(255, 130, 134, 140), 1.0f);
    Gdiplus::SolidBrush boltFill  (Gdiplus::Color(255, 200, 204, 210)); // small hub bolts
    Gdiplus::SolidBrush caliperFill(Gdiplus::Color(235, 214, 68, 52));   // red brake caliper

    // Draw each disc with its corner label ABOVE it and Avg/Max temps BELOW it.
    Gdiplus::Font labelFont(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 230, 230, 230)); // Off-white corner name
    Gdiplus::Font tempFont(L"Segoe UI", 8.0f);
    Gdiplus::SolidBrush avgBrush(Gdiplus::Color(255, 100, 200, 255));   // Light Blue for Avg
    Gdiplus::SolidBrush maxBrush(Gdiplus::Color(255, 255, 130, 130));   // Soft Red for Max

    Gdiplus::StringFormat centerFmt;
    centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

    for (int i = 0; i < 4; ++i) {
        // Corner label ABOVE the disc+caliper, centered on it. Sits well clear of the red caliper.
        Gdiplus::RectF labelRect((Gdiplus::REAL)(pos[i].x - 12), (Gdiplus::REAL)(pos[i].y - labelH - iconSize * 0.34f + 2), (Gdiplus::REAL)(iconSize + 24), labelH);
        graphics.DrawString(pos[i].label.c_str(), -1, &labelFont, labelRect, &centerFmt, &labelBrush);

        // --- Draw a disk brake: rotor + grooves + hub/bolts + caliper ---
        float dx = (float)pos[i].x;
        float dy = (float)pos[i].y;
        float d  = (float)iconSize;
        float cx = dx + d / 2.0f;   // disc centre x
        float cy = dy + d / 2.0f;   // disc centre y

        // Rotor face (filled steel-grey disc).
        graphics.FillEllipse(&rotorFill, dx, dy, d, d);
        // Bright outer rim to define the edge.
        graphics.DrawEllipse(&rimPen, dx, dy, d, d);

        // Concentric grooves suggesting a slotted/drilled rotor face (3 rings).
        for (int g = 1; g <= 3; ++g) {
            float inset = d * (0.28f + 0.16f * g);   // shrink inward each ring
            graphics.DrawEllipse(&groovePen, cx - inset / 2.0f, cy - inset / 2.0f, inset, inset);
        }

        // Central hub with a few bolt dots around it (wheel/rotor centre).
        float hubD = d * 0.34f;
        graphics.FillEllipse(&hubFill, cx - hubD / 2.0f, cy - hubD / 2.0f, hubD, hubD);
        graphics.DrawEllipse(&hubPen,   cx - hubD / 2.0f, cy - hubD / 2.0f, hubD, hubD);

        float boltOrbit = d * 0.15f;                 // radius of the bolt circle
        float boltR     = d * 0.035f;                // each bolt dot diameter
        const double PI = 3.14159265358979323846;
        for (int b = 0; b < 4; ++b) {
            double ang = PI / 2.0 + (PI / 2.0) * b; // N, E, S, W
            float bx = cx + (float)(boltOrbit * std::cos(ang)) - boltR / 2.0f;
            float by = cy + (float)(boltOrbit * std::sin(ang)) - boltR / 2.0f;
            graphics.FillEllipse(&boltFill, bx, by, boltR, boltR);
        }

        // Caliper: a red block straddling the top of the disc. Kept in a tight band just above
        // the rim so it never reaches up into the corner-label zone drawn higher up.
        float calW = d * 0.58f;
        float calH = d * 0.26f;
        Gdiplus::RectF caliper(dx + (d - calW) / 2.0f, dy - calH * 0.30f, calW, calH);
        graphics.FillRectangle(&caliperFill, caliper);

        // Avg / Max temps BELOW the disc, centered on it. Build wide buffers via swprintf.
        wchar_t avgBuf[32];
        wchar_t maxBuf[32];
        swprintf(avgBuf, 32, L"Avg: %.0fC", brakeTemps[i].avg);
        swprintf(maxBuf, 32, L"Max: %.0fC", brakeTemps[i].max);

        Gdiplus::RectF avgRect((Gdiplus::REAL)(pos[i].x - 16), (Gdiplus::REAL)(pos[i].y + iconSize + 3), (Gdiplus::REAL)(iconSize + 32), 15);
        graphics.DrawString(avgBuf, -1, &tempFont, avgRect, &centerFmt, &avgBrush);

        Gdiplus::RectF maxRect((Gdiplus::REAL)(pos[i].x - 16), (Gdiplus::REAL)(pos[i].y + iconSize + 19), (Gdiplus::REAL)(iconSize + 32), 15);
        graphics.DrawString(maxBuf, -1, &tempFont, maxRect, &centerFmt, &maxBrush);
    }

    // --- Temperature Gauge rendering (Water & Oil) in third column ---
    const int gaugeX = leftPad + 2 * (iconSize + colGap) + 30; // x of gauge column (pushed to the right)
    const float gaugeD = 46.0f;                            // same diameter as brake discs

    auto drawTempGauge = [&](float gx, float gy, double tempValue, bool dataValid, double warnThreshold, const wchar_t* caption) {
        float cx = gx + gaugeD / 2.0f;
        float cy = gy + gaugeD / 2.0f;

        // Gauge background circle (dark)
        Gdiplus::SolidBrush gaugeBg(Gdiplus::Color(255, 40, 42, 48));
        graphics.FillEllipse(&gaugeBg, gx, gy, gaugeD, gaugeD);
        Gdiplus::Pen gaugeRim(Gdiplus::Color(255, 100, 104, 112), 1.5f);
        graphics.DrawEllipse(&gaugeRim, gx, gy, gaugeD, gaugeD);

        // Colored arc segments (blue -> yellow -> red) over top half
        const int segCount = 24;
        float arcInset = gaugeD * 0.12f;
        float arcX = gx + arcInset;
        float arcY = gy + arcInset;
        float arcW = gaugeD - 2.0f * arcInset;

        for (int s = 0; s < segCount; ++s) {
            float t = (float)s / (segCount - 1);
            int r, g, b;
            if (t < 0.5f) {
                float f = t * 2.0f;
                r = (int)(60 + f * 195);   // 60 -> 255
                g = (int)(140 + f * 80);   // 140 -> 220
                b = (int)(255 - f * 195);  // 255 -> 60
            } else {
                float f = (t - 0.5f) * 2.0f;
                r = 255;
                g = (int)(220 - f * 150);   // 220 -> 70
                b = (int)(60 - f * 10);     // 60 -> 50
            }
            float startAng = 180.0f + t * 180.0f / segCount;
            float sweepAng = 180.0f / segCount + 0.5f;
            Gdiplus::Pen segPen(Gdiplus::Color(255, r, g, b), 3.0f);
            graphics.DrawArc(&segPen, arcX, arcY, arcW, arcW, startAng, sweepAng);
        }

        // Red warning zone: ~30° arc starting at the threshold temperature
        {
            const double tMin = 40.0, tMax = 150.0;
            float threshFrac = (float)((warnThreshold - tMin) / (tMax - tMin));
            if (threshFrac < 0.0f) threshFrac = 0.0f;
            if (threshFrac > 1.0f) threshFrac = 1.0f;
            float warnStart = 180.0f + threshFrac * 180.0f;
            float warnSweep = 30.0f;
            if (warnStart + warnSweep > 360.0f) warnSweep = 360.0f - warnStart;
            Gdiplus::Pen warnPen(Gdiplus::Color(255, 220, 40, 40), 5.0f);
            graphics.DrawArc(&warnPen, arcX, arcY, arcW, arcW, warnStart, warnSweep);
        }

        // Needle
        if (dataValid) {
            const double tMin = 40.0, tMax = 150.0;
            double clamped = tempValue < tMin ? tMin : (tempValue > tMax ? tMax : tempValue);
            float frac = (float)((clamped - tMin) / (tMax - tMin));
            float needleAngRad = (float)(3.14159265 * (1.0 - frac)); // PI(left) to 0(right)
            float needleLen = gaugeD * 0.38f;
            float nx = cx + needleLen * cosf(needleAngRad);
            float ny = cy - needleLen * sinf(needleAngRad);
            // Needle turns red when the temperature exceeds the warning threshold
            Gdiplus::Color needleCol = (tempValue > warnThreshold)
                ? Gdiplus::Color(255, 230, 60, 50)   // alert red
                : Gdiplus::Color(255, 240, 240, 240); // normal white
            Gdiplus::Pen needlePen(needleCol, 2.0f);
            graphics.DrawLine(&needlePen, cx, cy, nx, ny);
        } else {
            // No data: short grey needle pointing down
            Gdiplus::Pen needlePen(Gdiplus::Color(255, 120, 120, 120), 2.0f);
            graphics.DrawLine(&needlePen, cx, cy, cx, cy + gaugeD * 0.25f);
        }

        // Center hub dot
        Gdiplus::SolidBrush hubDot(Gdiplus::Color(255, 200, 204, 210));
        float hubR = 3.0f;
        graphics.FillEllipse(&hubDot, cx - hubR, cy - hubR, hubR * 2.0f, hubR * 2.0f);

        // Caption below gauge
        Gdiplus::Font capFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush capBrush(Gdiplus::Color(255, 230, 230, 230));
        Gdiplus::RectF capRect(gx - 6.0f, gy + gaugeD + 2.0f, gaugeD + 12.0f, 14.0f);
        graphics.DrawString(caption, -1, &capFont, capRect, &centerFmt, &capBrush);

        // Value text
        wchar_t valBuf[16];
        Gdiplus::RectF valRect(gx - 6.0f, gy + gaugeD + 17.0f, gaugeD + 12.0f, 14.0f);
        if (dataValid) {
            swprintf(valBuf, 16, L"%.0fC", tempValue);
            Gdiplus::SolidBrush valBrush(Gdiplus::Color(255, 100, 200, 255));
            graphics.DrawString(valBuf, -1, &tempFont, valRect, &centerFmt, &valBrush);
        } else {
            Gdiplus::SolidBrush noDataBrush(Gdiplus::Color(255, 140, 140, 140));
            graphics.DrawString(L"--", -1, &tempFont, valRect, &centerFmt, &noDataBrush);
        }
    };

    // Water gauge (front row) and Oil gauge (rear row)
    // Needle turns red above the configured warning thresholds
    drawTempGauge((float)gaugeX, (float)topY0, waterTemp.avg, waterTemp.valid, g_thresholds.waterTempMax, L"Water");
    drawTempGauge((float)gaugeX, (float)topY1, oilTemp.avg, oilTemp.valid, g_thresholds.oilTempMax, L"Oil");
}

void __fastcall TFormMotecSetup::DrawModernHistogram(TCanvas* canvas, int x, int y, int width, int height, const SuspensionCornerData& stats, const String& label)
{
    if (!canvas) return;

    // Initialize GDI+ Graphics from VCL Canvas
    Gdiplus::Graphics graphics(canvas->Handle);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 1. Background: Deep Charcoal
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 26, 26, 26));
    graphics.FillRectangle(&bgBrush, 0, 0, width, height);

    int marginLeft = 35;
    int marginBottom = 20;
    int chartWidth = width - marginLeft;
    int chartHeight = height - marginBottom;

    // Highlight Slow Damper Section (Symmetrical range around 0)
    double slowRange = g_thresholds.damperSlowSpeedRange;
    if (slowRange > 0) {
        float xCenter = marginLeft + chartWidth / 2.0f;
        float pixelsPerUnit = chartWidth / 400.0f; // Total range is -200 to 200
        float slowHalfWidth = (float)(slowRange * pixelsPerUnit);

        Gdiplus::SolidBrush slowBgBrush(Gdiplus::Color(100, 60, 60, 100)); // Enhanced contrast for slow damper section
        graphics.FillRectangle(&slowBgBrush, Gdiplus::RectF(xCenter - slowHalfWidth, 0, slowHalfWidth * 2, (float)height));
    }

    // 2. Grid Lines (Subtle Dark Grey)
    Gdiplus::Pen gridPen(Gdiplus::Color(80, 60, 60, 60), 1.0f);
    
    // Vertical center line (0 velocity)
    graphics.DrawLine(&gridPen, marginLeft + chartWidth / 2, 0, marginLeft + chartWidth / 2, chartHeight);
    
    // Horizontal grid lines (e.g., every 25% of height)
    for (int i = 1; i < 4; ++i) {
        int gy = (chartHeight * i) / 4;
        graphics.DrawLine(&gridPen, marginLeft, gy, width, gy);
    }

    // 3. Draw Bars
    // Use the shared Y-axis maximum so all four histograms share the same scale.
    double maxBin = suspensionMaxBin;

    if (maxBin > 0) {
        // Apply user-defined Y offset as a PERCENTAGE headroom above the tallest bin.
        // histogramYOffset is stored in percent units (e.g., 5 => +5%), so scale it by maxBin;
        // adding it absolutely would dwarf fractional bin values and make every bar ~invisible.
        double displayMax = maxBin * (1.0 + g_thresholds.histogramYOffset / 100.0);

        // Gradient for bars: Electric Blue to Darker Blue
        Gdiplus::LinearGradientBrush barBrush(
            Gdiplus::Rect(0, 0, width, height),
            Gdiplus::Color(255, 0, 180, 255), // Top: Bright Cyan/Blue
            Gdiplus::Color(255, 0, 60, 120),   // Bottom: Deep Blue
            Gdiplus::LinearGradientModeVertical
        );

        float binWidth = (float)chartWidth / 41.0f;
        float scaleY = (float)chartHeight / displayMax;

        for (int i = 0; i < VelocityHistogram::kBinCount; ++i) {
            float barHeight = stats.bins[i] * scaleY;
            float bx = marginLeft + i * binWidth;
            float by = chartHeight - barHeight;
            
            graphics.FillRectangle(&barBrush, bx, by, binWidth, barHeight);
        }
    }

    // 4. Labels & Axis
    Gdiplus::Font font(L"Segoe UI", 8, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 180, 180, 180));

    // Corner Label (e.g., "FL")
    graphics.DrawString(label.c_str(), -1, &font, Gdiplus::PointF(5, 5), &textBrush);
    
    // Y-Axis Labels
    if (maxBin > 0) {
        wchar_t labelBuf[32];
        
        // Top Label (Max)
        
        for (int i = 1; i < 4; ++i) {
            int gy = (chartHeight * i) / 4;
            double val = maxBin * (1.0 - (i / 4.0)) * 100.0; // bins are fractions -> percent
            swprintf(labelBuf, 32, L"%.1f%%", val);
            graphics.DrawString(labelBuf, -1, &font, Gdiplus::PointF(5, gy - 7), &textBrush);
        }
        graphics.DrawString(L"0.0%", -1, &font, Gdiplus::PointF(5, chartHeight - 7), &textBrush);
    }

    // X-Axis Labels (Every 50 mm/s within -200 to +200 range)
    for (int val = -200; val <= 200; val += 50) {
        wchar_t xLabelBuf[16];
        swprintf(xLabelBuf, 16, L"%d", val);
        float xPos = marginLeft + ((val + 205.0f) / 410.0f) * chartWidth;
        Gdiplus::RectF labelBounds;
        graphics.MeasureString(xLabelBuf, -1, &font, Gdiplus::PointF(0, 0), &labelBounds);
        float labelX = xPos - labelBounds.Width / 2.0f;
        labelX = std::max(0.0f, std::min(labelX, width - labelBounds.Width));
        graphics.DrawString(xLabelBuf, -1, &font, Gdiplus::PointF(labelX, height - 15), &textBrush);
    }
}

void __fastcall TFormMotecSetup::DamperFLPaintBoxPaint(TObject *Sender) {
    TPaintBox* pb = (TPaintBox*)Sender;
    DrawModernHistogram(pb->Canvas, pb->Left, pb->Top, pb->Width, pb->Height, suspensionData[0], "Front Left");
}

void __fastcall TFormMotecSetup::DamperFRPaintBoxPaint(TObject *Sender) {
    TPaintBox* pb = (TPaintBox*)Sender;
    DrawModernHistogram(pb->Canvas, pb->Left, pb->Top, pb->Width, pb->Height, suspensionData[1], "Front Right");
}

void __fastcall TFormMotecSetup::DamperRLPaintBoxPaint(TObject *Sender) {
    TPaintBox* pb = (TPaintBox*)Sender;
    DrawModernHistogram(pb->Canvas, pb->Left, pb->Top, pb->Width, pb->Height, suspensionData[2], "Rear Left");
}

void __fastcall TFormMotecSetup::DamperRRPaintBoxPaint(TObject *Sender) {
    TPaintBox* pb = (TPaintBox*)Sender;
    DrawModernHistogram(pb->Canvas, pb->Left, pb->Top, pb->Width, pb->Height, suspensionData[3], "Rear Right");
}

void __fastcall TFormMotecSetup::resetAnalysisGraphics()
{
    for (int i = 0; i < 4; ++i) {
        tireTemps[i].inner = 0.0;
        tireTemps[i].middle = 0.0;
        tireTemps[i].outer = 0.0;
        tireTemps[i].pressure = 0.0;
        tireTemps[i].pressureValid = false;
        tireTemps[i].valid = false;

        suspensionData[i].travel = 0.0;
        suspensionData[i].bumpstop = 0.0;
        suspensionData[i].issue = false;
        suspensionData[i].valid = false;
        for (int b = 0; b < VelocityHistogram::kBinCount; ++b)
            suspensionData[i].bins[b] = 0.0;
    }

    suspensionMaxBin = 0.0;

    hasTireTempData = false;
    aeroBalanceValue = 0.0;
    aeroBalanceValid = false;

    for (int i = 0; i < 4; ++i) {
        brakeTemps[i].avg = 0.0;
        brakeTemps[i].max = 0.0;
        brakeTemps[i].valid = false;
    }
    hasBrakeTempData = false;

    if (TireTempPaintBox != NULL)
        TireTempPaintBox->Invalidate();
    if (BrakeTempPaintBox != NULL)
        BrakeTempPaintBox->Invalidate();
}

void __fastcall TFormMotecSetup::UpdateTireTemps(const std::vector<LapStats>& laps)
{
    if (laps.empty()) {
        hasTireTempData = false;
        if (TireTempPaintBox != NULL)
            TireTempPaintBox->Invalidate();
        return;
    }

    LapStats avg = buildAverageLapData(laps);

    tireTemps[0].inner  = avg.tyreInnerTempLF.average();
    tireTemps[0].middle = avg.tyreCentreTempLF.average();
    tireTemps[0].outer  = avg.tyreOuterTempLF.average();
    tireTemps[0].valid  = (avg.tyreInnerTempLF.count > 0 && avg.tyreCentreTempLF.count > 0 && avg.tyreOuterTempLF.count > 0);
    tireTemps[0].pressure = avg.pressureLF.average();
    tireTemps[0].pressureValid = (avg.pressureLF.count > 0);

    tireTemps[1].inner  = avg.tyreInnerTempRF.average();
    tireTemps[1].middle = avg.tyreCentreTempRF.average();
    tireTemps[1].outer  = avg.tyreOuterTempRF.average();
    tireTemps[1].valid  = (avg.tyreInnerTempRF.count > 0 && avg.tyreCentreTempRF.count > 0 && avg.tyreOuterTempRF.count > 0);
    tireTemps[1].pressure = avg.pressureRF.average();
    tireTemps[1].pressureValid = (avg.pressureRF.count > 0);

    tireTemps[2].inner  = avg.tyreInnerTempLR.average();
    tireTemps[2].middle = avg.tyreCentreTempLR.average();
    tireTemps[2].outer  = avg.tyreOuterTempLR.average();
    tireTemps[2].valid  = (avg.tyreInnerTempLR.count > 0 && avg.tyreCentreTempLR.count > 0 && avg.tyreOuterTempLR.count > 0);
    tireTemps[2].pressure = avg.pressureLR.average();
    tireTemps[2].pressureValid = (avg.pressureLR.count > 0);

    tireTemps[3].inner  = avg.tyreInnerTempRR.average();
    tireTemps[3].middle = avg.tyreCentreTempRR.average();
    tireTemps[3].outer  = avg.tyreOuterTempRR.average();
    tireTemps[3].valid  = (avg.tyreInnerTempRR.count > 0 && avg.tyreCentreTempRR.count > 0 && avg.tyreOuterTempRR.count > 0);
    tireTemps[3].pressure = avg.pressureRR.average();
    tireTemps[3].pressureValid = (avg.pressureRR.count > 0);

    aeroBalanceValue = avg.aeroBalance.average();
    aeroBalanceValid = (avg.aeroBalance.count > 0);

    hasTireTempData = tireTemps[0].valid || tireTemps[1].valid || tireTemps[2].valid || tireTemps[3].valid;
    if (TireTempPaintBox != NULL)
        TireTempPaintBox->Invalidate();
}

void __fastcall TFormMotecSetup::UpdateBrakeTemps(const std::vector<LapStats>& laps)
{
    if (laps.empty()) {
        hasBrakeTempData = false;
        if (BrakeTempPaintBox) BrakeTempPaintBox->Invalidate();
        return;
    }

    // Use the last lap for current brake temperatures as they are per-lap stats
    const LapStats& stats = laps.back();

    for (int i = 0; i < 4; ++i) {
        const StatSummary* summary = nullptr;
        switch (i) {
            case 0: summary = &stats.brakeTempLF; break; // FL
            case 1: summary = &stats.brakeTempRF; break; // FR
            case 2: summary = &stats.brakeTempLR; break; // RL
            case 3: summary = &stats.brakeTempRR; break; // RR
        }

        if (summary && summary->count > 0) {
            // For average, we might want to aggregate over all laps if that's what the user expects,
            // but usually brake temps are shown for the current/last lap or an average.
            // Given the previous implementation used 'stats', I will stick to providing data from the provided vector.
            // However, since it's a vector of laps, we should probably aggregate if we want "average" as requested in legend.
            // But for now, let's just fix the signature and use the last lap to satisfy the linker.
            brakeTemps[i].avg = summary->average(); 
            brakeTemps[i].max = summary->max;
            brakeTemps[i].valid = true;
        } else {
            brakeTemps[i].valid = false;
        }
    }

    // Water & Oil temperatures
    if (stats.waterTemp.count > 0) {
        waterTemp.avg = stats.waterTemp.average();
        waterTemp.max = stats.waterTemp.max;
        waterTemp.valid = true;
    } else {
        waterTemp.valid = false;
    }

    if (stats.oilTemp.count > 0) {
        oilTemp.avg = stats.oilTemp.average();
        oilTemp.max = stats.oilTemp.max;
        oilTemp.valid = true;
    } else {
        oilTemp.valid = false;
    }

    hasBrakeTempData = true;
    if (BrakeTempPaintBox) BrakeTempPaintBox->Invalidate();
}

void __fastcall TFormMotecSetup::TireTempPaintBoxPaint(TObject *Sender)
{
    // Initialize GDI+ Graphics from VCL Canvas
    Gdiplus::Graphics graphics(TireTempPaintBox->Canvas->Handle);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    TCanvas* canvas = TireTempPaintBox->Canvas;
    canvas->Brush->Color = clWhite;
    canvas->FillRect(TRect(0, 0, TireTempPaintBox->Width, TireTempPaintBox->Height));

    if (!hasTireTempData) {
        canvas->Font->Color = clGray;
        canvas->Font->Name = "Segoe UI";
        canvas->Font->Size = 10;
        canvas->Font->Style = TFontStyles();
        String msg = "No tyre temp data";
        int tw = canvas->TextWidth(msg);
        canvas->TextOut((TireTempPaintBox->Width - tw) / 2, TireTempPaintBox->Height / 2 - 10, msg);
        return;
    }

    const String tireNames[4] = { "FL", "FR", "RL", "RR" };
    const int boxW = TireTempPaintBox->Width;
    const int boxH = TireTempPaintBox->Height;
    const int tireW = 70;
    const int tireH = 90;
    const int pressureH = 18;
    const int labelH = 16;
    const int tempH = 14;
    const int contentH = labelH + pressureH + tireH + tempH;
    const int rowGap = 20;
    const int totalBlockH = 2 * contentH + rowGap;
    int startY = 5;
    const int aeroW = 115;
    const int tireAreaW = boxW - aeroW;
    const int cellW = tireAreaW / 2;

    for (int t = 0; t < 4; t++) {
        int col = t % 2;
        int row = t / 2;
        int cellX = col * cellW;
        int cellY = startY + row * (contentH + rowGap);
        int centerX = cellX + cellW / 2;

        // Tire name label
        canvas->Font->Color = clBlack;
        canvas->Font->Name = "Segoe UI";
        canvas->Font->Size = 9;
        TFontStyles boldStyle;
        boldStyle << fsBold;
        canvas->Font->Style = boldStyle;
        String nameStr = tireNames[t];
        int nameW = canvas->TextWidth(nameStr);
        canvas->TextOut(centerX - nameW / 2, cellY + 2, nameStr);

        // Pressure
        int yCursor = cellY + labelH;
        canvas->Font->Size = 8;
        canvas->Font->Style = TFontStyles();
        if (tireTemps[t].pressureValid) {
            String pressStr = FloatToStrF(tireTemps[t].pressure, ffFixed, 5, 1) + " psi";
            int pressW = canvas->TextWidth(pressStr);
            canvas->Font->Color = TColor(RGB(0, 100, 0));
            canvas->TextOut(centerX - pressW / 2, yCursor, pressStr);
        } else {
            canvas->Font->Color = clGray;
            canvas->TextOut(centerX - 5, yCursor, "-");
        }
        yCursor += pressureH;

        // Tire graphic
        int tireX = centerX - tireW / 2;
        int tireY = yCursor;
        bool isLeftSide = (t == 0 || t == 2);

        double inner = tireTemps[t].inner;
        double middle = tireTemps[t].middle;
        double outer = tireTemps[t].outer;

        bool middleBetween = (middle > inner && middle < outer) || (middle < inner && middle > outer);
        TColor middleColor = middleBetween ? TColor(RGB(0, 200, 0)) : TColor(RGB(255, 165, 0));
        TColor highColor = TColor(RGB(0, 128, 0));
        TColor lowColor = TColor(RGB(144, 238, 144));
        TColor innerColor = (inner >= outer) ? highColor : lowColor;
        TColor outerColor = (outer >= inner) ? highColor : lowColor;

        TColor colors[3];
        if (isLeftSide) {
            colors[0] = outerColor;
            colors[1] = middleColor;
            colors[2] = innerColor;
        } else {
            colors[0] = innerColor;
            colors[1] = middleColor;
            colors[2] = outerColor;
        }

        // Draw tire outline
        canvas->Pen->Color = TColor(RGB(80, 80, 80));
        canvas->Pen->Width = 2;
        canvas->Brush->Color = clWhite;
        canvas->Rectangle(tireX, tireY, tireX + tireW, tireY + tireH);
        canvas->Pen->Width = 1;

        // Draw 3 vertical bands inside tire
        int innerPad = 4;
        int bandStartX = tireX + innerPad;
        int bandStartY = tireY + innerPad;
        int bandDrawW = (tireW - 2 * innerPad - 4) / 3;
        int bandDrawH = tireH - 2 * innerPad;

        for (int b = 0; b < 3; b++) {
            int bx = bandStartX + b * (bandDrawW + 2);
            canvas->Brush->Color = colors[b];
            canvas->Pen->Color = TColor(RGB(100, 100, 100));
            canvas->Pen->Width = 1;
            canvas->Rectangle(bx, bandStartY, bx + bandDrawW, bandStartY + bandDrawH);
        }

        // Temperature values below tire
        int tempY = tireY + tireH + 4;
        canvas->Brush->Color = clWhite;
        canvas->Font->Color = clBlack;
        canvas->Font->Size = 8;
        canvas->Font->Style = TFontStyles();
        String tI = FloatToStrF(tireTemps[t].inner, ffFixed, 4, 1) + L"\u00B0";
        String tM = FloatToStrF(tireTemps[t].middle, ffFixed, 4, 1) + L"\u00B0";
        String tO = FloatToStrF(tireTemps[t].outer, ffFixed, 4, 1) + L"\u00B0";

        // Order: Left side (FL/RL) -> Outer, Middle, Inner | Right side (FR/RR) -> Inner, Middle, Outer
        String s1, s2, s3;
        if (isLeftSide) {
            s1 = tO; s2 = tM; s3 = tI;
        } else {
            s1 = tI; s2 = tM; s3 = tO;
        }

        int totalW = canvas->TextWidth(s1) + canvas->TextWidth(s2) + canvas->TextWidth(s3) + 8;
        int tx = centerX - totalW / 2;
        canvas->TextOut(tx, tempY, s1);
        tx += canvas->TextWidth(s1) + 4;
        canvas->TextOut(tx, tempY, s2);
        tx += canvas->TextWidth(s2) + 4;
        canvas->TextOut(tx, tempY, s3);

    }

    // --- Aero balance indicator (car icon + line + triangle) in the right column ---
    if (aeroBalanceValid) {
        int aeroCX = tireAreaW + aeroW / 2;
        int blockMidY = startY + totalBlockH / 2;

        // Title above the indicator
        canvas->Brush->Color = clWhite;
        canvas->Font->Name = "Segoe UI";
        canvas->Font->Size = 9;
        TFontStyles titleBold;
        titleBold << fsBold;
        canvas->Font->Style = titleBold;
        canvas->Font->Color = clBlack;
        String titleStr = "Aero Balance";
        int titleW = canvas->TextWidth(titleStr);
        int titleY = blockMidY - 60;
        canvas->TextOut(aeroCX - titleW / 2, titleY, titleStr);
        canvas->Font->Style = TFontStyles();

        // Draw car as emoji icon
        canvas->Brush->Color = clWhite;
        canvas->Font->Name = "Segoe UI Emoji";
        canvas->Font->Size = 34;
        canvas->Font->Color = clBlack;
        String carIcon = L"\U0001F3CE";
        int iconW = canvas->TextWidth(carIcon);
        int iconH = canvas->TextHeight(carIcon);
        int iconX = aeroCX - iconW / 2;
        int iconY = blockMidY - iconH / 2 - 4;
        canvas->TextOut(iconX, iconY, carIcon);

        // Balance line below car (same width as icon)
        int lineY = iconY + iconH + 4;
        int lineX1 = iconX;
        int lineX2 = iconX + iconW;
        canvas->Pen->Color = clBlack;
        canvas->Pen->Width = 2;
        canvas->MoveTo(lineX1, lineY);
        canvas->LineTo(lineX2, lineY);
        canvas->Pen->Width = 1;

        // Legend: "Front" at left end, "Rear" at right end
        canvas->Brush->Color = clWhite;
        canvas->Font->Name = "Segoe UI";
        canvas->Font->Size = 7;
        canvas->Font->Color = TColor(RGB(100, 100, 100));
        canvas->Font->Style = TFontStyles();
        String frontLbl = "Front";
        String rearLbl = "Rear";
        int frontW = canvas->TextWidth(frontLbl);
        int rearW = canvas->TextWidth(rearLbl);
        int lblY = lineY + 28;
        canvas->TextOut(lineX1 - frontW / 2, lblY, frontLbl);
        canvas->TextOut(lineX2 - rearW / 2, lblY, rearLbl);

        // Triangle marker (position based on aeroBalanceValue: 0%=front, 100%=rear)
        double frac = aeroBalanceValue / 100.0;
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        int triX = lineX1 + (int)(frac * (lineX2 - lineX1));
        TColor markerColor = TColor(RGB(0, 100, 0));

        // Draw triangle using MoveTo/LineTo (pointing down from the line)
        int triSize = 8;
        canvas->Pen->Color = markerColor;
        canvas->Pen->Width = 2;
        canvas->MoveTo(triX - triSize, lineY + 1);
        canvas->LineTo(triX + triSize, lineY + 1);
        canvas->LineTo(triX, lineY + 1 + triSize);
        canvas->LineTo(triX - triSize, lineY + 1);
        canvas->Pen->Width = 1;

        int triBot = lineY + 1 + triSize;

        // Numeric value
        String valStr = FloatToStrF(aeroBalanceValue, ffFixed, 5, 1) + "%";
        canvas->Brush->Color = clWhite;
        canvas->Font->Color = clBlack;
        canvas->Font->Name = "Segoe UI";
        canvas->Font->Size = 9;
        TFontStyles boldStyle;
        boldStyle << fsBold;
        canvas->Font->Style = boldStyle;
        int valW = canvas->TextWidth(valStr);
        canvas->TextOut(triX - valW / 2, triBot + 2, valStr);
        canvas->Font->Style = TFontStyles();
    }
}

// Removed legacy SuspensionPaintBoxPaint implementation as it is replaced by TChart components.



bool __fastcall TFormMotecSetup::AnalyzeSelectedFile(const std::string& filePath, std::string& errorMessage)
{
    resetAnalysisGraphics();
	ButtonLoad->Enabled = false;
	UpdateProgress(0, "Analyzing CSV file...");
	std::vector<LapStats> laps;
	ChannelProfile profile = ChannelProfile::EvoM1;
	if (ChannelProfileCombo != NULL)
		profile = (ChannelProfile)ChannelProfileCombo->ItemIndex;
	if (!parseMotecCsv(filePath, laps, errorMessage, AnalysisProgress, profile)) {
		ButtonLoad->Enabled = true;
		//StatusBar1->SimpleText = "CSV analysis failed";
        return false;
    }
	UpdateProgress(50, "Generating report...");

    ReportLanguage language = (LanguageCombo != NULL) ? (ReportLanguage)LanguageCombo->ItemIndex : ReadReportLanguageSetting();
	ReportResult result = buildAverageLapReport(laps, language, profile);

    UpdateReportTabs(result);
    std::vector<LapStats> validLaps = LapReportGenerator::getValidLaps(laps);
    UpdateTireTemps(validLaps);
    UpdateBrakeTemps(validLaps);
    UpdateSuspension(validLaps);
    if (TabSummary != NULL && TabSummary->Visible)
        PageControlReport->ActivePage = TabSummary;
    ButtonLoad->Enabled = true;
    ProgressBarExport->Position = 100;
	//StatusBar1->SimpleText = "CSV analysis complete";
	return true;
}

bool __fastcall TFormMotecSetup::AnalyzeSelectedLdFile(const std::string& filePath, std::string& errorMessage)
{
    resetAnalysisGraphics();
    Button1->Enabled = false;
    UpdateProgress(0, "Analyzing LD/LDX file...");
    std::vector<LapStats> laps;
    ChannelProfile profile = ChannelProfile::EvoM1;
    if (ChannelProfileCombo != NULL)
        profile = (ChannelProfile)ChannelProfileCombo->ItemIndex;

    if (!parseMotecLd(filePath, laps, errorMessage, AnalysisProgress, profile)) {
        Button1->Enabled = true;
		//StatusBar1->SimpleText = "LD/LDX analysis failed";
		return false;
    }

    UpdateProgress(50, "Generating report...");
    ReportLanguage language = (LanguageCombo != NULL) ? (ReportLanguage)LanguageCombo->ItemIndex : ReadReportLanguageSetting();
	ReportResult result = buildAverageLapReport(laps, language, profile);

    UpdateReportTabs(result);
    std::vector<LapStats> validLaps = LapReportGenerator::getValidLaps(laps);
    UpdateTireTemps(validLaps);
    UpdateBrakeTemps(validLaps);
    UpdateSuspension(validLaps);
    if (TabSummary != NULL && TabSummary->Visible)
        PageControlReport->ActivePage = TabSummary;
    Button1->Enabled = true;
    ProgressBarExport->Position = 100;
	//StatusBar1->SimpleText = "LD/LDX analysis complete";
	return true;
}

void __fastcall TFormMotecSetup::ButtonLoadClick(TObject *Sender)
{
	TOpenDialog* dialog = new TOpenDialog(nullptr);
	try {
		dialog->InitialDir = ReadLastFolder("CsvImport");
		dialog->Filter = L"CSV files (*.csv)|*.csv|All files (*.*)|*.*";
		dialog->Options << ofFileMustExist;

		if (dialog->Execute()) {
			UnicodeString uPath = dialog->FileName;
			UTF8String utf8 = uPath;          // conversion propre
			std::string filePath = utf8.c_str();
			SaveLastFolder("CsvImport", ExtractFileDir(uPath));
			//MemoReport->Lines->Add("PATH = " + String(filePath.c_str()));
			std::string errorMessage;
			if (!AnalyzeSelectedFile(filePath, errorMessage))
				MemoSummary->Text = "Analysis failed.\n" + String(errorMessage.c_str());
		}

	}
	__finally {
		delete dialog;
	}
}


void __fastcall TFormMotecSetup::Button1Click(TObject *Sender)
{
	TOpenDialog* dialog = new TOpenDialog(nullptr);
	try {
		ChannelProfile profile = ChannelProfile::EvoM1;
		if (ChannelProfileCombo != NULL)
			profile = (ChannelProfile)ChannelProfileCombo->ItemIndex;
		String section = LdSectionForProfile(profile);
		dialog->InitialDir = ReadLastFolder(section, "LdImport");
		dialog->Filter = L"LD/LDX files (*.ld)|*.ld|All files (*.*)|*.*";
		dialog->Options << ofFileMustExist;

		if (dialog->Execute()) {
			UnicodeString uPath = dialog->FileName;
			UTF8String utf8 = uPath;
			std::string filePath = utf8.c_str();
			SaveLastFolder(section, ExtractFileDir(uPath));
			std::string errorMessage;
			if (!AnalyzeSelectedLdFile(filePath, errorMessage))
				MemoSummary->Text = "LD/LDX analysis failed.\n" + String(errorMessage.c_str());
		}

	}
	__finally {
		delete dialog;
	}

}

//---------------------------------------------------------------------------
// Live capture from AMS2 shared memory
//---------------------------------------------------------------------------

void __fastcall TFormMotecSetup::LiveCaptureCheckBoxClick(TObject *Sender)
{
	if (LiveCaptureCheckBox->Checked) {
		liveActive = true;
		liveConnected = false;

		// Determine mode based on current profile selection
		ChannelProfile profile = ChannelProfile::AMS2; // Default fallback
		if (ChannelProfileCombo != NULL) {
			profile = (ChannelProfile)ChannelProfileCombo->ItemIndex;
		}
		liveMode = (profile == ChannelProfile::EvoM1) ? LIVE_EVO : LIVE_AMS2;

        MemoSummary->Lines->Clear();
        MemoSummary->Lines->Add("Live capture enabled. Searching for source...");

		// Reset state for reconnection
		liveInPit = true;
		liveLapInProgress = false;
		liveLaps.clear();
		liveLapNumber = 0;
		sessionLapCounter = 0;
		lastCompletedLaps = 0;
		liveCurrentLap = LapStats();
		liveWasOnTrack = false;

		// LED red = not connected yet (reconnecting)
		LiveLed->Brush->Color = clRed;
		LabelLiveStatus->Caption = "Connecting...";

		// Start the connection timer to begin looking for shared memory/process
		LiveConnectTimer->Enabled = true;
	} else {
		stopLiveCapture();
	}
}



void __fastcall TFormMotecSetup::LiveConnectTimerTimer(TObject *Sender)
{
    if (!liveActive || liveConnected)
        return;

    // Check if AMS2 process is actually running before attempting connection
    if (liveMode == LIVE_AMS2 && !Ams2IsProcessRunning()) {
        return; // Keep retrying, but don't try to open shared memory yet
    }

    std::string err;
    bool ok = false;

    if (liveMode == LIVE_EVO)
        ok = AcsEvoOpenSharedMemory(err);
    else
        ok = Ams2OpenSharedMemory(err);

    if (ok)
    {
        // Connected!
        liveConnected = true;
        LiveLed->Brush->Color = clLime;
        LabelLiveStatus->Caption = "Connected";
        LabelLiveStatus->Font->Color = clGreen;
        LiveConnectTimer->Enabled = false;

        // Start the high-rate data timer
        LiveTimer->Enabled = true;

        if (liveMode == LIVE_EVO)
        {
            // Read static track name
            SPageFileStaticEvo stat;
            if (AcsEvoReadStatic(stat))
                liveTrackName = stat.track;

            MemoSummary->Lines->Add("Live: connected to AC EVO. Waiting for car to leave pits...");
			//StatusBar1->SimpleText = "Live: connected (AC EVO) — waiting for track";
        }
        else
        {
            // We don't add the "Waiting..." message yet, because we might be 
            // immediately seeing a frame that says we are on track.
            // Instead, let processLiveFrame handle the status updates.
			//StatusBar1->SimpleText = "Live: connected — waiting for car to leave pits...";
            MemoSummary->Lines->Add("Live: connected to AMS2. Waiting for car to leave pits...");
        }
    }
    else
    {
        // Still not connected — keep retrying
		//if (liveMode == LIVE_EVO)
			//StatusBar1->SimpleText = "Live: connecting... (AC EVO not detected)";
		//else
			//StatusBar1->SimpleText = "Live: connecting... (AMS2 not detected)";
    }
}

void __fastcall TFormMotecSetup::stopLiveCapture()
{
    if (LiveCaptureCheckBox != NULL && LiveCaptureCheckBox->Checked) {
        // Reconnection mode: user still wants live capture, but connection is lost.
        liveConnected = false;
        LiveTimer->Enabled = false;

        // Close handles to avoid leaks/conflicts on next attempt
        if (liveMode == LIVE_EVO)
            AcsEvoCloseSharedMemory();
        else
            Ams2CloseSharedMemory();

        // Visual feedback for reconnection state
        LiveLed->Brush->Color = clRed;
        LabelLiveStatus->Caption = "Reconnecting...";
        LabelLiveStatus->Font->Color = clGray;

        // Keep the connection timer running to look for the process again
        LiveConnectTimer->Enabled = true;
    } else {
        // Actual user-initiated stop or complete shutdown of live mode.
        liveActive = false;
        liveConnected = false;
        LiveConnectTimer->Enabled = false;
        LiveTimer->Enabled = false;
        LiveLed->Brush->Color = clRed;
        LabelLiveStatus->Caption = "Disconnected";
        LabelLiveStatus->Font->Color = clGray;

        // If a lap is in progress, finalize it
        if (liveLapInProgress)
            finalizeLiveLap();

        Button1->Enabled = true;
        ButtonLoad->Enabled = true;

        if (liveMode == LIVE_EVO)
            AcsEvoCloseSharedMemory();
        else
            Ams2CloseSharedMemory();

        if (!liveLaps.empty())
            generateLiveReport();
        else
        {
            MemoSummary->Lines->Add("Live capture stopped. No laps captured.");
		//StatusBar1->SimpleText = "Live capture stopped";
        }
    }
}

void __fastcall TFormMotecSetup::LiveTimerTimer(TObject *Sender)
{
    if (!liveActive || !liveConnected)
        return;

    // If we are in AMS2 mode, check if the process is still running.
    // If it's not, the game has closed or crashed.
    if (liveMode == LIVE_AMS2 && !Ams2IsProcessRunning()) {
        MemoSummary->Lines->Add("Live: connection lost (AMS2 process not found).");
        stopLiveCapture();
        return;
    }

    if (liveMode == LIVE_EVO)
    {
        SPageFilePhysics phys;
        SPageFileGraphicEvo gfx;
        if (!AcsEvoReadPhysics(phys) || !AcsEvoReadGraphics(gfx))
        {
            // If reading fails, it might be a torn read or connection loss. 
            // We don't stop immediately to avoid false disconnects on torn reads.
            return; 
        }
        processLiveFrameEVO(phys, gfx);
    }
    else
    {
        Ams2SharedMemory frame;
        if (!Ams2ReadFrame(frame))
        {
            // If reading fails, it might be a torn read. We skip this tick.
            return; 
        }

        processLiveFrame(frame);
    }
}

void __fastcall TFormMotecSetup::processLiveFrame(const Ams2SharedMemory& frame)
{
	// 1. Detect Pause (mGameState == 3)
	if (frame.mGameState == GAME_INGAME_PAUSED) {
        // If we were on track and now paused, just stop feeding data but don't finalize the lap yet
		liveWasOnTrack = false;
		lastPitMode = frame.mPitMode; // Keep track of pit mode even when paused
		//StatusBar1->SimpleText = "Live: PAUSED";
		return;
    }

	bool onTrack = Ams2IsOnTrack(frame);
	bool inPits = Ams2IsInPits(frame);

    // --- Event Detection based on transitions (AMS2_on_track_detection.md) ---

	// 0. Detect pit → track transition: start a new lap if none is currently in progress
	if (onTrack && !liveWasOnTrack && !liveLapInProgress && sessionLapCounter == 0) {
		MemoSummary->Lines->Add("Leaving Pits");
		sessionLapCounter = 0;
		lastCompletedLaps = frame.mParticipantInfo[frame.mViewedParticipantIndex].mLapsCompleted;
		firstLapStarted = false;
		//return;
	}

	float delta = frame.mCurrentTime - previous_mCurrentTime;

	// 1. Lap Completion & Next Lap Start: mCompletedLaps > lastCompletedLaps
	if (delta < -1.0f || (!firstLapStarted && frame.mCurrentTime > 0.0f)) {
		sessionLapCounter++; // Increment our internal session counter
		firstLapStarted = true ;
        // If we are currently in a lap, finalize it because the new one has started
        if (liveLapInProgress) {
            liveCurrentLap.endTime = (double)GetTickCount64() / 1000.0;
            finalizeLiveLap();
		}

		// Start the next lap immediately if still on track (satisfies user request for Lap 1 start after crossing line)
		if (onTrack) {
			liveLapNumber = sessionLapCounter; // Use our internal counter to drive liveLapNumber
			liveLapInProgress = true;
			liveCurrentLap = LapStats();
            liveCurrentLap.lapNumber = liveLapNumber;
            liveCurrentLap.startTime = (double)GetTickCount64() / 1000.0;
            liveCurrentLap.vehicleName = frame.mCarName;
            liveCurrentLap.trackName = frame.mTrackLocation;

			MemoSummary->Lines->Add("Lap " + IntToStr(liveLapNumber) + " started.");
			//StatusBar1->SimpleText = "Live: Lap " + IntToStr(liveLapNumber) + " in progress";
		}
	}

	// 2. Teleport / Pit Entry: lastPitMode == 0 AND car in garage then generate the report
	if (lastPitMode == PIT_MODE_NONE && ( frame.mPitMode == PIT_MODE_IN_GARAGE || frame.mPitMode == PIT_MODE_IN_PIT)) {
		// If a lap was being recorded, finalize it first
		if (liveLapInProgress) {
			liveCurrentLap.endTime = (double)GetTickCount64() / 1000.0;
			finalizeLiveLap();
		}

		// Generate report if we have any completed laps
		if (!liveLaps.empty()) {
			generateLiveReport();
		}

		firstLapStarted = false;

		if (frame.mPitMode == PIT_MODE_IN_GARAGE) {
			MemoSummary->Lines->Add("Back to Garage");
		} else if (frame.mPitMode == PIT_MODE_IN_PIT) {
			MemoSummary->Lines->Add("Pistop");
		}

		// If no laps were ever recorded, reset the session counters for a fresh start next time
		if (!liveLapInProgress && liveLaps.empty()) {
			sessionLapCounter = 0;
			firstLapStarted = false;
			MemoSummary->Lines->Add("Back to garage with no valid lap.");
		}
	}
    // Feed data only when on track and lap is in progress
    if (onTrack && liveLapInProgress)
    {
		double timeSeconds = (double)GetTickCount64() / 1000.0;
		Ams2FeedLap(liveCurrentLap, frame, timeSeconds);
    }

	// Update history for next frame
    lastPitMode = frame.mPitMode;
	liveWasOnTrack = onTrack;
	previous_mCurrentTime = frame.mCurrentTime;

}
void __fastcall TFormMotecSetup::processLiveFrameEVO(const SPageFilePhysics& phys, const SPageFileGraphicEvo& gfx)
{
    bool onTrack = AcsEvoIsOnTrack(gfx);
	bool inPits = AcsEvoIsInPits(gfx);

    // Detect pit → track transition: start a new lap
    if (onTrack && !liveWasOnTrack)
    {
        liveLapInProgress = true;
        liveLapNumber++;
        liveCurrentLap = LapStats();
        liveCurrentLap.lapNumber = liveLapNumber;
        liveCurrentLap.startTime = (double)GetTickCount64() / 1000.0;
        liveCurrentLap.vehicleName = gfx.car_model;
        liveCurrentLap.trackName = liveTrackName;

        MemoSummary->Lines->Add("Lap " + IntToStr(liveLapNumber) + " started.");
		//StatusBar1->SimpleText = "Live: Lap " + IntToStr(liveLapNumber) + " in progress";
    }

    // Detect track → pit transition: finalize the lap
    if (inPits && liveWasOnTrack && liveLapInProgress)
    {
        liveCurrentLap.endTime = (double)GetTickCount64() / 1000.0;
        finalizeLiveLap();
    }

    // If we entered pits and have captured laps, generate a report even if no lap is currently in progress
    if (inPits && liveWasOnTrack && !liveLapInProgress && !liveLaps.empty())
    {
        generateLiveReport();
    }

    // Feed data only when on track and lap is in progress
    if (onTrack && liveLapInProgress)
    {
        double timeSeconds = (double)GetTickCount64() / 1000.0;
        AcsEvoFeedLap(liveCurrentLap, phys, gfx, timeSeconds);
    }

    liveWasOnTrack = onTrack;
}

void __fastcall TFormMotecSetup::finalizeLiveLap()
{
    liveLapInProgress = false;

    // Only keep laps with meaningful data
    if (liveCurrentLap.rowCount > 10)
    {
        liveLaps.push_back(liveCurrentLap);
        MemoSummary->Lines->Add("Lap " + IntToStr(liveLapNumber) + " completed (" +
                               IntToStr((int)liveCurrentLap.rowCount) + " samples).");
    }
    else
    {
        MemoSummary->Lines->Add("Lap " + IntToStr(liveLapNumber) + " discarded (too few samples).");
    }

    liveCurrentLap = LapStats();
}

void __fastcall TFormMotecSetup::generateLiveReport()
{
    if (liveLaps.empty()) return;

    // Use the existing pipeline to generate the report
    ChannelProfile profile = ChannelProfile::AMS2;
    if (ChannelProfileCombo != NULL)
        profile = (ChannelProfile)ChannelProfileCombo->ItemIndex;

    ReportLanguage language = (LanguageCombo != NULL) ? (ReportLanguage)LanguageCombo->ItemIndex : ReadReportLanguageSetting();

	ReportResult result = buildAverageLapReport(liveLaps, language, profile);

    UpdateReportTabs(result);

    // Update graphics (valid laps only, consistent with the report)
    std::vector<LapStats> validLiveLaps = LapReportGenerator::getValidLaps(liveLaps);
    UpdateTireTemps(validLiveLaps);
    UpdateBrakeTemps(validLiveLaps);
    UpdateSuspension(validLiveLaps);
    if (TabSummary != NULL && TabSummary->Visible)
        PageControlReport->ActivePage = TabSummary;

    //StatusBar1->SimpleText = "Live capture complete — " + IntToStr((int)liveLaps.size()) + " laps";
}

void __fastcall TFormMotecSetup::UpdateReportTabs(const ReportResult& result)
{
    if (PageControlReport == NULL) return;

    // 1. Clear all memos first
    MemoSummary->Lines->Clear();
    MemoGeneral->Lines->Clear();
    MemoTyre->Lines->Clear();
    MemoBrake->Lines->Clear();
    MemoSuspension->Lines->Clear();
    MemoEngineCooling->Lines->Clear();
    MemoHandling->Lines->Clear();

    // 3. Populate content from result map
    auto formatForMemo = [](const std::string& s) -> String {
        std::string winS = s;
        size_t pos = 0;
        while ((pos = winS.find('\n', pos)) != std::string::npos) {
            if (pos == 0 || winS[pos - 1] != '\r') {
                winS.insert(pos, 1, '\r');
                pos += 2;
            } else {
                pos++;
            }
        }
        return String(winS.c_str());
    };

    if (result.count(ReportSection::Summary)) {
        MemoSummary->Text = formatForMemo(result.at(ReportSection::Summary).content);
    }
    if (result.count(ReportSection::General)) {
        MemoGeneral->Text = formatForMemo(result.at(ReportSection::General).content);
    }
    if (result.count(ReportSection::Tyre)) {
        MemoTyre->Text = formatForMemo(result.at(ReportSection::Tyre).content);
    }
    if (result.count(ReportSection::Brake)) {
        MemoBrake->Text = formatForMemo(result.at(ReportSection::Brake).content);
    }
    if (result.count(ReportSection::Suspension)) {
        MemoSuspension->Text = formatForMemo(result.at(ReportSection::Suspension).content);
    }
    if (result.count(ReportSection::EngineCooling)) {
        MemoEngineCooling->Text = formatForMemo(result.at(ReportSection::EngineCooling).content);
    }
    if (result.count(ReportSection::Handling)) {
        MemoHandling->Text = formatForMemo(result.at(ReportSection::Handling).content);
    }

    // 3. Manage Tab Visibility: show a tab only when it has data and a recommendation
    auto setTabVisible = [&](TTabSheet* tab, ReportSection section) {
        if (tab == NULL) return;
        const bool hasData = result.count(section) && !result.at(section).content.empty();
        const bool visible = hasData && result.at(section).hasRecommendation;
        tab->Visible = visible;

        // Visual indicator: ● prefix when section has content, plain name otherwise.
        // Strip any previous dot before re-applying.
        String cap = tab->Caption;
        if (cap.Length() >= 2 && (System::WideChar)cap[1] == 0x25CF && cap[2] == ' ') {
            cap = cap.SubString(3, cap.Length() - 2);
        }
        tab->Caption = hasData ? (String((System::WideChar)0x25CF) + " " + cap) : cap;
    };

    setTabVisible(TabGeneral, ReportSection::General);
    setTabVisible(TabTyre, ReportSection::Tyre);
    setTabVisible(TabBrake, ReportSection::Brake);
    setTabVisible(TabSuspension, ReportSection::Suspension);
    setTabVisible(TabEngineCooling, ReportSection::EngineCooling);
    setTabVisible(TabHandling, ReportSection::Handling);

    // Summary is always visible if it has content.
    if (TabSummary != NULL) {
        TabSummary->Visible = result.count(ReportSection::Summary);
    }

    // Force a REAL page transition so Windows properly hides/shows tab windows
    // and triggers painting. If ActivePage is already Summary, VCL does nothing
    // on re-assignment — no show/hide, no repaint, stale pixels remain.
    // Bouncing through another visible tab replicates what clicking does.
    if (TabSummary != NULL && TabSummary->Visible) {
        if (PageControlReport->ActivePage == TabSummary) {
            TTabSheet* other = NULL;
            for (int i = 0; i < PageControlReport->PageCount; ++i) {
                TTabSheet* t = PageControlReport->Pages[i];
                if (t != TabSummary && t->Visible) { other = t; break; }
            }
            if (other) {
                PageControlReport->ActivePage = other;
                PageControlReport->ActivePage = TabSummary;
            }
        } else {
            PageControlReport->ActivePage = TabSummary;
        }
    }
}

void __fastcall TFormMotecSetup::UpdateSuspension(std::vector<LapStats>& laps)
{
    if (laps.empty())
        return;

    LapStats avg = buildAverageLapData(laps);

    // Corner order: 0=LF, 1=RF, 2=LR, 3=RR.
    // Priority (profile-agnostic): direct suspension-velocity channel first;
    // fall back to the virtual channel derived from damper travel over time.
    const VelocityHistogram* suspVels[4] = { &avg.suspVelLF, &avg.suspVelRF, &avg.suspVelLR, &avg.suspVelRR };
    const DamperCornerStats* dampers[4] = { &avg.damperLF, &avg.damperRF, &avg.damperLR, &avg.damperRR };
    const StatSummary* travels[4] = { &avg.susTravelLF, &avg.susTravelRF, &avg.susTravelLR, &avg.susTravelRR };
    const StatSummary* bumpstops[4] = { &avg.bumpStopLF, &avg.bumpStopRF, &avg.bumpStopLR, &avg.bumpStopRR };

    for (int i = 0; i < 4; i++) {
        const VelocityHistogram& hist =
            suspVels[i]->totalCount > 0 ? *suspVels[i] : dampers[i]->histogram;
        suspensionData[i].valid = hist.totalCount > 0;
        if (suspensionData[i].valid) {
            for (int b = 0; b < VelocityHistogram::kBinCount; b++) {
                suspensionData[i].bins[b] = static_cast<double>(hist.bins[b]) / static_cast<double>(hist.totalCount);
            }
            suspensionData[i].travel = travels[i]->average();
            suspensionData[i].bumpstop = bumpstops[i]->max;
            suspensionData[i].issue = hasDamperIssue(hist);
        }
    }

    // Shared Y-axis scale: highest bin across all four corners.
    suspensionMaxBin = 0.0;
    for (int i = 0; i < 4; ++i)
        if (suspensionData[i].valid) {
            for (int b = 0; b < VelocityHistogram::kBinCount; ++b)
                if (suspensionData[i].bins[b] > suspensionMaxBin)
                    suspensionMaxBin = suspensionData[i].bins[b];
        }

    // Repaint the four damper histogram paint boxes so they reflect the new data.
    if (DamperFLPaintBox != NULL) DamperFLPaintBox->Invalidate();
    if (DamperFRPaintBox != NULL) DamperFRPaintBox->Invalidate();
    if (DamperRLPaintBox != NULL) DamperRLPaintBox->Invalidate();
    if (DamperRRPaintBox != NULL) DamperRRPaintBox->Invalidate();
}


