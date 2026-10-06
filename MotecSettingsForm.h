//---------------------------------------------------------------------------
// MotecSettingsForm.h — Dialog for configuring analysis thresholds.
//---------------------------------------------------------------------------

#pragma once

#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.Forms.hpp>
#include <Vcl.ExtCtrls.hpp>
#include <Vcl.Graphics.hpp>
#include <Vcl.ComCtrls.hpp>
#include "MotecSetupAnalyzer.h"

class TFormMotecSettings : public TForm
{
__published:
    TPageControl *PageControl;
    TTabSheet *TabGeneral;
    TTabSheet *TabTyre;
    TTabSheet *TabHandling;
    TTabSheet *TabThermal;
    TTabSheet *TabSuspension;
    TTabSheet *TabBrake;

    // General Tab
    TGroupBox *GroupGeneral;
    TLabel *LblMinDamperSamples;
    TEdit *EditMinDamperSamples;
    TLabel *LblMinHandlingSamples;
    TEdit *EditMinHandlingSamples;
    TLabel *LblMinSteerYawSamples;
    TEdit *EditMinSteerYawSamples;
    TLabel *LblHandlingConfidenceSamples;
    TEdit *EditHandlingConfidenceSamples;

    // Valid Lap Time Tab (General)
    TGroupBox *GroupLapValidity;
    TLabel *LblMinValidLapTime;
    TEdit *EditMinValidLapTime;
    TLabel *LblMaxValidLapTime;
    TEdit *EditMaxValidLapTime;

    // Tyre Tab
    TGroupBox *GroupTyrePressure;
    TLabel *LblPressLow;
    TEdit *EditPressLow;
    TLabel *LblPressHigh;
    TEdit *EditPressHigh;
    TLabel *LblPressSideDiffMax;
    TEdit *EditPressSideDiffMax;
    TLabel *LblPressFrontRearDiffMax;
    TEdit *EditPressFrontRearDiffMax;

    TGroupBox *GroupTyreTemp;
    TLabel *LblCamberMinSpread;
    TEdit *EditCamberMinSpread;
    TLabel *LblCamberMaxSpread;
    TEdit *EditCamberMaxSpread;

    // Damper Tab
    TGroupBox *GroupDamper;
    TLabel *LblDamperLowSpeedThreshold;
    TEdit *EditDamperLowSpeedThreshold;
    TLabel *LblDamperAsymmetryThreshold;
    TEdit *EditDamperAsymmetryThreshold;
    TLabel *LblDamperHighSpeedShareMax;
    TEdit *EditDamperHighSpeedShareMax;
    TLabel *LblDamperLowSpeedShareMin;
    TEdit *EditDamperLowSpeedShareMin;

    // Damper 4-Zone Targets Tab
    TGroupBox *GroupDamperZones;
    TLabel *LblHscTargetMin;
    TEdit *EditHscTargetMin;
    TLabel *LblHscTargetMax;
    TEdit *EditHscTargetMax;
    TLabel *LblLscTargetMin;
    TEdit *EditLscTargetMin;
    TLabel *LblLscTargetMax;
    TEdit *EditLscTargetMax;
    TLabel *LblLsrTargetMin;
    TEdit *EditLsrTargetMin;
    TLabel *LblLsrTargetMax;
    TEdit *EditLsrTargetMax;
    TLabel *LblHsrTargetMin;
    TEdit *EditHsrTargetMin;
    TLabel *LblHsrTargetMax;
    TEdit *EditHsrTargetMax;
    TLabel *LblZoneModerateBand;
    TEdit *EditZoneModerateBand;

    // Handling Tab
    TGroupBox *GroupHandling;
    TLabel *LblBrakePedalMin;
    TEdit *EditBrakePedalMin;
    TLabel *LblThrottleMaxBraking;
    TEdit *EditThrottleMaxBraking;
    TLabel *LblSteerMinCornering;
    TEdit *EditSteerMinCornering;

    // Thermal Tab
    TGroupBox *GroupThermal;
    TLabel *LblWaterTempMax;
    TEdit *EditWaterTempMax;
    TLabel *LblOilTempMax;
    TEdit *EditOilTempMax;

    // Suspension Tab
    TGroupBox *GroupSuspension;
    TLabel *LblSuspTravelMax;
    TEdit *EditSuspTravelMax;
    TLabel *LblRideHeightDiffMax;
    TEdit *EditRideHeightDiffMax;
    TLabel *LblSuspVelocityMax;
    TEdit *EditSuspVelocityMax;
    TLabel *LblSuspAvgFrontMax;
    TEdit *EditSuspAvgFrontMax;
    TLabel *LblSuspAvgRearMax;
    TEdit *EditSuspAvgRearMax;

    // Brake Tab
    TGroupBox *GroupBrake;
    TLabel *LblBrakeTempDiffMax;
    TEdit *EditBrakeTempDiffMax;
    TLabel *LblBrakeBiasMax;
    TEdit *EditBrakeBiasMax;
    TLabel *LblBrakeBiasFrontMin;
    TEdit *EditBrakeBiasFrontMin;
    TLabel *LblBrakeTorqueMin;
    TEdit *EditBrakeTorqueMin;
    TLabel *LblBrakeChannelMismatchMax;
    TEdit *EditBrakeChannelMismatchMax;
    TLabel *LblBrakeTorqueFrontShareMax;
    TEdit *EditBrakeTorqueFrontShareMax;
    TLabel *LblBrakeTorqueFrontShareMin;
    TEdit *EditBrakeTorqueFrontShareMin;
    TLabel *LblBrakeTorqueLrDiffRatio;
    TEdit *EditBrakeTorqueLrDiffRatio;
    TLabel *LblBrakeBiasSettingFrontMax;
    TEdit *EditBrakeBiasSettingFrontMax;
    TLabel *LblBrakeBiasSettingRearMin;
    TEdit *EditBrakeBiasSettingRearMin;

    // Buttons
    TButton *ButtonSave;
    TButton *ButtonCancel;

    void __fastcall ButtonSaveClick(TObject *Sender);
    void __fastcall ButtonCancelClick(TObject *Sender);
    void __fastcall FormShow(TObject *Sender);

public:
    __fastcall TFormMotecSettings(TComponent* Owner);
    void LoadFromThresholds();
    void SaveToThresholds();
    void TranslateUI();
};

extern TFormMotecSettings *FormMotecSettings;
