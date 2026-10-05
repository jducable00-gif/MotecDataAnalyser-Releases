// LapDiagnostics.h - Phase 1 of the refactoring plan: pure, language-independent
// diagnostic heuristics over a single lap. Each function returns structured
// findings (raw numbers + flags); all EN/FR text rendering stays in the reporter
// (LapReportGenerator). Thresholds come from g_thresholds only - no literals here.

#ifndef LAPDIAGNOSTICS_H
#define LAPDIAGNOSTICS_H

#include <string>
#include <vector>
#include "MotecSetupAnalyzer.h"

// ---------------------------------------------------------------------------
// Data availability helpers (moved out of analyze_motec_setup.cpp)
// ---------------------------------------------------------------------------
bool hasPressureData(const LapStats& lap);
bool hasBrakeTempData(const LapStats& lap);
bool hasSuspensionTravelData(const LapStats& lap);
bool hasRideHeightData(const LapStats& lap);
bool hasWheelSpeedData(const LapStats& lap);
bool hasDamperVelocityData(const LapStats& lap);

// ---------------------------------------------------------------------------
// Channel validation + stability score (moved out of analyze_motec_setup.cpp)
// ---------------------------------------------------------------------------
std::string detectChannelMismatch(const LapStats& lap, ReportLanguage language = ReportLanguage::English);
int computeStabilityScore(const LapStats& lap);

// ---------------------------------------------------------------------------
// Pressure findings
// ---------------------------------------------------------------------------
struct PressureFinding {
    bool sideUnbalancedLeft = false;   // leftDiff > pressureSideDiffMax (front axle)
    double frontLeftAvg = 0.0, frontRightAvg = 0.0, frontSpread = 0.0;
    bool rearUnbalanced = false;       // rightDiff > pressureSideDiffMax (rear axle)
    double rearLeftAvg = 0.0, rearRightAvg = 0.0, rearSpread = 0.0;
    bool frontRearLarge = false;       // |front-rear| > pressureFrontRearDiffMax
    double avgFront = 0.0, avgRear = 0.0, frontRearDiff = 0.0;   // signed: rear - front
    bool lowOverall = false;           // both axles below profile minimum
    bool highOverall = false;          // both axles above profile maximum
};

PressureFinding checkTyrePressures(const LapStats& lap, ChannelProfile profile);

// ---------------------------------------------------------------------------
// Cooling / engine temperature findings
// ---------------------------------------------------------------------------
struct EngineTempFinding {
    bool waterTooHot = false;  // max > waterTempMax
    double waterMax = 0.0;
    bool oilTooHot = false;    // max > oilTempMax
    double oilMax = 0.0;
};

EngineTempFinding checkEngineTemperatures(const LapStats& lap);

// ---------------------------------------------------------------------------
// Cooling / setup status (suspension asymmetry + ride height) - single string slot
// ---------------------------------------------------------------------------
struct SetupStatusFinding {
    bool suspensionAsymmetry = false; // L/R travel diff > suspensionTravelMax on any axle
    double frontTravelDiff = 0.0, rearTravelDiff = 0.0;
    bool rideHeightImbalance = false; // |front-rear height| > rideHeightDiffMax
    double aeroFrontAvg = 0.0, aeroRearAvg = 0.0, aeroHeightDiff = 0.0;   // signed: rear - front
};

SetupStatusFinding checkSuspensionAndRideHeight(const LapStats& lap);

// ---------------------------------------------------------------------------
// Brake heat findings
// ---------------------------------------------------------------------------
struct BrakeHeatFinding {
    bool frontImbalance = false;  // |LF-RF| > brakeTempDiffMax
    double lfAvg = 0.0, rfAvg = 0.0, lrAvg = 0.0, rrAvg = 0.0;
    double frontSpread = 0.0, rearSpread = 0.0;
    bool rearImbalance = false;   // |LR-RR| > brakeTempDiffMax
    bool biasExtreme = false;     // |bias| > brakeBiasMax && frontAvg > brakeBiasFrontAvgMin
    double bias = 0.0, frontAvg = 0.0;
};

BrakeHeatFinding checkBrakeHeats(const LapStats& lap);

// ---------------------------------------------------------------------------
// Steering / yaw correlation verdicts (shared gate for corner response,
// steering response and speed-bucket blocks)
// ---------------------------------------------------------------------------
struct SteerYawVerdict {
    bool available = false;       // steerYawCount > minSteerYawSamples && avgSpeed > speedLowMax
    double correlation = 0.0;
    double averageSpeed = 0.0;
    bool understeer = false;      // corr < steerYawCorrUndersteer
    bool oversteer = false;       // corr > steerYawCorrOversteer

    // Speed bucket (no speed gate - only the sample-count gate)
    int speedBucket = 0;          // 0=low, 1=medium, 2=high
};

SteerYawVerdict evaluateSteerYaw(const LapStats& lap);

// ---------------------------------------------------------------------------
// Wheel speed check findings
// ---------------------------------------------------------------------------
struct WheelSpeedFinding {
    bool frontImbalance = false;  // |LF-RF| > wheelSpeedFrontDiffMax
    double lfAvg = 0.0, rfAvg = 0.0, lrAvg = 0.0, rrAvg = 0.0;
    double frontSpread = 0.0, rearSpread = 0.0;
    bool rearImbalance = false;   // |LR-RR| > wheelSpeedRearDiffMax
    bool tractionPatternMatch = false; // imbalance + matching L/R surface temp diff on an axle
};

WheelSpeedFinding checkWheelSpeeds(const LapStats& lap);

// ---------------------------------------------------------------------------
// Traction loss findings (primary pattern + secondary spread branch)
// ---------------------------------------------------------------------------
struct TractionLossFinding {
    bool rearPattern = false;     // rearDiff > max || rearTempDiff > max || |longG| > max
    double frontSpread = 0.0, rearSpread = 0.0;   // signed: right - left (avg wheel speed)
    double rearTempDiff = 0.0;    // signed: RR - CLR surface temp avg
    bool bothAxlesPattern = false;// front > FrontDiffMax && rear > RearDiffMin

    bool secondarySpread = false; // !issue + spread/pressure thresholds exceeded on the rear axle
};

TractionLossFinding checkTractionLoss(const LapStats& lap);

// ---------------------------------------------------------------------------
// Bumpstop findings (active corners list)
// ---------------------------------------------------------------------------
struct BumpStopFinding {
    std::vector<int> activeCorners;  // LF=0, RF=1, LR=2, RR=3 with max > bumpStopMin
};

BumpStopFinding checkBumpStops(const LapStats& lap);

// ---------------------------------------------------------------------------
// Suspension travel findings (avg per axle)
// ---------------------------------------------------------------------------
struct SuspTravelFinding {
    bool frontHigh = false;   // avgFront > suspensionAvgTravelFrontMax
    double avgFront = 0.0, avgRear = 0.0;
    bool rearHigh = false;    // avgRear > suspensionAvgTravelRearMax
};

SuspTravelFinding checkSuspensionTravel(const LapStats& lap);

// ---------------------------------------------------------------------------
// Aero balance + LSD torque findings (single status slot each)
// ---------------------------------------------------------------------------
struct AeroLsdFinding {
    bool aeroFrontHeavy = false;  // avgBalance > aeroBalanceMax
    double aeroBalanceAvg = 0.0;
    bool aeroRearHeavy = false;   // avgBalance < -aeroBalanceMax

    bool lsdHighLocking = false;  // torque > profile lsdHigh (both ifs can fire)
    double lsdTorqueAvg = 0.0;
    bool lsdLowLocking = false;   // torque < profile lsdLow
};

AeroLsdFinding checkAeroAndLsd(const LapStats& lap, ChannelProfile profile);

// ---------------------------------------------------------------------------
// Aero height finding (direct issue)
// ---------------------------------------------------------------------------
struct AeroHeightFinding {
    bool rearMuchHigher = false;  // speed.max > aeroSpeedMin && (rearH - frontH) > aeroHeightDiffMax
};

AeroHeightFinding checkAeroHeight(const LapStats& lap);

// ---------------------------------------------------------------------------
// Brake torque balance findings (EVO profile channels)
// ---------------------------------------------------------------------------
struct BrakeTorqueFinding {
    bool dataAvailable = false;   // all four brakeTorque counts > 0 && total > brakeTorqueMin
    double frontSharePct = 0.0;   // 100 * front / total
    bool frontBiased = false;     // share > brakeTorqueFrontShareMax
    bool rearBiased = false;      // share < brakeTorqueFrontShareMin
    bool frontAxleUnbalanced = false; // |LF-RF| > ratio * axleSum (front)
    double lfAvg = 0.0, rfAvg = 0.0, lrAvg = 0.0, rrAvg = 0.0;
    double frontSpread = 0.0, rearSpread = 0.0;   // signed: right - left
    bool rearAxleUnbalanced = false;              // |LR-RR| > ratio * axleSum (rear)
};

BrakeTorqueFinding checkBrakeTorques(const LapStats& lap);

// ---------------------------------------------------------------------------
// Brake bias setting finding (AMS2 profile channel)
// ---------------------------------------------------------------------------
struct BrakeBiasSettingFinding {
    bool dataAvailable = false;   // count > 0
    double avgFrontPct = 0.0;     // brakeBiasSetting average (% front)
    bool heavilyFront = false;    // > brakeBiasSettingFrontMax
    bool heavilyRear = false;     // < brakeBiasSettingRearMin
};

BrakeBiasSettingFinding checkBrakeBiasSetting(const LapStats& lap);

// ---------------------------------------------------------------------------
// Wheel slip ratio findings (EVO profile channels)
// ---------------------------------------------------------------------------
struct WheelSlipFinding {
    bool dataAvailable = false;   // all four wheelSlip counts > 0
    double rearPct = 0.0, frontPct = 0.0;   // scaled averages (% or raw)
    bool rearHigh = false;        // rearPct > wheelSlipMax
    bool frontHigh = false;       // frontPct > wheelSlipMax
};

WheelSlipFinding checkWheelSlips(const LapStats& lap);

// ---------------------------------------------------------------------------
// TCS / ABS activation findings (0..1 or 0..100 source scaling)
// ---------------------------------------------------------------------------
struct ActivationFinding {
    bool tcsData = false;         // count > 0
    double tcsPct = 0.0;          // scaled average (% of lap)
    bool tcsHigh = false;         // pct > tcsActiveMax

    bool absData = false;
    double absPct = 0.0;
    bool absHigh = false;         // pct > absActiveMax
};

ActivationFinding checkActivations(const LapStats& lap);

// ---------------------------------------------------------------------------
// Tyre air temperature findings (EVO profile channels)
// ---------------------------------------------------------------------------
struct AirTempFinding {
    bool dataAvailable = false;   // all four tyreAirTemp counts > 0
    double lfAvg = 0.0, rfAvg = 0.0, lrAvg = 0.0, rrAvg = 0.0;
    bool frontImbalance = false;  // |LF-RF| > tyreAirTempLrDiffMax
    bool rearImbalance = false;   // |LR-RR| > tyreAirTempLrDiffMax
};

AirTempFinding checkTyreAirTemps(const LapStats& lap);

// ---------------------------------------------------------------------------
// Suspension velocity finding (AMS2 profile channels) - peak corner detection
// ---------------------------------------------------------------------------
struct SuspVelocityFinding {
    bool dataAvailable = false;   // all four suspVel counts > 0
    double peakValue = 0.0;       // max of the four corners' .max
    int peakCorner = 0;           // LF=0, RF=1, LR=2, RR=3 (>= chain: first match wins)
    bool tooFast = false;         // peak > suspensionVelocityMax
};

SuspVelocityFinding checkSuspensionVelocities(const LapStats& lap);

#endif // LAPDIAGNOSTICS_H
