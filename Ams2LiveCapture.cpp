//---------------------------------------------------------------------------
// Ams2LiveCapture.cpp — AMS2 live capture from shared memory.
//
// Implements:
//   - Ams2OpenSharedMemory / Ams2ReadFrame / Ams2CloseSharedMemory
//   - Ams2FeedLap: feeds one frame into LapStats accumulators
//   - Ams2IsOnTrack / Ams2IsInPits: pit/track detection
//---------------------------------------------------------------------------

#include <windows.h>
#include <cmath>
#include <string>
#include <cstring>
#include <tlhelp32.h>
#include "Ams2LiveCapture.h"

// ---------------------------------------------------------------------------
// Shared memory handle (module-level, single instance)
// ---------------------------------------------------------------------------
static HANDLE g_hMapFile = NULL;
static void*  g_pSharedMem = NULL;

// ---------------------------------------------------------------------------
// Ams2OpenSharedMemory
// ---------------------------------------------------------------------------
bool Ams2OpenSharedMemory(std::string& errorMessage)
{
    if (g_hMapFile != NULL)
        return true; // already open

    g_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, "$pcars2$");
    if (g_hMapFile == NULL)
    {
        errorMessage = "AMS2 shared memory not found. Is AMS2 running?";
        return false;
    }

    g_pSharedMem = MapViewOfFile(g_hMapFile, FILE_MAP_READ, 0, 0, 0);
    if (g_pSharedMem == NULL)
    {
        CloseHandle(g_hMapFile);
        g_hMapFile = NULL;
        errorMessage = "Failed to map AMS2 shared memory.";
        return false;
    }

    // Verify version
    const Ams2SharedMemory* sm = static_cast<const Ams2SharedMemory*>(g_pSharedMem);
    if (sm->mVersion != SHARED_MEMORY_VERSION)
    {
        errorMessage = "AMS2 shared memory version mismatch (got " +
                       std::to_string(sm->mVersion) + ", expected " +
                       std::to_string(SHARED_MEMORY_VERSION) + ").";
        UnmapViewOfFile(g_pSharedMem);
        g_pSharedMem = NULL;
        CloseHandle(g_hMapFile);
        g_hMapFile = NULL;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Ams2ReadFrame — double-check sequence number to avoid torn reads
// ---------------------------------------------------------------------------
bool Ams2ReadFrame(Ams2SharedMemory& out)
{
    if (g_pSharedMem == NULL)
        return false;

    const Ams2SharedMemory* sm = static_cast<const Ams2SharedMemory*>(g_pSharedMem);

    // Read sequence number, copy data, re-read sequence number.
    // If they match and are even, the copy is consistent.
    unsigned int seq1 = sm->mSequenceNumber;
    if (seq1 & 1)
        return false; // write in progress

    // Copy the struct (it's ~4-5 KB, fast enough at 60 Hz)
    memcpy(&out, sm, sizeof(Ams2SharedMemory));

    unsigned int seq2 = sm->mSequenceNumber;
    if (seq1 != seq2)
        return false; // torn read, caller should retry

    return true;
}

// ---------------------------------------------------------------------------
// Ams2CloseSharedMemory
// ---------------------------------------------------------------------------
void Ams2CloseSharedMemory()
{
    if (g_pSharedMem != NULL)
    {
        UnmapViewOfFile(g_pSharedMem);
        g_pSharedMem = NULL;
    }
    if (g_hMapFile != NULL)
    {
        CloseHandle(g_hMapFile);
        g_hMapFile = NULL;
    }
}

// ---------------------------------------------------------------------------
// Ams2IsProcessRunning
// ---------------------------------------------------------------------------
bool Ams2IsProcessRunning()
{
    bool found = false;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe)) {
            do {
                // Check if the process name contains "AMS2" (handles AMS2.exe, AMS2AVX.exe, etc.)
                if (wcsstr(pe.szExeFile, L"AMS2") != NULL) {
                    found = true;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return found;
}

// ---------------------------------------------------------------------------
// Ams2IsOnTrack / Ams2IsInPits
// ---------------------------------------------------------------------------
bool Ams2IsOnTrack(const Ams2SharedMemory& frame)
{
    // Game must be in playing state
    if (frame.mGameState != GAME_INGAME_PLAYING)
        return false;

    // Car must not be in pits
    if (frame.mPitMode != PIT_MODE_NONE)
        return false;

    return true;
}

bool Ams2IsInPits(const Ams2SharedMemory& frame)
{
    return frame.mPitMode != PIT_MODE_NONE;
}

// ---------------------------------------------------------------------------
// Ams2FeedLap — feed one frame into LapStats accumulators
// ---------------------------------------------------------------------------
//
// Unit conversions (from plan_ams2.md):
//   mSpeed: m/s → ×3.6 → km/h
//   mRpm: direct
//   mThrottle: 0-1 → ×100 → %
//   mBrake: 0-1 → ×100 → %
//   mSteering: -1..1 → ×360 → degrees
//   mAngularVelocity[1]: rad/s → ×180/π → deg/s (yaw)
//   mLocalAcceleration[0]: m/s² → /9.81 → g (long)
//   mLocalAcceleration[2]: m/s² → /9.81 → g (lat)
//   mSuspensionTravel[4]: meters → ×1000 → mm
//   mAirPressure[4]: PSI direct
//   mTyreTempLeft/Center/Right[4]: °C direct
//   mBrakeTempCelsius[4]: °C direct
//   mRideHeight[4]: cm direct
//   mBrakeBias: 0-1 → ×100 → %
//   mFuelLevel: 0-1 → ×100 → %
//   mOilTempCelsius: °C direct
//   mWaterTempCelsius: °C direct
//   mTrackTemperature: °C direct
//   mAmbientTemperature: °C direct
//
// Tyre temp mapping (Left/Right → Inner/Outer):
//   Left tyres (LF=0, LR=2): mTyreTempLeft = Outer, mTyreTempRight = Inner
//   Right tyres (RF=1, RR=3): mTyreTempLeft = Inner, mTyreTempRight = Outer
// ---------------------------------------------------------------------------

static const double DEG_PER_RAD = 180.0 / 3.14159265358979323846;
static const double G = 9.81;

void Ams2FeedLap(LapStats& lap, const Ams2SharedMemory& frame, double timeSeconds)
{
    lap.rideHeightUnit = RideHeightUnit::Centimeters;

    // --- Speed ---
    lap.speed.add(frame.mSpeed * 3.6);

    // --- RPM ---
    lap.rpm.add(frame.mRpm);

    // --- Throttle / Brake ---
    lap.throttle.add(frame.mThrottle * 100.0);
    lap.brakePedal.add(frame.mBrake * 100.0);

    // --- Steering ---
    lap.steerAngle.add(frame.mSteering * 360.0);

    // --- Yaw rate ---
    lap.yawRate.add(frame.mAngularVelocity[1] * DEG_PER_RAD);

    // --- G-forces ---
    lap.longG.add(frame.mLocalAcceleration[0] / G);
    lap.latG.add(frame.mLocalAcceleration[2] / G);

    // --- Suspension travel (mm) ---
    lap.susTravelLF.add(frame.mSuspensionTravel[TYRE_FRONT_LEFT] * 1000.0);
    lap.susTravelRF.add(frame.mSuspensionTravel[TYRE_FRONT_RIGHT] * 1000.0);
    lap.susTravelLR.add(frame.mSuspensionTravel[TYRE_REAR_LEFT] * 1000.0);
    lap.susTravelRR.add(frame.mSuspensionTravel[TYRE_REAR_RIGHT] * 1000.0);

    // --- Suspension velocity (mm/s) — derived from travel change ---
    // We use the raw mSuspensionVelocity which is in m/s → ×1000 → mm/s
    lap.suspVelLF.add(frame.mSuspensionVelocity[TYRE_FRONT_LEFT] * 1000.0);
    lap.suspVelRF.add(frame.mSuspensionVelocity[TYRE_FRONT_RIGHT] * 1000.0);
    lap.suspVelLR.add(frame.mSuspensionVelocity[TYRE_REAR_LEFT] * 1000.0);
    lap.suspVelRR.add(frame.mSuspensionVelocity[TYRE_REAR_RIGHT] * 1000.0);

    // --- Tyre pressures (kPa → PSI) ---
    lap.pressureLF.add(frame.mAirPressure[TYRE_FRONT_LEFT] / 6.894757);
    lap.pressureRF.add(frame.mAirPressure[TYRE_FRONT_RIGHT] / 6.894757);
    lap.pressureLR.add(frame.mAirPressure[TYRE_REAR_LEFT] / 6.894757);
    lap.pressureRR.add(frame.mAirPressure[TYRE_REAR_RIGHT] / 6.894757);

    // --- Tyre temperatures (°C) ---
    // Left tyres (LF=0, LR=2): mTyreTempLeft = Outer, mTyreTempRight = Inner
    // Right tyres (RF=1, RR=3): mTyreTempLeft = Inner, mTyreTempRight = Outer
    lap.tyreInnerTempLF.add(frame.mTyreTempRight[TYRE_FRONT_LEFT]);
    lap.tyreCentreTempLF.add(frame.mTyreTempCenter[TYRE_FRONT_LEFT]);
    lap.tyreOuterTempLF.add(frame.mTyreTempLeft[TYRE_FRONT_LEFT]);

    lap.tyreInnerTempRF.add(frame.mTyreTempLeft[TYRE_FRONT_RIGHT]);
    lap.tyreCentreTempRF.add(frame.mTyreTempCenter[TYRE_FRONT_RIGHT]);
    lap.tyreOuterTempRF.add(frame.mTyreTempRight[TYRE_FRONT_RIGHT]);

    lap.tyreInnerTempLR.add(frame.mTyreTempRight[TYRE_REAR_LEFT]);
    lap.tyreCentreTempLR.add(frame.mTyreTempCenter[TYRE_REAR_LEFT]);
    lap.tyreOuterTempLR.add(frame.mTyreTempLeft[TYRE_REAR_LEFT]);

    lap.tyreInnerTempRR.add(frame.mTyreTempLeft[TYRE_REAR_RIGHT]);
    lap.tyreCentreTempRR.add(frame.mTyreTempCenter[TYRE_REAR_RIGHT]);
    lap.tyreOuterTempRR.add(frame.mTyreTempRight[TYRE_REAR_RIGHT]);

    // --- Brake temperatures (°C) ---
    lap.brakeTempLF.add(frame.mBrakeTempCelsius[TYRE_FRONT_LEFT]);
    lap.brakeTempRF.add(frame.mBrakeTempCelsius[TYRE_FRONT_RIGHT]);
    lap.brakeTempLR.add(frame.mBrakeTempCelsius[TYRE_REAR_LEFT]);
    lap.brakeTempRR.add(frame.mBrakeTempCelsius[TYRE_REAR_RIGHT]);

    // --- Brake bias (%) ---
    if (frame.mBrakeBias >= 0.0f)
        lap.brakeBiasSetting.add(frame.mBrakeBias * 100.0);

    // --- Fuel level (%) ---
    lap.fuelLevel.add(frame.mFuelLevel * 100.0);

    // --- Oil / Water / Track / Ambient temperatures (°C) ---
    lap.oilTemp.add(frame.mOilTempCelsius);
    lap.waterTemp.add(frame.mWaterTempCelsius);
    lap.trackTemp.add(frame.mTrackTemperature);
    lap.ambientTemp.add(frame.mAmbientTemperature);

    // --- TCS / ABS active ---
    lap.tcsActive.add((frame.mCarFlags & CAR_TCS) ? 1.0 : 0.0);
    lap.absActive.add((frame.mCarFlags & CAR_ABS) ? 1.0 : 0.0);

    // --- Wheel slip (from mTyreSlipSpeed, m/s → km/h) ---
    lap.wheelSlipLF.add(frame.mTyreSlipSpeed[TYRE_FRONT_LEFT] * 3.6);
    lap.wheelSlipRF.add(frame.mTyreSlipSpeed[TYRE_FRONT_RIGHT] * 3.6);
    lap.wheelSlipLR.add(frame.mTyreSlipSpeed[TYRE_REAR_LEFT] * 3.6);
    lap.wheelSlipRR.add(frame.mTyreSlipSpeed[TYRE_REAR_RIGHT] * 3.6);

    // --- Wheel speed (from mTyreRPS, rev/s → km/h assuming ~0.65m wheel circumference) ---
    // Actually mTyreRPS is rev/s, so speed = RPS * circumference.
    // We don't know the exact circumference, so use mSpeed as a proxy for wheel speed.
    // The existing pipeline uses wheelSpeed for slip calculations, so we feed mSpeed.
    lap.wheelSpeedLF.add(frame.mSpeed * 3.6);
    lap.wheelSpeedRF.add(frame.mSpeed * 3.6);
    lap.wheelSpeedLR.add(frame.mSpeed * 3.6);
    lap.wheelSpeedRR.add(frame.mSpeed * 3.6);

    // --- Brake torque (not directly available in shared memory, skip) ---

    // --- Bumpstop (not directly available, skip) ---

    // --- Corner entry/exit (not directly available, skip) ---

    // --- Damper stats — use addSample which derives velocity from travel ---
    double travelLF = frame.mSuspensionTravel[TYRE_FRONT_LEFT] * 1000.0;
    double travelRF = frame.mSuspensionTravel[TYRE_FRONT_RIGHT] * 1000.0;
    double travelLR = frame.mSuspensionTravel[TYRE_REAR_LEFT] * 1000.0;
    double travelRR = frame.mSuspensionTravel[TYRE_REAR_RIGHT] * 1000.0;

    lap.damperLF.addSample(travelLF, timeSeconds);
    lap.damperRF.addSample(travelRF, timeSeconds);
    lap.damperLR.addSample(travelLR, timeSeconds);
    lap.damperRR.addSample(travelRR, timeSeconds);

    // --- Handling sample (understeer/oversteer detection) ---
    double steer = frame.mSteering * 360.0;
    double yaw = frame.mAngularVelocity[1] * DEG_PER_RAD;
    double latG = frame.mLocalAcceleration[2] / G;
    double longG = frame.mLocalAcceleration[0] / G;
    double brake = frame.mBrake * 100.0;
    double throttle = frame.mThrottle * 100.0;
    double suspAsymmetry = (travelLF - travelRF) / 2.0;

    lap.addHandlingSample(steer, yaw, latG, longG, brake, throttle, suspAsymmetry);

    // --- Steer/Yaw correlation ---
    lap.addSteerYaw(steer, yaw);

    // --- Aero ride heights (cm) ---
    lap.aeroHeight0.add(frame.mRideHeight[TYRE_FRONT_LEFT]);
    lap.aeroHeight1.add(frame.mRideHeight[TYRE_FRONT_RIGHT]);
    lap.aeroHeight2.add(frame.mRideHeight[TYRE_REAR_LEFT]);
    lap.aeroHeight3.add(frame.mRideHeight[TYRE_REAR_RIGHT]);

    // --- Row count ---
    lap.rowCount++;
}
