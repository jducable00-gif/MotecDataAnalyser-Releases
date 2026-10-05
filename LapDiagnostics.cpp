// ===============================
// LapDiagnostics (Phase 1 of the refactoring plan)
// Pure heuristics: each check reads a LapStats and returns a structured finding.
// No language, no formatting — rendering lives in RecommendationEngine / LapReportGenerator.
// Ported verbatim from analyze_motec_setup.cpp; hardcoded literals replaced by g_thresholds fields.
// ===============================

#include <algorithm>
#include <cmath>
#include "LapDiagnostics.h"

// ---------- Data availability ----------

bool hasPressureData(const LapStats& lap) {
    return lap.pressureLF.count > 0 && lap.pressureRF.count > 0 &&
           lap.pressureLR.count > 0 && lap.pressureRR.count > 0;
}

bool hasBrakeTempData(const LapStats& lap) {
    return lap.brakeTempLF.count > 0 && lap.brakeTempRF.count > 0 &&
           lap.brakeTempLR.count > 0 && lap.brakeTempRR.count > 0;
}

bool hasSuspensionTravelData(const LapStats& lap) {
    return lap.susTravelLF.count > 0 && lap.susTravelRF.count > 0 &&
           lap.susTravelLR.count > 0 && lap.susTravelRR.count > 0;
}

bool hasRideHeightData(const LapStats& lap) {
    return lap.aeroHeight0.count > 0 && lap.aeroHeight1.count > 0 &&
           lap.aeroHeight2.count > 0 && lap.aeroHeight3.count > 0;
}

bool hasWheelSpeedData(const LapStats& lap) {
    return lap.wheelSpeedLF.count > 0 && lap.wheelSpeedRF.count > 0 &&
           lap.wheelSpeedLR.count > 0 && lap.wheelSpeedRR.count > 0;
}

bool hasDamperVelocityData(const LapStats& lap) {
    return lap.damperLF.histogram.totalCount >= g_thresholds.minDamperSamples &&
           lap.damperRF.histogram.totalCount >= g_thresholds.minDamperSamples &&
           lap.damperLR.histogram.totalCount >= g_thresholds.minDamperSamples &&
           lap.damperRR.histogram.totalCount >= g_thresholds.minDamperSamples;
}

// ---------- Channel validation / stability ----------

std::string detectChannelMismatch(const LapStats& lap, ReportLanguage language) {
    auto tr = [&](const std::string& en, const std::string& fr) -> std::string {
        return language == ReportLanguage::French ? fr : en;
    };

    if (hasBrakeTempData(lap)) {
        double frontAvg = (lap.brakeTempLF.average() + lap.brakeTempRF.average()) * 0.5;
        double rearAvg  = (lap.brakeTempLR.average() + lap.brakeTempRR.average()) * 0.5;
        if (rearAvg - frontAvg > g_thresholds.brakeChannelMismatchMax) {
            return tr(
                "Possible front/rear brake-channel mismatch: rear brake temperatures are much higher than front, which may indicate misassigned channels or a source-specific alias issue.",
                "Possible discordance avant/arrière des canaux de frein : les températures de frein arrière sont bien supérieures à l'avant, ce qui peut indiquer des canaux mal affectés ou un alias spécifique à la source."
            );
        }
    }

    if (hasPressureData(lap)) {
        double frontAvg = (lap.pressureLF.average() + lap.pressureRF.average()) * 0.5;
        double rearAvg  = (lap.pressureLR.average() + lap.pressureRR.average()) * 0.5;
        if (std::abs(frontAvg - rearAvg) > g_thresholds.pressureChannelMismatchMax && (rearAvg > frontAvg + g_thresholds.pressureChannelMismatchMin)) {
            return tr(
                "Possible front/rear pressure-channel mismatch: rear pressures are significantly higher than front, which may indicate a non-standard LD profile or misread axle mapping.",
                "Possible discordance avant/arrière des canaux de pression : les pressions arrière sont nettement plus élevées qu'à l'avant, ce qui peut indiquer un profil LD non standard ou un mauvais mapping d'essieu."
            );
        }
    }

    if (hasSuspensionTravelData(lap)) {
        double frontAvg = (lap.susTravelLF.average() + lap.susTravelRF.average()) * 0.5;
        double rearAvg  = (lap.susTravelLR.average() + lap.susTravelRR.average()) * 0.5;
        if (std::abs(frontAvg - rearAvg) > g_thresholds.suspensionChannelMismatchMax && rearAvg > frontAvg + g_thresholds.suspensionChannelMismatchMin) {
            return tr(
                "Possible front/rear suspension-channel mismatch: rear travel differs strongly from front and may reflect a swapped or non-standard axle mapping.",
                "Possible discordance avant/arrière des canaux de suspension : la course arrière diffère fortement de l'avant et peut refléter un essieu inversé ou un mapping d'essieu non standard."
            );
        }
    }

    return "";
}

int computeStabilityScore(const LapStats& lap) {
    double score = 100.0;

    if (lap.steerYawCount > g_thresholds.minSteerYawSamples) {
        double corr = lap.steerYawCorrelation();
        if (corr < g_thresholds.stabilityCorrUndersteer)
            score -= 18.0;
        else if (corr > g_thresholds.stabilityCorrOversteer)
            score -= 10.0;
    }

    if (lap.wheelSpeedLF.count > 0 && lap.wheelSpeedRF.count > 0 &&
        lap.wheelSpeedLR.count > 0 && lap.wheelSpeedRR.count > 0) {
        double frontDiff = std::abs(lap.wheelSpeedLF.average() - lap.wheelSpeedRF.average());
        double rearDiff  = std::abs(lap.wheelSpeedLR.average() - lap.wheelSpeedRR.average());
        if (frontDiff > g_thresholds.stabilityWheelSpeedDiffMax) score -= 12.0;
        if (rearDiff  > g_thresholds.stabilityWheelSpeedDiffMax) score -= 12.0;
    }

    if (lap.pressureLF.count > 0 && lap.pressureRF.count > 0) {
        double diff = std::abs(lap.pressureLF.average() - lap.pressureRF.average());
        if (diff > g_thresholds.stabilityPressureDiffMax) score -= 8.0;
    }

    if (lap.pressureLR.count > 0 && lap.pressureRR.count > 0) {
        double diff = std::abs(lap.pressureLR.average() - lap.pressureRR.average());
        if (diff > g_thresholds.stabilityPressureDiffMax) score -= 8.0;
    }

    if (lap.surfTempCFL.count > 0 && lap.surfTempCRF.count > 0) {
        double diff = std::abs(lap.surfTempCFL.average() - lap.surfTempCRF.average());
        if (diff > g_thresholds.stabilityTempDiffMax) score -= 10.0;
    }

    if (lap.surfTempCLR.count > 0 && lap.surfTempCRR.count > 0) {
        double diff = std::abs(lap.surfTempCLR.average() - lap.surfTempCRR.average());
        if (diff > g_thresholds.stabilityTempDiffMax) score -= 10.0;
    }

    if (score < 0.0) score = 0.0;
    if (score > 100.0) score = 100.0;
    return static_cast<int>(std::round(score));
}

// ---------- Tyre pressures ----------

PressureFinding checkTyrePressures(const LapStats& lap, ChannelProfile profile) {
    PressureFinding f;
    if (!hasPressureData(lap))
        return f;

    const double lf = lap.pressureLF.average();
    const double rf = lap.pressureRF.average();
    const double lr = lap.pressureLR.average();
    const double rr = lap.pressureRR.average();

    f.frontLeftAvg  = lf;
    f.frontRightAvg = rf;
    f.rearLeftAvg   = lr;
    f.rearRightAvg  = rr;
    f.frontSpread   = std::abs(lf - rf);
    f.rearSpread    = std::abs(lr - rr);

    const double avgFront = (lf + rf) * 0.5;
    const double avgRear  = (lr + rr) * 0.5;
    // Signed front/rear difference, same convention as the original report:
    // positive means the FRONT axle is higher than the rear.
    f.frontRearDiff = avgFront - avgRear;

    const double pressureLow  = g_thresholds.pressureLow(profile);
    const double pressureHigh = g_thresholds.pressureHigh(profile);

    if (f.frontSpread > g_thresholds.pressureSideDiffMax)
        f.sideUnbalancedLeft = true;   // front axle L/R unbalanced
    if (f.rearSpread  > g_thresholds.pressureSideDiffMax)
        f.rearUnbalanced     = true;

    if (std::abs(f.frontRearDiff) > g_thresholds.pressureFrontRearDiffMax)
        f.frontRearLarge = true;

    f.avgFront = avgFront;
    f.avgRear  = avgRear;

    if (avgFront < pressureLow && avgRear < pressureLow)
        f.lowOverall = true;
    else if (avgFront > pressureHigh && avgRear > pressureHigh)
        f.highOverall = true;

    return f;
}

// ---------- Engine temperatures ----------

EngineTempFinding checkEngineTemperatures(const LapStats& lap) {
    EngineTempFinding f;
    if (lap.waterTemp.count > 0 && lap.waterTemp.max > g_thresholds.waterTempMax) {
        f.waterTooHot = true;
        f.waterMax = lap.waterTemp.max;
    }
    if (lap.oilTemp.count > 0 && lap.oilTemp.max > g_thresholds.oilTempMax) {
        f.oilTooHot = true;
        f.oilMax = lap.oilTemp.max;
    }
    return f;
}

// ---------- Suspension / ride height (cooling & setup check) ----------

SetupStatusFinding checkSuspensionAndRideHeight(const LapStats& lap) {
    SetupStatusFinding f;
    if (lap.rpm.count == 0)
        return f;   // original block is gated on rpm availability

    if (hasSuspensionTravelData(lap)) {
        double frontTravel = std::abs(lap.susTravelLF.average() - lap.susTravelRF.average());
        double rearTravel  = std::abs(lap.susTravelLR.average() - lap.susTravelRR.average());
        f.frontTravelDiff = frontTravel;
        f.rearTravelDiff  = rearTravel;
        if (frontTravel > g_thresholds.suspensionTravelMax || rearTravel > g_thresholds.suspensionTravelMax)
            f.suspensionAsymmetry = true;
    }

    if (hasRideHeightData(lap)) {
        double frontAverage = (lap.aeroHeight0.average() + lap.aeroHeight1.average()) * 0.5;
        double rearAverage  = (lap.aeroHeight2.average() + lap.aeroHeight3.average()) * 0.5;
        f.aeroFrontAvg = frontAverage;
        f.aeroRearAvg  = rearAverage;
        f.aeroHeightDiff = std::abs(frontAverage - rearAverage);
        if (f.aeroHeightDiff > g_thresholds.rideHeightDiffMax)
            f.rideHeightImbalance = true;   // wins over suspensionAsymmetry in the report text
    }

    return f;
}

// ---------- Brake heat ----------

BrakeHeatFinding checkBrakeHeats(const LapStats& lap) {
    BrakeHeatFinding f;
    if (!hasBrakeTempData(lap))
        return f;

    const double lf = lap.brakeTempLF.average();
    const double rf = lap.brakeTempRF.average();
    const double lr = lap.brakeTempLR.average();
    const double rr = lap.brakeTempRR.average();

    f.lfAvg = lf; f.rfAvg = rf; f.lrAvg = lr; f.rrAvg = rr;
    f.frontSpread = std::abs(lf - rf);
    f.rearSpread  = std::abs(lr - rr);

    const double frontAvg = (lf + rf) * 0.5;
    const double rearAvg  = (lr + rr) * 0.5;
    f.frontAvg = frontAvg;
    f.bias     = frontAvg - rearAvg;   // signed: positive means more heat up front

    if (f.frontSpread > g_thresholds.brakeTempDiffMax)
        f.frontImbalance = true;
    if (f.rearSpread  > g_thresholds.brakeTempDiffMax)
        f.rearImbalance  = true;
    if (std::abs(f.bias) > g_thresholds.brakeBiasMax && frontAvg > g_thresholds.brakeBiasFrontAvgMin)
        f.biasExtreme = true;

    return f;
}

// ---------- Steering / yaw response ----------

SteerYawVerdict evaluateSteerYaw(const LapStats& lap) {
    SteerYawVerdict v;
    if (lap.steerYawCount <= g_thresholds.minSteerYawSamples)
        return v;   // not enough synchronized samples at all

    const double corr = lap.steerYawCorrelation();
    const double avgSpeed = lap.speed.average();
    v.correlation  = corr;
    v.averageSpeed = avgSpeed;

    if (avgSpeed < g_thresholds.speedLowMax)          v.speedBucket = 0;   // low-speed corners
    else if (avgSpeed < g_thresholds.speedMediumMax)  v.speedBucket = 1;   // medium-speed corners
    else                                              v.speedBucket = 2;   // high-speed corners

    // The corner-response / steering-response checks additionally require the lap to be fast enough.
    v.available = avgSpeed > g_thresholds.speedLowMax;
    if (!v.available)
        return v;

    if (corr < g_thresholds.steerYawCorrUndersteer)
        v.understeer = true;
    else if (corr > g_thresholds.steerYawCorrOversteer)
        v.oversteer  = true;

    return v;
}

// ---------- Wheel speeds ----------

WheelSpeedFinding checkWheelSpeeds(const LapStats& lap) {
    WheelSpeedFinding f;
    if (!hasWheelSpeedData(lap))
        return f;

    const double lf = lap.wheelSpeedLF.average();
    const double rf = lap.wheelSpeedRF.average();
    const double lr = lap.wheelSpeedLR.average();
    const double rr = lap.wheelSpeedRR.average();

    f.lfAvg = lf; f.rfAvg = rf; f.lrAvg = lr; f.rrAvg = rr;
    f.frontSpread = std::abs(lf - rf);
    f.rearSpread  = std::abs(lr - rr);

    if (f.frontSpread > g_thresholds.wheelSpeedFrontDiffMax)
        f.frontImbalance = true;
    if (f.rearSpread  > g_thresholds.wheelSpeedRearDiffMax)
        f.rearImbalance  = true;

    // Traction-loss pattern: speed imbalance matched by a temperature imbalance on the same axle.
    bool tempMatchFront = false, tempMatchRear = false;
    if (lap.surfTempCFL.count > 0 && lap.surfTempCRF.count > 0) {
        double tDiff = std::abs(lap.surfTempCFL.average() - lap.surfTempCRF.average());
        if (tDiff > g_thresholds.wheelSpeedTempDiffMax) tempMatchFront = true;
    }
    if (lap.surfTempCLR.count > 0 && lap.surfTempCRR.count > 0) {
        double tDiff = std::abs(lap.surfTempCLR.average() - lap.surfTempCRR.average());
        if (tDiff > g_thresholds.wheelSpeedTempDiffMax) tempMatchRear = true;
    }

    f.tractionPatternMatch = (f.frontImbalance || f.rearImbalance) && (tempMatchFront || tempMatchRear);
    return f;
}

// ---------- Traction loss ----------

TractionLossFinding checkTractionLoss(const LapStats& lap) {
    TractionLossFinding f;
    if (!hasWheelSpeedData(lap))
        return f;

    const double lf = lap.wheelSpeedLF.average();
    const double rf = lap.wheelSpeedRF.average();
    const double lr = lap.wheelSpeedLR.average();
    const double rr = lap.wheelSpeedRR.average();

    // Signed spreads: positive means the RIGHT wheel is faster than the left.
    f.frontSpread = rf - lf;
    f.rearSpread  = rr - lr;

    if (lap.surfTempCLR.count > 0 && lap.surfTempCRR.count > 0)
        f.rearTempDiff = std::abs(lap.surfTempCLR.average() - lap.surfTempCRR.average());

    const double frontAbs = std::abs(f.frontSpread);
    const double rearAbs  = std::abs(f.rearSpread);

    if (rearAbs > g_thresholds.tractionRearDiffMax ||
        f.rearTempDiff > g_thresholds.tractionRearTempDiffMax ||
        std::abs(lap.longG.average()) > g_thresholds.tractionLongGMax) {
        f.rearPattern = true;
    }

    if (frontAbs > g_thresholds.tractionFrontDiffMax && rearAbs > g_thresholds.tractionRearDiffMin)
        f.bothAxlesPattern = true;

    // Secondary pattern: no speed-based issue, but temperature + pressure spreads agree on one side.
    if (!f.rearPattern && !f.bothAxlesPattern &&
        lap.surfTempCLR.count > 0 && lap.surfTempCRR.count > 0 &&
        lap.pressureLR.count > 0 && lap.pressureRR.count > 0) {
        double rearPressureDiff = std::abs(lap.pressureLR.average() - lap.pressureRR.average());
        if (f.rearTempDiff > g_thresholds.tractionRearTempSpreadMax &&
            rearPressureDiff > g_thresholds.tractionRearPressureSpreadMax) {
            f.secondarySpread = true;
        }
    }

    return f;
}

// ---------- Bumpstops / suspension travel ----------

BumpStopFinding checkBumpStops(const LapStats& lap) {
    BumpStopFinding f;
    const StatSummary* corners[4] = { &lap.bumpStopLF, &lap.bumpStopRF, &lap.bumpStopLR, &lap.bumpStopRR };
    for (int i = 0; i < 4; ++i) {
        double m = corners[i]->count > 0 ? corners[i]->max : 0.0;
        if (m > g_thresholds.bumpStopMin)
            f.activeCorners.push_back(i);   // LF=0, RF=1, LR=2, RR=3
    }
    return f;
}

SuspTravelFinding checkSuspensionTravel(const LapStats& lap) {
    SuspTravelFinding f;
    const double lf = lap.susTravelLF.count > 0 ? lap.susTravelLF.average() : 0.0;
    const double rf = lap.susTravelRF.count > 0 ? lap.susTravelRF.average() : 0.0;
    const double lr = lap.susTravelLR.count > 0 ? lap.susTravelLR.average() : 0.0;
    const double rr = lap.susTravelRR.count > 0 ? lap.susTravelRR.average() : 0.0;

    f.avgFront = (lf + rf) * 0.5;
    f.avgRear  = (lr + rr) * 0.5;

    if (f.avgFront > g_thresholds.suspensionAvgTravelFrontMax)
        f.frontHigh = true;
    if (f.avgRear  > g_thresholds.suspensionAvgTravelRearMax)
        f.rearHigh  = true;

    return f;
}

// ---------- Aero balance / LSD locking torque ----------

AeroLsdFinding checkAeroAndLsd(const LapStats& lap, ChannelProfile profile) {
    AeroLsdFinding f;
    // aeroBalanceMax is a plain threshold member (not per-profile).
    if (lap.aeroBalance.count > 0 && std::abs(lap.aeroBalance.average()) > g_thresholds.aeroBalanceMax) {
        double ab = lap.aeroBalance.average();
        if (ab > g_thresholds.aeroBalanceMax)
            f.aeroFrontHeavy = true;
        else
            f.aeroRearHeavy  = true;
    }

    if (lap.lsdTorque.count > 0) {
        double lsd = lap.lsdTorque.average();
        f.lsdTorqueAvg = lsd;
        // Two independent checks, same as the original: both flags may be set under a
        // misconfigured threshold pair. The reporter prefers the low-locking message (it
        // replaced the status last in the original code).
        if (lsd > g_thresholds.lsdHigh(profile))
            f.lsdHighLocking = true;
        if (lsd < g_thresholds.lsdLow(profile))
            f.lsdLowLocking  = true;
    }

    return f;
}

// ---------- Aero ride height ----------

AeroHeightFinding checkAeroHeight(const LapStats& lap) {
    AeroHeightFinding f;
    // Original gate: speed above the aero threshold and heights 0 + 3 present (averages use all four).
    if (lap.speed.max > g_thresholds.aeroSpeedMin &&
        lap.aeroHeight0.count > 0 && lap.aeroHeight3.count > 0) {
        double frontH = (lap.aeroHeight0.average() + lap.aeroHeight1.average()) * 0.5;
        double rearH  = (lap.aeroHeight2.average() + lap.aeroHeight3.average()) * 0.5;
        if ((rearH - frontH) > g_thresholds.aeroHeightDiffMax)
            f.rearMuchHigher = true;
    }
    return f;
}

// ---------- Brake torque balance (EVO) ----------

BrakeTorqueFinding checkBrakeTorques(const LapStats& lap) {
    BrakeTorqueFinding f;
    if (!(lap.brakeTorqueLF.count > 0 && lap.brakeTorqueRF.count > 0 &&
          lap.brakeTorqueLR.count > 0 && lap.brakeTorqueRR.count > 0))
        return f;

    const double lf = lap.brakeTorqueLF.average();
    const double rf = lap.brakeTorqueRF.average();
    const double lr = lap.brakeTorqueLR.average();
    const double rr = lap.brakeTorqueRR.average();

    f.lfAvg = lf; f.rfAvg = rf; f.lrAvg = lr; f.rrAvg = rr;
    f.frontSpread = std::abs(lf - rf);
    f.rearSpread  = std::abs(lr - rr);

    const double frontTorque = (lf + rf) * 0.5;
    const double rearTorque  = (lr + rr) * 0.5;
    const double totalTorque = frontTorque + rearTorque;

    if (totalTorque <= g_thresholds.brakeTorqueMin)
        return f;   // not enough braking data to judge the split

    f.dataAvailable = true;
    f.frontSharePct = 100.0 * frontTorque / totalTorque;

    if (f.frontSharePct > g_thresholds.brakeTorqueFrontShareMax)
        f.frontBiased = true;
    else if (f.frontSharePct < g_thresholds.brakeTorqueFrontShareMin)
        f.rearBiased  = true;

    // Per-axle L/R balance uses the SUM of the two corner averages as reference.
    const double frontLrSum = lf + rf;
    if (frontLrSum > g_thresholds.brakeTorqueMin &&
        std::abs(lf - rf) > g_thresholds.brakeTorqueLrDiffRatio * frontLrSum) {
        f.frontAxleUnbalanced = true;
    }

    const double rearLrSum = lr + rr;
    if (rearLrSum > g_thresholds.brakeTorqueMin &&
        std::abs(lr - rr) > g_thresholds.brakeTorqueLrDiffRatio * rearLrSum) {
        f.rearAxleUnbalanced = true;
    }

    return f;
}

// ---------- Brake bias setting (AMS2) ----------

BrakeBiasSettingFinding checkBrakeBiasSetting(const LapStats& lap) {
    BrakeBiasSettingFinding f;
    if (lap.brakeBiasSetting.count == 0)
        return f;

    const double bias = lap.brakeBiasSetting.average();
    f.dataAvailable = true;
    f.avgFrontPct   = bias;

    if (bias > g_thresholds.brakeBiasSettingFrontMax)
        f.heavilyFront = true;
    else if (bias < g_thresholds.brakeBiasSettingRearMin)
        f.heavilyRear  = true;

    return f;
}

// ---------- Wheel slip ratio (EVO) ----------

WheelSlipFinding checkWheelSlips(const LapStats& lap) {
    WheelSlipFinding f;
    if (!(lap.wheelSlipLF.count > 0 && lap.wheelSlipRF.count > 0 &&
          lap.wheelSlipLR.count > 0 && lap.wheelSlipRR.count > 0))
        return f;

    // Some sources report slip as a fraction (<= ~1.5), others as percent: normalize to percent.
    const double slipScale = (lap.wheelSlipRR.max > 1.5) ? 1.0 : 100.0;

    const double rearPct  = ((lap.wheelSlipLR.average() + lap.wheelSlipRR.average()) * 0.5) * slipScale;
    const double frontPct = ((lap.wheelSlipLF.average() + lap.wheelSlipRF.average()) * 0.5) * slipScale;

    f.dataAvailable = true;
    f.rearPct  = rearPct;
    f.frontPct = frontPct;

    if (rearPct  > g_thresholds.wheelSlipMax) f.rearHigh  = true;
    if (frontPct > g_thresholds.wheelSlipMax) f.frontHigh = true;

    return f;
}

// ---------- TCS / ABS activation ----------

ActivationFinding checkActivations(const LapStats& lap) {
    ActivationFinding f;
    if (lap.tcsActive.count > 0) {
        // Some sources report a boolean, others an on-time percentage: normalize to percent.
        const double tcsPct = (lap.tcsActive.max <= 1.5) ? lap.tcsActive.average() * 100.0 : lap.tcsActive.average();
        f.tcsData = true;
        f.tcsPct  = tcsPct;
        if (tcsPct > g_thresholds.tcsActiveMax)
            f.tcsHigh = true;
    }

    if (lap.absActive.count > 0) {
        const double absPct = (lap.absActive.max <= 1.5) ? lap.absActive.average() * 100.0 : lap.absActive.average();
        f.absData = true;
        f.absPct  = absPct;
        if (absPct > g_thresholds.absActiveMax)
            f.absHigh = true;
    }

    return f;
}

// ---------- Tyre air temperature (EVO) ----------

AirTempFinding checkTyreAirTemps(const LapStats& lap) {
    AirTempFinding f;
    if (!(lap.tyreAirTempLF.count > 0 && lap.tyreAirTempRF.count > 0 &&
          lap.tyreAirTempLR.count > 0 && lap.tyreAirTempRR.count > 0))
        return f;

    const double lf = lap.tyreAirTempLF.average();
    const double rf = lap.tyreAirTempRF.average();
    const double lr = lap.tyreAirTempLR.average();
    const double rr = lap.tyreAirTempRR.average();

    f.dataAvailable = true;
    f.lfAvg = lf; f.rfAvg = rf; f.lrAvg = lr; f.rrAvg = rr;

    if (std::abs(lf - rf) > g_thresholds.tyreAirTempLrDiffMax)
        f.frontImbalance = true;
    if (std::abs(lr - rr) > g_thresholds.tyreAirTempLrDiffMax)
        f.rearImbalance  = true;

    return f;
}

// ---------- Suspension velocity (AMS2) ----------

SuspVelocityFinding checkSuspensionVelocities(const LapStats& lap) {
    SuspVelocityFinding f;
    if (!(lap.suspVelLF.totalCount > 0 && lap.suspVelRF.totalCount > 0 &&
          lap.suspVelLR.totalCount > 0 && lap.suspVelRR.totalCount > 0))
        return f;

    const double peaks[4] = { lap.suspVelLF.stdDev(), lap.suspVelRF.stdDev(), lap.suspVelLR.stdDev(), lap.suspVelRR.stdDev() };
    int peakCorner = 0;
    for (int i = 1; i < 4; ++i) {
        if (peaks[i] > peaks[peakCorner])   // strict comparison: on ties the first corner wins (LF-first), like the original chain
            peakCorner = i;
    }

    f.dataAvailable = true;
    f.peakValue     = peaks[peakCorner];
    f.peakCorner    = peakCorner;

    if (f.peakValue > g_thresholds.suspensionVelocityMax)
        f.tooFast = true;

    return f;
}
