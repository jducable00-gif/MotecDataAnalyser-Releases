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
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "AcsEvoLiveCapture.h"
#include "LiveChannelResolver.h"   // JSON-table-driven channel feed (LIVE_CAPTURE_JSON_MIGRATION_PLAN)

// ---------------------------------------------------------------------------
// Shared memory handles (module-level, single instance)
// ---------------------------------------------------------------------------
static HANDLE g_hPhysics  = NULL;
static void*  g_pPhysics  = NULL;
static HANDLE g_hGraphics = NULL;
static void*  g_pGraphics = NULL;
static HANDLE g_hStatic   = NULL;
static void*  g_pStatic   = NULL;

// rad/s -> deg/s (post-table handling sample only; the table row uses its own factor).
static const double DEG_PER_RAD = 180.0 / 3.14159265358979323846;

std::string AcsEvoGetChannelTableWarning();   // defined below

// ---------------------------------------------------------------------------
// One-time diagnostic: dump the RAW shared-memory values for every channel we
// feed, so a "0°" report can be attributed to the game (writes 0.0f) vs our
// pipeline (drops/zeroes it). Written next to the exe as ac_evo_diag.txt on
// the first frame after connect; delete this block once diagnosed.
// ---------------------------------------------------------------------------
static void AcsEvoDumpRawDiagOnce(const SPageFilePhysics& phys, const SPageFileGraphicEvo& gfx) {
    static bool done = false;
    if (done) return;
    done = true;

    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* p = path + strlen(path);
    while (p > path && *p != '\\' && *p != '/') --p;
    size_t remaining = MAX_PATH - static_cast<size_t>(p - path);
    strcpy_s(p, remaining, "\\ac_evo_diag.txt");

    FILE* f = NULL;
    fopen_s(&f, path, "w");
    if (!f) return;

    fprintf(f, "AC EVO raw shared-memory dump (first frame)\n\n");
    for (int i = 0; i < 4; ++i) {
        const char* w[4] = {"FL", "FR", "RL", "RR"};
        fprintf(f, "%s: tyreTempI=%.2f  tyreTempM=%.2f  tyreTempO=%.2f  tyreTemp=%.2f\n",
                w[i], (double)phys.tyreTempI[i], (double)phys.tyreTempM[i],
                (double)phys.tyreTempO[i], (double)phys.tyreTemp[i]);
    }
    fprintf(f, "\n");
    for (int i = 0; i < 4; ++i) {
        const char* w[4] = {"FL", "FR", "RL", "RR"};
        fprintf(f, "%s: pressure=%.2f  brakeTemp=%.1f  wheelAngSpeed=%.3f  suspTravel=%.5f\n",
                w[i], (double)phys.wheelsPressure[i], (double)phys.brakeTemp[i],
                (double)phys.wheelAngularSpeed[i], (double)phys.suspensionTravel[i]);
    }
    fprintf(f, "\n");
    for (int i = 0; i < 4; ++i) {
        const char* w[4] = {"FL", "FR", "RL", "RR"};
        const SMEvoTyreState& t = (&gfx.tyre_lf)[i];   // lf, rf, lr, rr are contiguous in the struct
        fprintf(f, "%s gfx: temp_c=%.2f  left=%.2f  center=%.2f  right=%.2f\n",
                w[i], (double)t.tyre_temperature_c, (double)t.tyre_temperature_left,
                (double)t.tyre_temperature_center, (double)t.tyre_temperature_right);
    }
    fprintf(f, "\n");
    fprintf(f, "waterTemp=%.1f  oil_temp(gfx)=%.2f  rideHeight[0]=%.5f  rideHeight[1]=%.5f\n",
            (double)phys.waterTemp, (double)gfx.oil_temperature_c,
            (double)phys.rideHeight[0], (double)phys.rideHeight[1]);

    fclose(f);
}
// Resolved channel table for EVOM1 LIVE rows (mapping.json), built ONCE on first use.
// Unit conversions live in mapping.json, not here: this function only extracts RAW values
// from the shared-memory frames and lets FeedLapFromTable() apply factor/offset per row.
std::string g_acsEvoChannelTableWarning;
static LiveChannel::ResolvedChannelTable& AcsEvoGetChannelTable()
{
    static LiveChannel::ResolvedChannelTable table = [] {
        LiveChannel::ResolvedChannelTable t;
        t.build(ChannelProfile::EvoM1);   // EVOM1 + Generic rows
        return t;
    }();
    // One-time validation: surface (never fatal) any mapping row that failed to resolve.
    static bool validated = false;
    if (!validated) {
        std::string report;
        // Required raw channel names - a JSON/dict-key typo would otherwise cause
        // silent per-frame skips, so every name the dict below is built from must exist in mapping.json.
        const std::vector<std::string> required = [] {
            std::vector<std::string> v;
            v.push_back("physics.speedKmh");
            v.push_back("physics.rpms");
            v.push_back("physics.gas");
            v.push_back("physics.brake");
            v.push_back("physics.steerAngle");
            v.push_back("physics.localAngularVel[1]");
            v.push_back("physics.accG[0]");
            v.push_back("physics.accG[1]");
            for (int i = 0; i < 4; ++i) {
                char key[64];
                snprintf(key, sizeof(key), "physics.wheelAngularSpeed[%d]", i);   v.push_back(key);
                snprintf(key, sizeof(key), "physics.brakeTemp[%d]", i);           v.push_back(key);
                snprintf(key, sizeof(key), "physics.wheelsPressure[%d]", i);      v.push_back(key);
                snprintf(key, sizeof(key), "physics.tyreTempI[%d]", i);           v.push_back(key);
                snprintf(key, sizeof(key), "physics.tyreTempM[%d]", i);           v.push_back(key);
                snprintf(key, sizeof(key), "physics.tyreTempO[%d]", i);           v.push_back(key);
                snprintf(key, sizeof(key), "physics.brakeTorque[%d]", i);         v.push_back(key);
                snprintf(key, sizeof(key), "physics.slipRatio[%d]", i);           v.push_back(key);
                snprintf(key, sizeof(key), "physics.suspensionTravel[%d]", i);    v.push_back(key);
            }
            v.push_back("physics.tcinAction");
            v.push_back("physics.absInAction");
            v.push_back("physics.brakeBias");
            v.push_back("graphics.oil_temperature_c");
            v.push_back("physics.waterTemp");
            v.push_back("physics.roadTemp");
            v.push_back("physics.airTemp");
            v.push_back("physics.fuel");
            v.push_back("physics.rideHeight[0]");
            v.push_back("physics.rideHeight[1]");
            return v;
        }();
        table.validate(required, report);
        g_acsEvoChannelTableWarning = report.empty() ? "" : "AC EVO live channel table: " + report;
        validated = true;
    }
    return table;
}

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
// AcsEvoFeedLap — feed one frame into LapStats accumulators (table-driven)
// ---------------------------------------------------------------------------
// Unit conversions live in mapping.json EVOM1 LIVE rows; this function only
// extracts RAW values from the physics/graphics frames. Pre-computed percent
// values (fuel, TCS/ABS) are fed as 0..100 so f=1 ratio rows land in percent,
// matching old C++ behaviour exactly.
// ---------------------------------------------------------------------------

void AcsEvoFeedLap(LapStats& lap, const SPageFilePhysics& phys,
                   const SPageFileGraphicEvo& gfx, double timeSeconds)
{
    LiveChannel::ResolvedChannelTable& table = AcsEvoGetChannelTable();

    AcsEvoDumpRawDiagOnce(phys, gfx);   // one-time raw-value dump -> ac_evo_diag.txt (delete after diagnosis)


    // --- Raw channel dictionary (RAW values only - no unit conversion here). ---
    // Keys MUST match the "channel" field of mapping.json EVOM1 LIVE rows EXACTLY.
    std::unordered_map<std::string, double> raw;
    raw["physics.speedKmh"]            = phys.speedKmh;                    // km/h (Vehicle_Speed)
    raw["physics.rpms"]                = phys.rpms;                        // RPM
    raw["physics.gas"]                 = phys.gas;                         // 0..1 -> % x100 via table
    raw["physics.brake"]               = phys.brake;                       // 0..1 -> % x100 via table (Brake_Pos)
    raw["physics.steerAngle"]          = phys.steerAngle;                  // -1..1 -> deg x360 via table
    raw["physics.localAngularVel[1]"]  = phys.localAngularVel[1];          // rad/s -> deg/s via table (Yaw_Rate)
    raw["physics.accG[0]"]             = phys.accG[0];                     // g direct (lat)
    raw["physics.accG[1]"]             = phys.accG[1];                     // g direct (long)
    for (int i = 0; i < 4; ++i) {   // wheel order: FL=0, FR=1, RL=2, RR=3
        char key[64];
        snprintf(key, sizeof(key), "physics.wheelAngularSpeed[%d]", i); raw[key] = phys.wheelAngularSpeed[i];  // rad/s -> km/h via table factor (r=0.33m)
        snprintf(key, sizeof(key), "physics.brakeTemp[%d]", i);         raw[key] = phys.brakeTemp[i];          // C
        snprintf(key, sizeof(key), "physics.wheelsPressure[%d]", i);    raw[key] = phys.wheelsPressure[i];     // PSI direct (f=1)
        snprintf(key, sizeof(key), "physics.tyreTempI[%d]", i);         raw[key] = phys.tyreTempI[i];          // C inner
        snprintf(key, sizeof(key), "physics.tyreTempM[%d]", i);         raw[key] = phys.tyreTempM[i];          // C centre (Tyre_Temp_*_Middle + Surf_Temp_*)
        snprintf(key, sizeof(key), "physics.tyreTempO[%d]", i);         raw[key] = phys.tyreTempO[i];          // C outer
        snprintf(key, sizeof(key), "physics.brakeTorque[%d]", i);       raw[key] = phys.brakeTorque[i];        // Nm
        snprintf(key, sizeof(key), "physics.slipRatio[%d]", i);         raw[key] = phys.slipRatio[i];          // ratio -> % x100 via table (Wheel_Slip_Ratio_*)
        snprintf(key, sizeof(key), "physics.suspensionTravel[%d]", i);  raw[key] = phys.suspensionTravel[i];   // m -> mm x1000 via table
    }
    raw["physics.tcinAction"]          = phys.tcinAction ? 100.0 : 0.0;     // pre-computed % (TCS_In_Action, f=1)
    raw["physics.absInAction"]         = phys.absInAction ? 100.0 : 0.0;    // pre-computed % (ABS_In_Action, f=1)
    raw["physics.brakeBias"]           = phys.brakeBias;                    // 0..1 -> % x100 via table
    raw["graphics.oil_temperature_c"]  = gfx.oil_temperature_c;             // C (oil temp lives on the graphics page)
    raw["physics.waterTemp"]           = phys.waterTemp;                    // C
    raw["physics.roadTemp"]            = phys.roadTemp;                     // C (track surface)
    raw["physics.airTemp"]             = phys.airTemp;                      // C ambient
    raw["physics.fuel"]                = phys.fuel * 100.0;                 // % - pre-computed so the ratio row lands in percent, matching old C++
    raw["physics.rideHeight[0]"]       = phys.rideHeight[0];               // m -> cm x100 via table (aeroHeight0)
    raw["physics.rideHeight[1]"]        = phys.rideHeight[1];               // m -> cm x100 via table (aeroHeight1)

    // Per-band tyre temps: AC EVO writes these ONLY on the graphics page, not physics.
    // The physics arrays (tyreTempI/M/O) are always 0.0f in practice.
    raw["graphics.tyre_lf.temp_left"]   = gfx.tyre_lf.tyre_temperature_left;   // C
    raw["graphics.tyre_lf.temp_center"]  = gfx.tyre_lf.tyre_temperature_center;
    raw["graphics.tyre_lf.temp_right"]   = gfx.tyre_lf.tyre_temperature_right;
    raw["graphics.tyre_rf.temp_left"]   = gfx.tyre_rf.tyre_temperature_left;   // C
    raw["graphics.tyre_rf.temp_center"]  = gfx.tyre_rf.tyre_temperature_center;
    raw["graphics.tyre_rf.temp_right"]   = gfx.tyre_rf.tyre_temperature_right;
    raw["graphics.tyre_lr.temp_left"]   = gfx.tyre_lr.tyre_temperature_left;   // C
    raw["graphics.tyre_lr.temp_center"]  = gfx.tyre_lr.tyre_temperature_center;
    raw["graphics.tyre_lr.temp_right"]   = gfx.tyre_lr.tyre_temperature_right;
    raw["graphics.tyre_rr.temp_left"]   = gfx.tyre_rr.tyre_temperature_left;   // C
    raw["graphics.tyre_rr.temp_center"]  = gfx.tyre_rr.tyre_temperature_center;
    raw["graphics.tyre_rr.temp_right"]   = gfx.tyre_rr.tyre_temperature_right;

    // --- Generic table-driven feed: v = raw * factor + offset -> LapStats sink. ---
    LiveChannel::FeedLapFromTable(lap, raw, table, timeSeconds);
    lap.rideHeightUnit = RideHeightUnit::Centimeters;   // aeroHeight0/1 arrive in cm (table x10 from m) - identical to old C++ path

    // --- Damper stats: addSample derives shaft velocity from travel deltas over wall-clock time.
    // Stays post-table by design (multi-value derived sample, not a single-channel sink). ---
    double travelLF = phys.suspensionTravel[0] * 1000.0;   // m -> mm
    double travelRF = phys.suspensionTravel[1] * 1000.0;
    double travelLR = phys.suspensionTravel[2] * 1000.0;
    double travelRR = phys.suspensionTravel[3] * 1000.0;
    lap.damperLF.addSample(travelLF, timeSeconds);
    lap.damperRF.addSample(travelRF, timeSeconds);
    lap.damperLR.addSample(travelLR, timeSeconds);
    lap.damperRR.addSample(travelRR, timeSeconds);

    // --- Handling sample (understeer/oversteer detection) - multi-value derived sample. ---
    double steer = phys.steerAngle * 360.0;
    double yaw   = phys.localAngularVel[1] * DEG_PER_RAD;
    double latG  = phys.accG[0];
    double longG = phys.accG[1];
    double brake     = phys.brake * 100.0;
    double throttle  = phys.gas * 100.0;
    double suspAsymmetry = (travelLF - travelRF) / 2.0;
    lap.addHandlingSample(steer, yaw, latG, longG, brake, throttle, suspAsymmetry);

    // --- Steer/Yaw correlation ---
    lap.addSteerYaw(steer, yaw);

    // --- Row count ---
    lap.rowCount++;
}

std::string AcsEvoGetChannelTableWarning() { return g_acsEvoChannelTableWarning; }
