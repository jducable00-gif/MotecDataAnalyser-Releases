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

    // General Tab
    TGroupBox *GroupGeneral;
    TLabel *LblMinDamperSamples;
    TEdit *EditMinDamperSamples;

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
    TLabel *LblPressFrontRearDiffMax;
    TEdit *EditPressFrontRearDiffMax;

    TGroupBox *GroupTyreTemp;
    TLabel *LblCamberMinSpread;
    TEdit *EditCamberMinSpread;
    TLabel *LblCamberMaxSpread;
    TEdit *EditCamberMaxSpread;

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
    TLabel *LblDamperSlowSpeedRange;
    TEdit *EditDamperSlowSpeedRange;

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
