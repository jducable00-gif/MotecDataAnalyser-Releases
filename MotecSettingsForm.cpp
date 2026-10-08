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
    EditMinValidLapTime->Text = FloatToStr(g_thresholds.minValidLapTimeMultiplier);
    EditMaxValidLapTime->Text = FloatToStr(g_thresholds.maxValidLapTimeMultiplier);

    // Pressure (psi)
    EditPressLow->Text = FloatToStr(g_thresholds.pressureLowPsi);
    EditPressHigh->Text = FloatToStr(g_thresholds.pressureHighPsi);
    EditPressFrontRearDiffMax->Text = FloatToStr(g_thresholds.pressureFrontRearDiffMax);

    // Tyre temp spread (C)
	EditCamberMinSpread->Text = FloatToStr(g_thresholds.camberMinSpread);
    EditCamberMaxSpread->Text = FloatToStr(g_thresholds.camberMaxSpread);

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
    EditDamperSlowSpeedRange->Text = FloatToStr(g_thresholds.damperSlowSpeedRange);

    // Handling detection
    EditBrakePedalMin->Text = FloatToStr(g_thresholds.brakePedalMin);
    EditThrottleMaxBraking->Text = FloatToStr(g_thresholds.throttleMaxBraking);
    EditSteerMinCornering->Text = FloatToStr(g_thresholds.steerMinCornering);

    // Thermal
    EditWaterTempMax->Text = FloatToStr(g_thresholds.waterTempMax);
    EditOilTempMax->Text = FloatToStr(g_thresholds.oilTempMax);
}

void TFormMotecSettings::SaveToThresholds()
{
    try {
        // Update global g_thresholds from UI
        g_thresholds.minDamperSamples = (int)StrToInt(EditMinDamperSamples->Text);
        g_thresholds.minValidLapTimeMultiplier = StrToFloatDef(EditMinValidLapTime->Text, g_thresholds.minValidLapTimeMultiplier);
        g_thresholds.maxValidLapTimeMultiplier = StrToFloatDef(EditMaxValidLapTime->Text, g_thresholds.maxValidLapTimeMultiplier);

        g_thresholds.pressureLowPsi = StrToFloat(EditPressLow->Text);
        g_thresholds.pressureHighPsi = StrToFloat(EditPressHigh->Text);
        g_thresholds.pressureFrontRearDiffMax = StrToFloat(EditPressFrontRearDiffMax->Text);

        g_thresholds.camberMinSpread = StrToFloat(EditCamberMinSpread->Text);
        g_thresholds.camberMaxSpread = StrToFloat(EditCamberMaxSpread->Text);

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
        g_thresholds.damperSlowSpeedRange = StrToFloatDef(EditDamperSlowSpeedRange->Text, g_thresholds.damperSlowSpeedRange);

        g_thresholds.brakePedalMin = StrToFloat(EditBrakePedalMin->Text);
        g_thresholds.throttleMaxBraking = StrToFloat(EditThrottleMaxBraking->Text);
        g_thresholds.steerMinCornering = StrToFloat(EditSteerMinCornering->Text);

        g_thresholds.waterTempMax = StrToFloat(EditWaterTempMax->Text);
        g_thresholds.oilTempMax = StrToFloat(EditOilTempMax->Text);

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

            ini->WriteFloat("Pressure", "Low", g_thresholds.pressureLowPsi);
            ini->WriteFloat("Pressure", "High", g_thresholds.pressureHighPsi);
            ini->WriteFloat("Pressure", "FrontRearDiffMax", g_thresholds.pressureFrontRearDiffMax);

            ini->WriteFloat("TyreTemp", "CamberMinSpread", g_thresholds.camberMinSpread);
            ini->WriteFloat("TyreTemp", "CamberMaxSpread", g_thresholds.camberMaxSpread);



            ini->WriteFloat("DamperZones", "HscTargetMin", g_thresholds.damperHscTargetMin);
            ini->WriteFloat("DamperZones", "HscTargetMax", g_thresholds.damperHscTargetMax);
            ini->WriteFloat("DamperZones", "LscTargetMin", g_thresholds.damperLscTargetMin);
            ini->WriteFloat("DamperZones", "LscTargetMax", g_thresholds.damperLscTargetMax);
            ini->WriteFloat("DamperZones", "LsrTargetMin", g_thresholds.damperLsrTargetMin);
            ini->WriteFloat("DamperZones", "LsrTargetMax", g_thresholds.damperLsrTargetMax);
            ini->WriteFloat("DamperZones", "HsrTargetMin", g_thresholds.damperHsrTargetMin);
            ini->WriteFloat("DamperZones", "HsrTargetMax", g_thresholds.damperHsrTargetMax);
            ini->WriteFloat("DamperZones", "ModerateBand", g_thresholds.damperZoneModerateBand);
            ini->WriteFloat("Damper", "SlowSpeedRange", g_thresholds.damperSlowSpeedRange);

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

    TabGeneral->Caption = isFr ? "Général" : "General";
    TabTyre->Caption = isFr ? "Pneus" : "Tyre";
    TabHandling->Caption = isFr ? "Comportement" : "Handling";
    TabThermal->Caption = isFr ? "Thermique" : "Thermal";
    TabSuspension->Caption = isFr ? "Suspension" : "Suspension";

    GroupGeneral->Caption = isFr ? "Échantillonnage et Confiance" : "Sampling & Confidence";
    LblMinDamperSamples->Caption = isFr ? "Nb min échantillons amortisseurs :" : "Min Damper Samples:";
    GroupLapValidity->Caption = isFr ? "Temps de tour valide" : "Valid Lap Time";
    LblMinValidLapTime->Caption = isFr ? "Temps min. tour valide (x meilleur):" : "Min Valid Lap Time (x best):";
    LblMaxValidLapTime->Caption = isFr ? "Temps max. tour valide (x meilleur):" : "Max Valid Lap Time (x best):";

    GroupTyrePressure->Caption = isFr ? "Pression (psi)" : "Pressure (psi)";
    LblPressLow->Caption = isFr ? "Minimum :" : "Low:";
    LblPressHigh->Caption = isFr ? "Maximum :" : "High:";
    LblPressFrontRearDiffMax->Caption = isFr ? "Diff pression Max Avant/Arrière :" : "Max Front/Rear Pressure Diff:";

    GroupTyreTemp->Caption = isFr ? "Température (C)" : "Temperature (C)";
    LblCamberMinSpread->Caption = isFr ? "Écart Intérieur/Extérieur Min :" : "Min Inner/Outer Spread:";
    LblCamberMaxSpread->Caption = isFr ? "Écart Intérieur/Extérieur Max :" : "Max Inner/Outer Spread:";

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
    LblDamperSlowSpeedRange->Caption = isFr ? "Largeur Bande Basse Vitesse (mm/s) :" : "Slow Speed Range (mm/s):";

    GroupHandling->Caption = isFr ? "Seuils de Détection" : "Detection Thresholds";
    LblBrakePedalMin->Caption = isFr ? "Pédale Frein Min :" : "Brake Pedal Min:";
    LblThrottleMaxBraking->Caption = isFr ? "Accélérateur Max au freinage :" : "Throttle Max Braking:";
    LblSteerMinCornering->Caption = isFr ? "Direction Min en virage :" : "Steer Min Cornering:";

    GroupThermal->Caption = isFr ? "Seuils Thermiques Moteur" : "Engine Thermal Thresholds";
    LblWaterTempMax->Caption = isFr ? "Temp Eau Max :" : "Max Water Temp:";
    LblOilTempMax->Caption = isFr ? "Temp Huile Max :" : "Max Oil Temp:";

    ButtonSave->Caption = isFr ? "Enregistrer" : "Save";
    ButtonCancel->Caption = isFr ? "Annuler" : "Cancel";
}

#pragma package(smart_init)


