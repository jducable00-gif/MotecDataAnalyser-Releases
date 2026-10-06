//---------------------------------------------------------------------------
// MotecSettingsForm.cpp � Implementation of the settings dialog.
//---------------------------------------------------------------------------

#pragma hdrstop

#include <vcl.h>
#include "MotecSettingsForm.h"
#include "MotecSetupAnalyzer.h"
#include "MotecSetupForm.h"
#include <IniFiles.hpp>

// Access to global thresholds and helper functions from other units
extern Thresholds g_thresholds;
extern String ThresholdsFilePath(); 

__fastcall TFormMotecSettings::TFormMotecSettings(TComponent* Owner)
    : TForm(Owner)
{
}

void __fastcall TFormMotecSettings::FormShow(TObject *Sender)
{
    TranslateUI();
    LoadFromThresholds();
}

void TFormMotecSettings::LoadFromThresholds()
{
    // General
    EditMinDamperSamples->Text = IntToStr((int)g_thresholds.minDamperSamples);
    EditMinHandlingSamples->Text = IntToStr((int)g_thresholds.minHandlingSamples);
    EditMinSteerYawSamples->Text = IntToStr((int)g_thresholds.minSteerYawSamples);
    EditHandlingConfidenceSamples->Text = IntToStr((int)g_thresholds.handlingConfidenceSamples);
    EditMinValidLapTime->Text = FloatToStr(g_thresholds.minValidLapTimeMultiplier);
    EditMaxValidLapTime->Text = FloatToStr(g_thresholds.maxValidLapTimeMultiplier);

    // Pressure (psi)
    EditPressLow->Text = FloatToStr(g_thresholds.pressureLowPsi);
    EditPressHigh->Text = FloatToStr(g_thresholds.pressureHighPsi);
    EditPressSideDiffMax->Text = FloatToStr(g_thresholds.pressureSideDiffMax);
    EditPressFrontRearDiffMax->Text = FloatToStr(g_thresholds.pressureFrontRearDiffMax);

    // Tyre temp spread (C)
	EditCamberMinSpread->Text = FloatToStr(g_thresholds.camberMinSpread);
    EditCamberMaxSpread->Text = FloatToStr(g_thresholds.camberMaxSpread);

    // Damper velocity (mm/s)
    EditDamperLowSpeedThreshold->Text = FloatToStr(g_thresholds.damperLowSpeedThreshold);
    EditDamperAsymmetryThreshold->Text = FloatToStr(g_thresholds.damperAsymmetryThreshold);
    EditDamperHighSpeedShareMax->Text = FloatToStr(g_thresholds.damperHighSpeedShareMax);
    EditDamperLowSpeedShareMin->Text = FloatToStr(g_thresholds.damperLowSpeedShareMin);

    // Damper 4-zone targets (%)
    EditHscTargetMin->Text = FloatToStr(g_thresholds.damperHscTargetMin);
    EditHscTargetMax->Text = FloatToStr(g_thresholds.damperHscTargetMax);
    EditLscTargetMin->Text = FloatToStr(g_thresholds.damperLscTargetMin);
    EditLscTargetMax->Text = FloatToStr(g_thresholds.damperLscTargetMax);
    EditLsrTargetMin->Text = FloatToStr(g_thresholds.damperLsrTargetMin);
    EditLsrTargetMax->Text = FloatToStr(g_thresholds.damperLsrTargetMax);
    EditHsrTargetMin->Text = FloatToStr(g_thresholds.damperHsrTargetMin);
    EditHsrTargetMax->Text = FloatToStr(g_thresholds.damperHsrTargetMax);
    EditZoneModerateBand->Text = FloatToStr(g_thresholds.damperZoneModerateBand);

    // Handling detection
    EditBrakePedalMin->Text = FloatToStr(g_thresholds.brakePedalMin);
    EditThrottleMaxBraking->Text = FloatToStr(g_thresholds.throttleMaxBraking);
    EditSteerMinCornering->Text = FloatToStr(g_thresholds.steerMinCornering);

    // Thermal
    EditWaterTempMax->Text = FloatToStr(g_thresholds.waterTempMax);
    EditOilTempMax->Text = FloatToStr(g_thresholds.oilTempMax);

    // Suspension
    EditSuspTravelMax->Text = FloatToStr(g_thresholds.suspensionTravelMax);
    EditRideHeightDiffMax->Text = FloatToStr(g_thresholds.rideHeightDiffMax);
    EditSuspVelocityMax->Text = FloatToStr(g_thresholds.suspensionVelocityMax);
    EditSuspAvgFrontMax->Text = FloatToStr(g_thresholds.suspensionAvgTravelFrontMax);
    EditSuspAvgRearMax->Text = FloatToStr(g_thresholds.suspensionAvgTravelRearMax);

    // Brake
    EditBrakeTempDiffMax->Text = FloatToStr(g_thresholds.brakeTempDiffMax);
    EditBrakeBiasMax->Text = FloatToStr(g_thresholds.brakeBiasMax);
    EditBrakeBiasFrontMin->Text = FloatToStr(g_thresholds.brakeBiasFrontAvgMin);
    EditBrakeTorqueMin->Text = FloatToStr(g_thresholds.brakeTorqueMin);
    EditBrakeChannelMismatchMax->Text = FloatToStr(g_thresholds.brakeChannelMismatchMax);
    EditBrakeTorqueFrontShareMax->Text = FloatToStr(g_thresholds.brakeTorqueFrontShareMax);
    EditBrakeTorqueFrontShareMin->Text = FloatToStr(g_thresholds.brakeTorqueFrontShareMin);
    EditBrakeTorqueLrDiffRatio->Text = FloatToStr(g_thresholds.brakeTorqueLrDiffRatio);
    EditBrakeBiasSettingFrontMax->Text = FloatToStr(g_thresholds.brakeBiasSettingFrontMax);
    EditBrakeBiasSettingRearMin->Text = FloatToStr(g_thresholds.brakeBiasSettingRearMin);
}

void TFormMotecSettings::SaveToThresholds()
{
    try {
        // Update global g_thresholds from UI
        g_thresholds.minDamperSamples = (int)StrToInt(EditMinDamperSamples->Text);
        g_thresholds.minHandlingSamples = (int)StrToInt(EditMinHandlingSamples->Text);
        g_thresholds.minSteerYawSamples = (int)StrToInt(EditMinSteerYawSamples->Text);
        g_thresholds.handlingConfidenceSamples = (int)StrToInt(EditHandlingConfidenceSamples->Text);
        g_thresholds.minValidLapTimeMultiplier = StrToFloatDef(EditMinValidLapTime->Text, g_thresholds.minValidLapTimeMultiplier);
        g_thresholds.maxValidLapTimeMultiplier = StrToFloatDef(EditMaxValidLapTime->Text, g_thresholds.maxValidLapTimeMultiplier);

        g_thresholds.pressureLowPsi = StrToFloat(EditPressLow->Text);
        g_thresholds.pressureHighPsi = StrToFloat(EditPressHigh->Text);
        g_thresholds.pressureSideDiffMax = StrToFloat(EditPressSideDiffMax->Text);
        g_thresholds.pressureFrontRearDiffMax = StrToFloat(EditPressFrontRearDiffMax->Text);

        g_thresholds.camberMinSpread = StrToFloat(EditCamberMinSpread->Text);
        g_thresholds.camberMaxSpread = StrToFloat(EditCamberMaxSpread->Text);

        g_thresholds.damperLowSpeedThreshold = StrToFloat(EditDamperLowSpeedThreshold->Text);
        g_thresholds.damperAsymmetryThreshold = StrToFloat(EditDamperAsymmetryThreshold->Text);
        g_thresholds.damperHighSpeedShareMax = StrToFloat(EditDamperHighSpeedShareMax->Text);
        g_thresholds.damperLowSpeedShareMin = StrToFloat(EditDamperLowSpeedShareMin->Text);

        // Damper 4-zone targets (%)
        g_thresholds.damperHscTargetMin = StrToFloatDef(EditHscTargetMin->Text, g_thresholds.damperHscTargetMin);
        g_thresholds.damperHscTargetMax = StrToFloatDef(EditHscTargetMax->Text, g_thresholds.damperHscTargetMax);
        g_thresholds.damperLscTargetMin = StrToFloatDef(EditLscTargetMin->Text, g_thresholds.damperLscTargetMin);
        g_thresholds.damperLscTargetMax = StrToFloatDef(EditLscTargetMax->Text, g_thresholds.damperLscTargetMax);
        g_thresholds.damperLsrTargetMin = StrToFloatDef(EditLsrTargetMin->Text, g_thresholds.damperLsrTargetMin);
        g_thresholds.damperLsrTargetMax = StrToFloatDef(EditLsrTargetMax->Text, g_thresholds.damperLsrTargetMax);
        g_thresholds.damperHsrTargetMin = StrToFloatDef(EditHsrTargetMin->Text, g_thresholds.damperHsrTargetMin);
        g_thresholds.damperHsrTargetMax = StrToFloatDef(EditHsrTargetMax->Text, g_thresholds.damperHsrTargetMax);
        g_thresholds.damperZoneModerateBand = StrToFloatDef(EditZoneModerateBand->Text, g_thresholds.damperZoneModerateBand);

        g_thresholds.brakePedalMin = StrToFloat(EditBrakePedalMin->Text);
        g_thresholds.throttleMaxBraking = StrToFloat(EditThrottleMaxBraking->Text);
        g_thresholds.steerMinCornering = StrToFloat(EditSteerMinCornering->Text);

        g_thresholds.waterTempMax = StrToFloat(EditWaterTempMax->Text);
        g_thresholds.oilTempMax = StrToFloat(EditOilTempMax->Text);

        // Suspension
        g_thresholds.suspensionTravelMax = StrToFloat(EditSuspTravelMax->Text);
        g_thresholds.rideHeightDiffMax = StrToFloat(EditRideHeightDiffMax->Text);
        g_thresholds.suspensionVelocityMax = StrToFloat(EditSuspVelocityMax->Text);
        g_thresholds.suspensionAvgTravelFrontMax = StrToFloat(EditSuspAvgFrontMax->Text);
        g_thresholds.suspensionAvgTravelRearMax = StrToFloat(EditSuspAvgRearMax->Text);

        // Brake
        g_thresholds.brakeTempDiffMax = StrToFloat(EditBrakeTempDiffMax->Text);
        g_thresholds.brakeBiasMax = StrToFloat(EditBrakeBiasMax->Text);
        g_thresholds.brakeBiasFrontAvgMin = StrToFloat(EditBrakeBiasFrontMin->Text);
        g_thresholds.brakeTorqueMin = StrToFloat(EditBrakeTorqueMin->Text);
        g_thresholds.brakeChannelMismatchMax = StrToFloat(EditBrakeChannelMismatchMax->Text);
        g_thresholds.brakeTorqueFrontShareMax = StrToFloat(EditBrakeTorqueFrontShareMax->Text);
        g_thresholds.brakeTorqueFrontShareMin = StrToFloat(EditBrakeTorqueFrontShareMin->Text);
        g_thresholds.brakeTorqueLrDiffRatio = StrToFloat(EditBrakeTorqueLrDiffRatio->Text);
        g_thresholds.brakeBiasSettingFrontMax = StrToFloat(EditBrakeBiasSettingFrontMax->Text);
        g_thresholds.brakeBiasSettingRearMin = StrToFloat(EditBrakeBiasSettingRearMin->Text);

    } catch (...) {
        ShowMessage("Error: Invalid input values.");
        throw; 
    }
}

void __fastcall TFormMotecSettings::ButtonSaveClick(TObject *Sender)
{
    try {
        SaveToThresholds();
        
        // Write the updated g_thresholds to the INI file.
        TIniFile* ini = new TIniFile(ThresholdsFilePath());
        try {
            ini->WriteInteger("General", "MinDamperSamples", (int)g_thresholds.minDamperSamples);
            ini->WriteInteger("General", "MinHandlingSamples", (int)g_thresholds.minHandlingSamples);
            ini->WriteInteger("General", "MinSteerYawSamples", (int)g_thresholds.minSteerYawSamples);
            ini->WriteInteger("General", "HandlingConfidenceSamples", (int)g_thresholds.handlingConfidenceSamples);

            ini->WriteFloat("Pressure", "Low", g_thresholds.pressureLowPsi);
            ini->WriteFloat("Pressure", "High", g_thresholds.pressureHighPsi);
            ini->WriteFloat("Pressure", "SideDiffMax", g_thresholds.pressureSideDiffMax);
            ini->WriteFloat("Pressure", "FrontRearDiffMax", g_thresholds.pressureFrontRearDiffMax);

            ini->WriteFloat("TyreTemp", "CamberMinSpread", g_thresholds.camberMinSpread);
            ini->WriteFloat("TyreTemp", "CamberMaxSpread", g_thresholds.camberMaxSpread);

            ini->WriteFloat("Damper", "LowSpeedThreshold", g_thresholds.damperLowSpeedThreshold);
            ini->WriteFloat("Damper", "AsymmetryThreshold", g_thresholds.damperAsymmetryThreshold);
            ini->WriteFloat("Damper", "HighSpeedShareMax", g_thresholds.damperHighSpeedShareMax);
            ini->WriteFloat("Damper", "LowSpeedShareMin", g_thresholds.damperLowSpeedShareMin);

            ini->WriteFloat("DamperZones", "HscTargetMin", g_thresholds.damperHscTargetMin);
            ini->WriteFloat("DamperZones", "HscTargetMax", g_thresholds.damperHscTargetMax);
            ini->WriteFloat("DamperZones", "LscTargetMin", g_thresholds.damperLscTargetMin);
            ini->WriteFloat("DamperZones", "LscTargetMax", g_thresholds.damperLscTargetMax);
            ini->WriteFloat("DamperZones", "LsrTargetMin", g_thresholds.damperLsrTargetMin);
            ini->WriteFloat("DamperZones", "LsrTargetMax", g_thresholds.damperLsrTargetMax);
            ini->WriteFloat("DamperZones", "HsrTargetMin", g_thresholds.damperHsrTargetMin);
            ini->WriteFloat("DamperZones", "HsrTargetMax", g_thresholds.damperHsrTargetMax);
            ini->WriteFloat("DamperZones", "ModerateBand", g_thresholds.damperZoneModerateBand);

            ini->WriteFloat("Handling", "BrakePedalMin", g_thresholds.brakePedalMin);
            ini->WriteFloat("Handling", "ThrottleMaxBraking", g_thresholds.throttleMaxBraking);
            ini->WriteFloat("Handling", "SteerMinCornering", g_thresholds.steerMinCornering);
            ini->WriteFloat("Lap", "MinValidTimeMult", g_thresholds.minValidLapTimeMultiplier);
            ini->WriteFloat("Lap", "MaxValidTimeMult", g_thresholds.maxValidLapTimeMultiplier);

        } __finally {
            delete ini;
        }
        
        ModalResult = mrOk;
    } catch (...) {
        // Error message already shown in SaveToThresholds
    }
}

void __fastcall TFormMotecSettings::ButtonCancelClick(TObject *Sender)
{
    ModalResult = mrCancel;
}

#pragma package(smart_init)

#pragma resource "*.dfm"

TFormMotecSettings *FormMotecSettings;

void TFormMotecSettings::TranslateUI()
{
    ReportLanguage lang = TFormMotecSetup::ReadReportLanguageSetting();
    bool isFr = (lang == ReportLanguage::French);

    Caption = isFr ? "Réglages des Seuils" : "Threshold Settings";

    PageControl->ActivePage = TabBrake; // Keep active page as is
    TabGeneral->Caption = isFr ? "Général" : "General";
    TabTyre->Caption = isFr ? "Pneus" : "Tyre";
    TabHandling->Caption = isFr ? "Comportement" : "Handling";
    TabThermal->Caption = isFr ? "Thermique" : "Thermal";
    TabSuspension->Caption = isFr ? "Suspension" : "Suspension";
    TabBrake->Caption = isFr ? "Freins" : "Brake";

    GroupGeneral->Caption = isFr ? "Échantillonnage et Confiance" : "Sampling & Confidence";
    LblMinDamperSamples->Caption = isFr ? "Nb min échantillons amortisseurs :" : "Min Damper Samples:";
    LblMinHandlingSamples->Caption = isFr ? "Nb min échantillons comportement :" : "Min Handling Samples:";
    LblMinSteerYawSamples->Caption = isFr ? "Nb min échantillons direction/lacet :" : "Min Steer/Yaw Samples:";
    LblHandlingConfidenceSamples->Caption = isFr ? "Nb min échantillons pour confiance élevée :" : "Min Samples for High Confidence:";
    GroupLapValidity->Caption = isFr ? "Temps de tour valide" : "Valid Lap Time";
    LblMinValidLapTime->Caption = isFr ? "Temps min. tour valide (x meilleur):" : "Min Valid Lap Time (x best):";
    LblMaxValidLapTime->Caption = isFr ? "Temps max. tour valide (x meilleur):" : "Max Valid Lap Time (x best):";

    GroupTyrePressure->Caption = isFr ? "Pression (psi)" : "Pressure (psi)";
    LblPressLow->Caption = isFr ? "Minimum :" : "Low:";
    LblPressHigh->Caption = isFr ? "Maximum :" : "High:";
    LblPressSideDiffMax->Caption = isFr ? "Diff pression Max Gauche/Droite :" : "Max Left/Right Pressure Diff:";
    LblPressFrontRearDiffMax->Caption = isFr ? "Diff pression Max Avant/Arrière :" : "Max Front/Rear Pressure Diff:";

    GroupTyreTemp->Caption = isFr ? "Température (C)" : "Temperature (C)";
    LblCamberMinSpread->Caption = isFr ? "Écart Intérieur/Extérieur Min :" : "Min Inner/Outer Spread:";
    LblCamberMaxSpread->Caption = isFr ? "Écart Intérieur/Extérieur Max :" : "Max Inner/Outer Spread:";

    GroupDamper->Caption = isFr ? "Vitesse Amortisseur" : "Damper Velocity";
    LblDamperLowSpeedThreshold->Caption = isFr ? "Seuil Basse Vitesse :" : "Low Speed Threshold:";
    LblDamperAsymmetryThreshold->Caption = isFr ? "Seuil Asymétrie :" : "Asymmetry Threshold:";
    LblDamperHighSpeedShareMax->Caption = isFr ? "Part Haute Vitesse Max :" : "High Speed Share Max:";
    LblDamperLowSpeedShareMin->Caption = isFr ? "Part Basse Vitesse Min :" : "Low Speed Share Min:";

    GroupDamperZones->Caption = isFr ? "Cibles 4 Zones Amortisseur (%)" : "Damper 4-Zone Targets (%)";
    LblHscTargetMin->Caption = isFr ? "Comp. Haute Min :" : "Hi Comp Min:";
    LblHscTargetMax->Caption = isFr ? "Comp. Haute Max :" : "Hi Comp Max:";
    LblLscTargetMin->Caption = isFr ? "Comp. Basse Min :" : "Lo Comp Min:";
    LblLscTargetMax->Caption = isFr ? "Comp. Basse Max :" : "Lo Comp Max:";
    LblLsrTargetMin->Caption = isFr ? "Détente Basse Min :" : "Lo Rebound Min:";
    LblLsrTargetMax->Caption = isFr ? "Détente Basse Max :" : "Lo Rebound Max:";
    LblHsrTargetMin->Caption = isFr ? "Détente Haute Min :" : "Hi Rebound Min:";
    LblHsrTargetMax->Caption = isFr ? "Détente Haute Max :" : "Hi Rebound Max:";
    LblZoneModerateBand->Caption = isFr ? "Bande Modérée (pts) :" : "Moderate Band (pts):";

    GroupHandling->Caption = isFr ? "Seuils de Détection" : "Detection Thresholds";
    LblBrakePedalMin->Caption = isFr ? "Pédale Frein Min :" : "Brake Pedal Min:";
    LblThrottleMaxBraking->Caption = isFr ? "Accélérateur Max au freinage :" : "Throttle Max Braking:";
    LblSteerMinCornering->Caption = isFr ? "Direction Min en virage :" : "Steer Min Cornering:";

    GroupThermal->Caption = isFr ? "Seuils Thermiques Moteur" : "Engine Thermal Thresholds";
    LblWaterTempMax->Caption = isFr ? "Temp Eau Max :" : "Max Water Temp:";
    LblOilTempMax->Caption = isFr ? "Temp Huile Max :" : "Max Oil Temp:";

    GroupSuspension->Caption = isFr ? "Géométrie Suspension" : "Suspension Geometry";
    LblSuspTravelMax->Caption = isFr ? "Course Max (mm) :" : "Max Travel (mm):";
    LblRideHeightDiffMax->Caption = isFr ? "Diff Hauteur Caisse Max (G/D) :" : "Max Ride Height Diff (L/R):";
    LblSuspVelocityMax->Caption = isFr ? "Vitesse Max (mm/s) :" : "Max Velocity (mm/s):";
    LblSuspAvgFrontMax->Caption = isFr ? "Course Moyenne Avant Max :" : "Max Avg Front Travel:";
    LblSuspAvgRearMax->Caption = isFr ? "Course Moyenne Arrière Max :" : "Max Avg Rear Travel:";

    GroupBrake->Caption = isFr ? "Seuils Freins" : "Brake Thresholds";
    LblBrakeTempDiffMax->Caption = isFr ? "Diff Temp Freins Max :" : "Brake Temp Diff Max:";
    LblBrakeBiasMax->Caption = isFr ? "Diff Répartition Frein Max Autorisée :" : "Max Allowed Brake Bias Diff:";
    LblBrakeBiasFrontMin->Caption = isFr ? "Répartition Avant Moyenne Min :" : "Min Avg Front Brake Bias:";
    LblBrakeTorqueMin->Caption = isFr ? "Couple Frein Min :" : "Brake Torque Min:";
    LblBrakeChannelMismatchMax->Caption = isFr ? "Détection Inversion Temp AV/AR :" : "Front/Rear Temp Swap Detection:";
    LblBrakeTorqueFrontShareMax->Caption = isFr ? "Couple Frein Avant % (Max) :" : "Front Brake Torque % (Max):";
    LblBrakeTorqueFrontShareMin->Caption = isFr ? "Couple Frein Avant % (Min) :" : "Front Brake Torque % (Min):";
    LblBrakeTorqueLrDiffRatio->Caption = isFr ? "Ratio Déséquilibre Couple G/D :" : "Left/Right Torque Imbalance Ratio:";
    LblBrakeBiasSettingFrontMax->Caption = isFr ? "Réglage Répartition Avant Max :" : "Brake Bias Setting Front Max:";
    LblBrakeBiasSettingRearMin->Caption = isFr ? "Réglage Répartition Arrière Min :" : "Brake Bias Setting Rear Min:";

    ButtonSave->Caption = isFr ? "Enregistrer" : "Save";
    ButtonCancel->Caption = isFr ? "Annuler" : "Cancel";
}

#pragma package(smart_init)


