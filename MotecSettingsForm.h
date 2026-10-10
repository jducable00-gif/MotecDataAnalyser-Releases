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
    TLabel *LblHscTarget;
    TEdit *EditHscTarget;
    TLabel *LblLscTarget;
    TEdit *EditLscTarget;
    TLabel *LblLsrTarget;
    TEdit *EditLsrTarget;
    TLabel *LblHsrTarget;
    TEdit *EditHsrTarget;
    TLabel *LblZoneToleranceHsc;
    TEdit *EditZoneToleranceHsc;
    TLabel *LblZoneToleranceLsc;
    TEdit *EditZoneToleranceLsc;
    TLabel *LblZoneToleranceLsr;
    TEdit *EditZoneToleranceLsr;
    TLabel *LblZoneToleranceHsr;
    TEdit *EditZoneToleranceHsr;
    TLabel *LblDamperSlowSpeedRange;
    TEdit *EditDamperSlowSpeedRange;
    TLabel *LblHistogramYOffset;
    TEdit *EditHistogramYOffset;
    TLabel *LblHistogramMinYPct;
    TEdit *EditHistogramMinYPct;

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

    // PMR UDP Tab
    TTabSheet *TabPmr;
    TGroupBox *GroupPmrNetwork;
    TLabel *LblPmrBindAddress;
    TEdit *EditPmrBindAddress;
    TLabel *LblPmrPort;
    TEdit *EditPmrPort;
    TLabel *LblPmrHint;

    // Buttons
    TButton *ButtonSave;
    TButton *ButtonCancel;

    void __fastcall ButtonSaveClick(TObject *Sender);
    void __fastcall ButtonCancelClick(TObject *Sender);
    void __fastcall FormShow(TObject *Sender);
    // DFM-referenced handlers must stay in __published so VCL streaming can resolve them by name.
    void __fastcall EditPmrBindAddressChange(TObject *Sender);
    void __fastcall EditPmrBindAddressEnter(TObject *Sender);
    void __fastcall EditPmrBindAddressLeave(TObject *Sender);

private:
    bool pmrAddrShowingPlaceholder = false;  // true while the gray "127.0.0.1" hint stands for an empty value
    bool pmrAddrEditedOnFocus = false;       // set by OnChange: user actually typed something since focus

public:
    __fastcall TFormMotecSettings(TComponent* Owner);
    void LoadFromThresholds();
    void SaveToThresholds();
    void TranslateUI();
};

extern TFormMotecSettings *FormMotecSettings;
