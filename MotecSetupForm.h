//---------------------------------------------------------------------------
// MotecSetupForm.h — Déclaration de la fenêtre principale (TFormMotecSetup).
//
// Form VCL qui orchestre l'analyse : sélection des fichiers CSV / LD-LDX,
// options (rapport complet, langue, profil de canaux), barre de progression
// et affichage du rapport dans un TMemo. Les fonctions d'analyse elles-mêmes
// sont déclarées dans MotecSetupAnalyzer.h.
//---------------------------------------------------------------------------

#pragma once

#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.Forms.hpp>
#include <Vcl.ExtCtrls.hpp>
#include <Vcl.Graphics.hpp>

#include "MotecSetupAnalyzer.h"
#include <Vcl.ComCtrls.hpp>
#include <Vcl.WinXCtrls.hpp>
#include <Vcl.ExtCtrls.hpp>
#include "Ams2LiveCapture.h"
#include "AcsEvoLiveCapture.h"
#include "LapReportGenerator.h"
#include <map>

// Active live-capture backend.
enum LiveMode { LIVE_AMS2 = 0, LIVE_EVO = 1 };

class TFormMotecSetup : public TForm
{
__published:
    TButton *ButtonLoad;
    TButton *ButtonSettings;
    TPageControl *PageControlReport;
    TTabSheet *TabSummary;
    TTabSheet *TabGeneral;
    TTabSheet *TabTyre;
    TTabSheet *TabBrake;
    TTabSheet *TabSuspension;
    TTabSheet *TabEngineCooling;
    TTabSheet *TabHandling;
    TMemo *MemoSummary;
    TMemo *MemoGeneral;
    TMemo *MemoTyre;
    TMemo *MemoBrake;
    TMemo *MemoSuspension;
    TMemo *MemoEngineCooling;
    TMemo *MemoHandling;
	TStatusBar *StatusBar1;
	TButton *Button1;
    TProgressBar *ProgressBarExport;    TComboBox *ChannelProfileCombo;
    TLabel *LabelTitle;
    TLabel *LabelSubtitle;
    TPanel *AccentLine;
    TLabel *LabelProfile;
    TLabel *LabelLanguage;
    TPaintBox *TireTempPaintBox;
    TPaintBox *DamperFLPaintBox;
    TPaintBox *DamperFRPaintBox;
    TPaintBox *DamperRLPaintBox;
    TPaintBox *DamperRRPaintBox;
    TCheckBox *LiveCaptureCheckBox;
    TLabel *LabelLiveStatus;
    TShape *LiveLed;
    TTimer *LiveTimer;
    TTimer *LiveConnectTimer;
    TPaintBox *BrakeTempPaintBox;
    TComboBox *LanguageCombo;
    TCheckBox *AutoDetectTrackCheckBox;
    TButton *ButtonResetSession;

    void __fastcall ButtonLoadClick(TObject *Sender);
	void __fastcall Button1Click(TObject *Sender);
    void __fastcall ButtonSettingsClick(TObject *Sender);
    void __fastcall LanguageComboChange(TObject *Sender);
    void __fastcall ChannelProfileComboChange(TObject *Sender);
    void __fastcall FormResize(TObject *Sender);
    void __fastcall TireTempPaintBoxPaint(TObject *Sender);
    void __fastcall DamperFLPaintBoxPaint(TObject *Sender);
    void __fastcall DamperFRPaintBoxPaint(TObject *Sender);
    void __fastcall DamperRLPaintBoxPaint(TObject *Sender);
    void __fastcall DamperRRPaintBoxPaint(TObject *Sender);
    void __fastcall BrakeTempPaintBoxPaint(TObject *Sender);
    void __fastcall LiveCaptureCheckBoxClick(TObject *Sender);
    void __fastcall LiveTimerTimer(TObject *Sender);
    void __fastcall LiveConnectTimerTimer(TObject *Sender);
    void __fastcall ButtonResetSessionClick(TObject *Sender);
    void __fastcall AutoDetectTrackCheckBoxClick(TObject *Sender);
    void __fastcall resetLiveSession();
    void __fastcall updateResetButtonState();
    void __fastcall TranslateUI();


public:
    struct SuspensionCornerData {
        double bins[41] = {0}; // normalized 0-1 damper velocity distribution
        double travel = 0.0;   // average suspension travel (mm)
        double bumpstop = 0.0; // max bumpstop engagement (mm)
        bool issue = false;    // damper evaluation issue
        bool valid = false;    // has data
    };

    __fastcall TFormMotecSetup(TComponent* Owner);
    ~TFormMotecSetup();
    static ReportLanguage ReadReportLanguageSetting();
    bool __fastcall AnalyzeSelectedFile(const std::string& filePath, std::string& errorMessage);
    bool __fastcall AnalyzeSelectedLdFile(const std::string& filePath, std::string& errorMessage);
    void __fastcall resetAnalysisGraphics();
    void __fastcall stopLiveCapture();
    void __fastcall processLiveFrame(const Ams2SharedMemory& frame);
    void __fastcall processLiveFrameEVO(const SPageFilePhysics& phys, const SPageFileGraphicEvo& gfx);
    void __fastcall finalizeLiveLap();
    void __fastcall generateLiveReport();
    void __fastcall UpdateReportTabs(const ReportResult& result);
    void __fastcall DrawModernHistogram(TCanvas* canvas, int x, int y, int width, int height, const SuspensionCornerData& stats, const String& label);

    // Internal update helpers (NOT VCL event handlers — must stay out of __published).
    void __fastcall UpdateTireTemps(const std::vector<LapStats>& laps);
    void __fastcall UpdateBrakeTemps(const std::vector<LapStats>& laps);
    void __fastcall UpdateSuspension(std::vector<LapStats>& laps);

    struct TireTempData {
        double inner = 0.0;
        double middle = 0.0;
        double outer = 0.0;
        double pressure = 0.0;
        bool pressureValid = false;
        bool valid = false;
    };
    TireTempData tireTemps[4]; // 0=FL, 1=FR, 2=RL, 3=RR
    bool hasTireTempData = false;

    struct BrakeTempData {
        double avg = 0.0;
        double max = 0.0;
        bool valid = false;
    };
    BrakeTempData brakeTemps[4]; // 0=FL, 1=FR, 2=RL, 3=RR
    bool hasBrakeTempData = false;

    struct FluidTempData {
        double avg = 0.0;
        double max = 0.0;
        bool valid = false;
    };
    FluidTempData waterTemp;
    FluidTempData oilTemp;

    double aeroBalanceValue = 0.0;
    bool aeroBalanceValid = false;

    SuspensionCornerData suspensionData[4]; // 0=LF, 1=RF, 2=LR, 3=RR
    double suspensionMaxBin = 0.0;          // shared Y-axis max (highest bin across all 4 corners)

    // --- Live capture state ---
    bool liveActive = false;
    bool liveConnected = false;
    bool liveInPit = false;
    LiveMode liveMode = LIVE_AMS2;
    bool liveLapInProgress = false;
    LapStats liveCurrentLap;
    std::vector<LapStats> liveLaps;
    int liveLapNumber = 0;
    double liveLapStartTime = 0.0;
    bool liveWasOnTrack = false;
    std::string liveTrackName;

    // --- AMS2 State Tracking (for event detection) ---
    int lastPitMode = -1;           // To detect pit exit and teleport transitions
    unsigned int lastCompletedLaps = 0; // The value from the game's mCompletedLaps field
    unsigned int sessionLapCounter = 0;  // Our internal counter that resets on pit entry/exit
	float previous_mCurrentTime = 0.0;
    bool firstLapStarted = false;
};

extern PACKAGE TFormMotecSetup *FormMotecSetup;
