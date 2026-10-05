//---------------------------------------------------------------------------
// AcsEvoLiveCapture.cpp — AC EVO live capture from shared memory.
//
// Implements:
//   - AcsEvoOpenSharedMemory / AcsEvoReadPhysics / AcsEvoReadGraphics /
//     AcsEvoReadStatic / AcsEvoCloseSharedMemory
//   - AcsEvoFeedLap: feeds one frame (physics + graphics) into LapStats
//   - AcsEvoIsOnTrack / AcsEvoIsInPits: pit/track detection
//   - AcsEvoDetectLapChange: npos wrap detection
//---------------------------------------------------------------------------

#include <windows.h>
#include <cmath>
#include <string>
#include <cstring>
#include "AcsEvoLiveCapture.h"

// ---------------------------------------------------------------------------
// Shared memory handles (module-level, single instance)
// ---------------------------------------------------------------------------
static HANDLE g_hPhysics  = NULL;
static void*  g_pPhysics  = NULL;
static HANDLE g_hGraphics = NULL;
static void*  g_pGraphics = NULL;
static HANDLE g_hStatic   = NULL;
static void*  g_pStatic   = NULL;

// Default wheel radius (m) used to convert wheel angular speed (rad/s) to km/h.
static const double kWheelRadiusM = 0.33;
static const double DEG_PER_RAD = 180.0 / 3.14159265358979323846;

// ---------------------------------------------------------------------------
// AcsEvoOpenSharedMemory
// ---------------------------------------------------------------------------
bool AcsEvoOpenSharedMemory(std::string& errorMessage)
{
    if (g_pPhysics != NULL && g_pGraphics != NULL)
        return true; // already open

    // Physics
    g_hPhysics = OpenFileMappingA(FILE_MAP_READ, FALSE, ACEVO_SM_PHYSICS);
    if (g_hPhysics == NULL)
    {
        errorMessage = "AC EVO physics shared memory not found. Is AC EVO running?";
        return false;
    }
    g_pPhysics = MapViewOfFile(g_hPhysics, FILE_MAP_READ, 0, 0, 0);
    if (g_pPhysics == NULL)
    {
        CloseHandle(g_hPhysics);
        g_hPhysics = NULL;
        errorMessage = "Failed to map AC EVO physics shared memory.";
        return false;
    }

    // Graphics
    g_hGraphics = OpenFileMappingA(FILE_MAP_READ, FALSE, ACEVO_SM_GRAPHICS);
    if (g_hGraphics == NULL)
    {
        errorMessage = "AC EVO graphics shared memory not found. Is AC EVO running?";
        UnmapViewOfFile(g_pPhysics);
        g_pPhysics = NULL;
        CloseHandle(g_hPhysics);
        g_hPhysics = NULL;
        return false;
    }
    g_pGraphics = MapViewOfFile(g_hGraphics, FILE_MAP_READ, 0, 0, 0);
    if (g_pGraphics == NULL)
    {
        CloseHandle(g_hGraphics);
        g_hGraphics = NULL;
        UnmapViewOfFile(g_pPhysics);
        g_pPhysics = NULL;
        CloseHandle(g_hPhysics);
        g_hPhysics = NULL;
        errorMessage = "Failed to map AC EVO graphics shared memory.";
        return false;
    }

    // Static (optional — not required for capture, but useful for metadata)
    g_hStatic = OpenFileMappingA(FILE_MAP_READ, FALSE, ACEVO_SM_STATIC);
    if (g_hStatic != NULL)
    {
        g_pStatic = MapViewOfFile(g_hStatic, FILE_MAP_READ, 0, 0, 0);
        if (g_pStatic == NULL)
        {
            CloseHandle(g_hStatic);
            g_hStatic = NULL;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// AcsEvoReadPhysics — double-check packetId to avoid torn reads
// ---------------------------------------------------------------------------
bool AcsEvoReadPhysics(SPageFilePhysics& out)
{
    if (g_pPhysics == NULL)
        return false;

    const SPageFilePhysics* sm = static_cast<const SPageFilePhysics*>(g_pPhysics);

    int id1 = sm->packetId;
    memcpy(&out, sm, sizeof(SPageFilePhysics));
    int id2 = sm->packetId;

    return (id1 == id2);
}

// ---------------------------------------------------------------------------
// AcsEvoReadGraphics — double-check packetId to avoid torn reads
// ---------------------------------------------------------------------------
bool AcsEvoReadGraphics(SPageFileGraphicEvo& out)
{
    if (g_pGraphics == NULL)
        return false;

    const SPageFileGraphicEvo* sm = static_cast<const SPageFileGraphicEvo*>(g_pGraphics);

    int id1 = sm->packetId;
    memcpy(&out, sm, sizeof(SPageFileGraphicEvo));
    int id2 = sm->packetId;

    return (id1 == id2);
}

// ---------------------------------------------------------------------------
// AcsEvoReadStatic
// ---------------------------------------------------------------------------
bool AcsEvoReadStatic(SPageFileStaticEvo& out)
{
    if (g_pStatic == NULL)
        return false;

    const SPageFileStaticEvo* sm = static_cast<const SPageFileStaticEvo*>(g_pStatic);
    memcpy(&out, sm, sizeof(SPageFileStaticEvo));
    return true;
}

// ---------------------------------------------------------------------------
// AcsEvoCloseSharedMemory
// ---------------------------------------------------------------------------
void AcsEvoCloseSharedMemory()
{
    if (g_pPhysics != NULL)
    {
        UnmapViewOfFile(g_pPhysics);
        g_pPhysics = NULL;
    }
    if (g_hPhysics != NULL)
    {
        CloseHandle(g_hPhysics);
        g_hPhysics = NULL;
    }
    if (g_pGraphics != NULL)
    {
        UnmapViewOfFile(g_pGraphics);
        g_pGraphics = NULL;
    }
    if (g_hGraphics != NULL)
    {
        CloseHandle(g_hGraphics);
        g_hGraphics = NULL;
    }
    if (g_pStatic != NULL)
    {
        UnmapViewOfFile(g_pStatic);
        g_pStatic = NULL;
    }
    if (g_hStatic != NULL)
    {
        CloseHandle(g_hStatic);
        g_hStatic = NULL;
    }
}

// ---------------------------------------------------------------------------
// AcsEvoIsOnTrack / AcsEvoIsInPits
// ---------------------------------------------------------------------------
bool AcsEvoIsOnTrack(const SPageFileGraphicEvo& gfx)
{
    // Game must be live and the car must be on the track.
    return (gfx.status == AC_LIVE) && (gfx.car_location == ACEVO_TRACK);
}

bool AcsEvoIsInPits(const SPageFileGraphicEvo& gfx)
{
    return (gfx.car_location == ACEVO_PITLANE) ||
           (gfx.car_location == ACEVO_PITENTRY) ||
           (gfx.car_location == ACEVO_PITEXIT) ||
           gfx.is_in_pit_box;
}

// ---------------------------------------------------------------------------
// AcsEvoDetectLapChange — npos wrap (0.0 -> 1.0 per lap)
// ---------------------------------------------------------------------------
bool AcsEvoDetectLapChange(const SPageFileGraphicEvo& prev,
                           const SPageFileGraphicEvo& curr)
{
    // A lap completes when the position wraps from near the end (> 0.9)
    // back to near the start (< 0.1).
    return (prev.npos > 0.9f) && (curr.npos < 0.1f);
}

// ---------------------------------------------------------------------------
// AcsEvoFeedLap — feed one frame into LapStats accumulators
// ---------------------------------------------------------------------------
//
// Unit conversions (from plan_ac_evo.md):
//   speedKmh: km/h direct
//   gas / brake: 0-1 -> x100 -> %
//   rpms: direct
//   steerAngle: -1..1 -> x360 -> degrees
//   localAngularVel[1]: rad/s -> x180/pi -> deg/s (yaw)
//   accG[0]: g direct (lat), accG[1]: g direct (long)
//   wheelAngularSpeed[4]: rad/s -> x radius x 3600/(2*pi*1000) -> km/h
//   brakeTemp[4]: C direct
//   wheelsPressure[4]: PSI direct
//   tyreTempI/M/O[4]: C direct (inner/centre/outer)
//   brakeTorque[4]: Nm direct
//   slipRatio[4]: ratio -> x100 -> %
//   tcinAction / absInAction: 0/1 -> x100 -> %
//   brakeBias: 0-1 -> x100 -> %
//   suspensionTravel[4]: meters -> x1000 -> mm
//   oil_temperature_c (graphics): C direct
//   waterTemp (physics): C direct
//   roadTemp (physics): C direct
//   airTemp (physics): C direct
//   rideHeight[0..1]: meters -> x100 -> cm (front/rear)
// ---------------------------------------------------------------------------

void AcsEvoFeedLap(LapStats& lap, const SPageFilePhysics& phys,
                   const SPageFileGraphicEvo& gfx, double timeSeconds)
{
    lap.rideHeightUnit = RideHeightUnit::Centimeters;

    // --- Speed (km/h) ---
    lap.speed.add(phys.speedKmh);

    // --- RPM ---
    lap.rpm.add(phys.rpms);

    // --- Throttle / Brake (%) ---
    lap.throttle.add(phys.gas * 100.0);
    lap.brakePedal.add(phys.brake * 100.0);

    // --- Steering (degrees) ---
    lap.steerAngle.add(phys.steerAngle * 360.0);

    // --- Yaw rate (deg/s) ---
    lap.yawRate.add(phys.localAngularVel[1] * DEG_PER_RAD);

    // --- G-forces (g) ---
    lap.latG.add(phys.accG[0]);
    lap.longG.add(phys.accG[1]);

    // --- Wheel speed (km/h) from angular speed (rad/s) ---
    const double angToKmh = kWheelRadiusM * 3600.0 / (2.0 * 3.14159265358979323846 * 1000.0);
    lap.wheelSpeedLF.add(phys.wheelAngularSpeed[0] * angToKmh);
    lap.wheelSpeedRF.add(phys.wheelAngularSpeed[1] * angToKmh);
    lap.wheelSpeedLR.add(phys.wheelAngularSpeed[2] * angToKmh);
    lap.wheelSpeedRR.add(phys.wheelAngularSpeed[3] * angToKmh);

    // --- Brake temperatures (C) ---
    lap.brakeTempLF.add(phys.brakeTemp[0]);
    lap.brakeTempRF.add(phys.brakeTemp[1]);
    lap.brakeTempLR.add(phys.brakeTemp[2]);
    lap.brakeTempRR.add(phys.brakeTemp[3]);

    // --- Tyre pressures (PSI) ---
    lap.pressureLF.add(phys.wheelsPressure[0]);
    lap.pressureRF.add(phys.wheelsPressure[1]);
    lap.pressureLR.add(phys.wheelsPressure[2]);
    lap.pressureRR.add(phys.wheelsPressure[3]);

    // --- Tyre temperatures (C) — inner/centre/outer directly available ---
    lap.tyreInnerTempLF.add(phys.tyreTempI[0]);
    lap.tyreCentreTempLF.add(phys.tyreTempM[0]);
    lap.tyreOuterTempLF.add(phys.tyreTempO[0]);

    lap.tyreInnerTempRF.add(phys.tyreTempI[1]);
    lap.tyreCentreTempRF.add(phys.tyreTempM[1]);
    lap.tyreOuterTempRF.add(phys.tyreTempO[1]);

    lap.tyreInnerTempLR.add(phys.tyreTempI[2]);
    lap.tyreCentreTempLR.add(phys.tyreTempM[2]);
    lap.tyreOuterTempLR.add(phys.tyreTempO[2]);

    lap.tyreInnerTempRR.add(phys.tyreTempI[3]);
    lap.tyreCentreTempRR.add(phys.tyreTempM[3]);
    lap.tyreOuterTempRR.add(phys.tyreTempO[3]);

    // Surface (centre) temps also feed the surfTempC* channels used by the
    // front/rear temperature-difference checks.
    lap.surfTempCFL.add(phys.tyreTempM[0]);
    lap.surfTempCRF.add(phys.tyreTempM[1]);
    lap.surfTempCLR.add(phys.tyreTempM[2]);
    lap.surfTempCRR.add(phys.tyreTempM[3]);

    // --- Brake torque (Nm) ---
    lap.brakeTorqueLF.add(phys.brakeTorque[0]);
    lap.brakeTorqueRF.add(phys.brakeTorque[1]);
    lap.brakeTorqueLR.add(phys.brakeTorque[2]);
    lap.brakeTorqueRR.add(phys.brakeTorque[3]);

    // --- Wheel slip ratio (%) ---
    lap.wheelSlipLF.add(phys.slipRatio[0] * 100.0);
    lap.wheelSlipRF.add(phys.slipRatio[1] * 100.0);
    lap.wheelSlipLR.add(phys.slipRatio[2] * 100.0);
    lap.wheelSlipRR.add(phys.slipRatio[3] * 100.0);

    // --- TCS / ABS active (%) ---
    lap.tcsActive.add(phys.tcinAction ? 100.0 : 0.0);
    lap.absActive.add(phys.absInAction ? 100.0 : 0.0);

    // --- Brake bias setting (%) ---
    lap.brakeBiasSetting.add(phys.brakeBias * 100.0);

    // --- Suspension travel (mm) ---
    double travelLF = phys.suspensionTravel[0] * 1000.0;
    double travelRF = phys.suspensionTravel[1] * 1000.0;
    double travelLR = phys.suspensionTravel[2] * 1000.0;
    double travelRR = phys.suspensionTravel[3] * 1000.0;

    lap.susTravelLF.add(travelLF);
    lap.susTravelRF.add(travelRF);
    lap.susTravelLR.add(travelLR);
    lap.susTravelRR.add(travelRR);

    // --- Damper stats — derive velocity from travel (dTravel/dt) ---
    lap.damperLF.addSample(travelLF, timeSeconds);
    lap.damperRF.addSample(travelRF, timeSeconds);
    lap.damperLR.addSample(travelLR, timeSeconds);
    lap.damperRR.addSample(travelRR, timeSeconds);

    // --- Oil / Water / Track / Ambient temperatures (C) ---
    lap.oilTemp.add(gfx.oil_temperature_c);
    lap.waterTemp.add(phys.waterTemp);
    lap.trackTemp.add(phys.roadTemp);
    lap.ambientTemp.add(phys.airTemp);

    // --- Fuel level (%) ---
    lap.fuelLevel.add(phys.fuel * 100.0);

    // --- Ride height (cm) — front/rear only ---
    lap.aeroHeight0.add(phys.rideHeight[0] * 100.0);
    lap.aeroHeight1.add(phys.rideHeight[1] * 100.0);

    // --- Handling sample (understeer/oversteer detection) ---
    double steer = phys.steerAngle * 360.0;
    double yaw = phys.localAngularVel[1] * DEG_PER_RAD;
    double latG = phys.accG[0];
    double longG = phys.accG[1];
    double brake = phys.brake * 100.0;
    double throttle = phys.gas * 100.0;
    double suspAsymmetry = (travelLF - travelRF) / 2.0;

    lap.addHandlingSample(steer, yaw, latG, longG, brake, throttle, suspAsymmetry);

    // --- Steer/Yaw correlation ---
    lap.addSteerYaw(steer, yaw);

    // --- Row count ---
    lap.rowCount++;
}
