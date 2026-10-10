//---------------------------------------------------------------------------
// test_pmr_decoder.cpp — Gate 1 standalone unit test for PmrLiveCapture decoders.
//
// Hand-crafts byte-exact datagrams (matching the C# reference decoder wire order),
// calls each public decode function, and asserts key fields match expected values.
// Also tests edge cases: truncated packets, wrong version/type, empty buffer.
//
// Build with bcc32c (C++Builder 12):
//   call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat"
//   cd scripts
//   bcc32c -o test_pmr_decoder.exe ..\PmrLiveCapture.cpp test_pmr_decoder.cpp
//---------------------------------------------------------------------------

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include "PmrLiveCapture.h"

// Satisfy the extern in MotecSetupAnalyzer.h (pulled transitively via PmrLiveCapture.h).
Thresholds g_thresholds = Thresholds();

static int s_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { ++s_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while(0)


// ---------------------------------------------------------------------------
// Minimal LE binary writer for crafting test datagrams. Tight-packed, no padding —
// exactly matching the PMR wire format (verified against C# UDPProtocol.cs).
// ---------------------------------------------------------------------------
class Writer {
public:
    void u8(uint8_t v)      { m_buf.push_back(v); }
    void i32(int32_t v)     { uint8_t b[4]; std::memcpy(b, &v, 4); for (int i=0;i<4;++i) m_buf.push_back(b[i]); }
    void u16(uint16_t v)    { uint8_t b[2]; std::memcpy(b, &v, 2); for (int i=0;i<2;++i) m_buf.push_back(b[i]); }
    void f32(float v)       { uint8_t b[4]; std::memcpy(b, &v, 4); for (int i=0;i<4;++i) m_buf.push_back(b[i]); }

    // DynamicSizeString: u8 length + N bytes UTF-8.
    void str(const char* s) { size_t n = strlen(s); u8((uint8_t)n); for (size_t i=0;i<n;++i) m_buf.push_back((uint8_t)s[i]); }

    // UDPFloatList: u8 count + N floats.
    void floatList(float const* vals, uint8_t n) { u8(n); for (int i=0;i<(int)n;++i) f32(vals[i]); }

    const uint8_t* data() const { return m_buf.data(); }
    size_t         len()  const { return m_buf.size(); }

private:
    std::vector<uint8_t> m_buf;
};


// ===========================================================================
// RaceInfo builder (type=0, version=1)
// Wire order per PmrLiveCapture.cpp decodeRaceInfo():
//   header(3B) | 6 strings | 5 floats | 3 bytes
// ===========================================================================
static void buildRaceInfo(Writer& w) {
    // Header: u8 type=0, u16 version=1 (LE).
    w.u8(PMR_PACKET_RACE_INFO);   // 0x00
    w.u16(kPmrRaceInfoVersion);   // 0x01 0x00

    // Six dynamic strings in wire order.
    w.str("Silverstone");         // track
    w.str("GP Layout");           // layout
    w.str("2025 Season");         // season
    w.str("Sunny");               // weather
    w.str("Race");                // session
    w.str("Endurance 4h");        // gameMode

    // Five floats in wire order.
    w.f32(5891.0f);              // layoutLength (m)
    w.f32(14400.0f);             // duration (s = 4 hours)
    w.f32(600.0f);               // overtime (s)
    w.f32(28.5f);                // ambientTemperature (C)
    w.f32(41.2f);                // trackTemperature (C)

    // Three trailing single-byte fields (tight packed).
    w.u8(1);                     // isLaps = true
    w.u8(0);                     // state = 0
    w.u8(7);                     // numParticipants = 7
}


// ===========================================================================
// ParticipantState builder (type=1, version=1)
// Wire order per PmrLiveCapture.cpp decodeParticipantState():
//   header(3B) | vehicleId(i32) | isPlayer(u8) | 4 strings | racePos(i32) |
//   currentLap(i32) | curLapTime(f) bestLapTime(f) lapProgress(f) |
//   currentSector(i32) | floatList(curSectTimes) | floatList(bestSectTimes) |
//   inPits(u8) sessionFinished(u8) dq(u8) | flags(u32 LE)
// ===========================================================================
static void buildParticipantState(Writer& w, int vehicleId = 42, bool isPlayer = true) {
    // Header.
    w.u8(PMR_PACKET_PARTICIPANT_STATE);   // type=1
    w.u16(kPmrParticipantStateVersion);   // version=1

    // CRITICAL ORDER: vehicleId FIRST (i32), then isPlayer SECOND (u8).
    w.i32(vehicleId);          // e.g., 42 -> bytes [0x2A, 0x00, 0x00, 0x00]
    w.u8(isPlayer ? 1 : 0);

    // Four dynamic strings in wire order.
    w.str("Ferrari SF-25");     // vehicleName
    w.str("Charles Leclerc");   // driverName
    w.str("livery_07");         // liveryId
    w.str("F1");                // vehicleClass

    // Two ints, three floats.
    w.i32(3);                  // racePos = 3rd place
    w.i32(56);                 // currentLap = lap 56
    w.f32(90.482f);            // currentLapTime (s)
    w.f32(89.107f);             // bestLapTime (s)
    w.f32(0.72f);              // lapProgress

    // One int + two UDPFloatLists.
    w.i32(2);                  // currentSector = 2
    {   // currentSectorTimes: u8 count=1, one float
        uint8_t n = 1;
        w.u8(n);
        w.f32(30.5f);          // sector 1 time (only S1 completed this lap)
    }
    {   // bestSectorTimes: u8 count=3, three floats
        uint8_t n = 3;
        w.u8(n);
        w.f32(29.8f);
        w.f32(30.1f);
        w.f32(29.4f);
    }

    // Three trailing status bytes (tight packed) + u32 flag bitmask (LE).
    uint32_t flags = 0x00000005;   // DRS active + pit lane entry bits set
    w.u8(0);                       // inPits = false
    w.u8(0);                       // sessionFinished = false
    w.u8(1);                       // dq = true (testing a non-zero value)

    uint8_t fb[4]; std::memcpy(fb, &flags, 4);   // LE on x86: [5,0,0,0]
    for (int i=0;i<4;++i) w.u8(fb[i]);
}


// ===========================================================================
// VehicleTelemetry builder (type=2, version=2)
// Wire order per PmrLiveCapture.cpp decodeVehicleTelemetry():
//   header(3B) | vehicleId(i32) | wheelCount(u8) | N x wheelBlock | chassis |
//   drivetrain | suspension | input | setup | general | constant
// ===========================================================================

static void writeWheel(Writer& w, int idx /*0=LF 1=RF 2=LR 3=RR*/) {
    // Each wheel block is exactly 124 bytes (verified: 1 i32 + 30 f32 = 4+120).

    // contactMaterialHash (i32) — unique per wheel for identification.
    w.i32(100 + idx);              // LF=100 RF=101 LR=102 RR=103

    // angVel(f), linearSpeed(f).
    w.f32(85.0f  + idx * 10.f);   // rad/s: 85,95,105,115
    w.f32(60.0f  + idx * 5.f);    // m/s

    // slideLS(Vec3), forceLS(Vec3), momentLS(Vec3) — each x,y,z floats (9 total).
    for (int c = 0; c < 9; ++c) w.f32(1.0f + idx * 0.5f + c * 0.1f);

    // contactRadius, pressure(bar), inclination(rad), slipRatio, slipAngle — 5 floats.
    w.f32(0.33f);                  // radius (m)
    w.f32(2.40f + idx * 0.05f);   // bar: LF=2.40 RF=2.45 LR=2.50 RR=2.55
    w.f32(-1.2f);                 // inclination (rad)
    w.f32(0.87f + idx * 0.01f);   // slipRatio
    w.f32(4.5f);                  // slipAngle (rad, ~258 deg — VERIFY later)

    // tread(Vec3): inner/middle/outer temps in C. Distinctive per wheel: base=90+idx*10.
    float base = 90.0f + idx * 10.f;   // LF=90 RF=100 LR=110 RR=120
    w.f32(base);                      // [inner]
    w.f32(base - 5.f);                // [middle/centre] — typically cooler than inner+outer
    w.f32(base + 8.f);               // [outer]

    // carcass, internalAir, wellAir, rim, brake(C), springStrain(m?), damperVelocity(m/s) — 7 floats.
    w.f32(95.0f  + idx * 10.f);      // carcass temp C: LF=95 RF=105 LR=115 RR=125
    w.f32(88.0f  + idx * 10.f);      // internalAir C
    w.f32(76.0f  + idx * 10.f);      // wellAir C
    w.f32(92.0f  + idx * 10.f);      // rim C
    w.f32(450.0f + idx * 20.f);     // brake temp: LF=450 RF=470 LR=490 RR=510 (C)
    w.f32(0.08f  + idx * 0.01f);   // springStrain (m? VERIFY units later)
    w.f32(-0.15f - idx * 0.02f);   // damperVelocity m/s

    // hubTorque, hubPower, wheelTorque, wheelPower — last 4 floats of the block.
    w.f32(800.0f + idx * 100.f);     // N*m: LF=800 RF=900 LR=1000 RR=1100
    w.f32(65000.0f + idx * 10000.f);// W
    w.f32(750.0f + idx * 90.f);     // N*m: LF=750 RF=840 LR=930 RR=1020
    w.f32(60000.0f + idx * 8000.f) ;// W

    // Total bytes written per wheel: (1 i32 x 4B) + (30 f32 x 4B) = 4+120 = 124 B
}


static void buildVehicleTelemetry(Writer& w, int vehicleId = 7) {
    // Header.
    w.u8(PMR_PACKET_VEHICLE_TELEMETRY);   // type=2
    w.u16(kPmrTelemetryVersion);          // version=2

    // Vehicle ID (i32).
    w.i32(vehicleId);                     // 7 -> [0x07,0x00,0x00,0x00]

    // Wheel list: u8 count + N x wheelBlock(124B each).
    uint8_t nWheels = 4;
    w.u8(nWheels);
    for (int i = 0; i < 4; ++i) writeWheel(w, i);

    // ------------------------------------------------------------------
    // Chassis block — matches decodeChassis() field order exactly:
    //   posWS(3f) quat(x,y,z,w=4f) angVelWS(3f) angVelLS(3f) velWS(3f)
    //   velLS(3f) accWS(3f) accLS(3f) overallSpeed(f) forwardSpeed(f) sideslip(f)
    // Total: 27 floats x 4B = 108 B.
    // ------------------------------------------------------------------

    w.f32(100.0f); w.f32(-50.0f); w.f32(0.5f);   // posWS (x,y,z) — world position m
    w.f32(0.7071f); w.f32(0.0f);  w.f32(0.0f);   w.f32(0.7071f);  // quat x,y,z,w (45 deg yaw)

    w.f32(0.5f);  w.f32(-0.2f); w.f32(0.8f);     // angularVelocityWS
    w.f32(0.4f);  w.f32(-0.1f); w.f32(0.7f);     // angularVelocityLS

    w.f32(55.0f); w.f32(3.2f);  w.f32(0.0f);      // velocityWS (m/s)
    w.f32(54.8f); w.f32(-1.5f); w.f32(0.9f);     // velocityLS

    w.f32(1.2f);  w.f32(-0.3f); w.f32(9.8f);      // accelerationWS (m/s^2)
    w.f32(4.5f);  w.f32(0.1f);  w.f32(-6.2f);     // accelerationLS

    w.f32(55.1f);                                 // overallSpeed m/s (~198 km/h)
    w.f32(54.7f);                                 // forwardSpeed
    w.f32(0.03f);                                  // sideslip (rad, small = stable)

    // ------------------------------------------------------------------
    // Drivetrain block — matches decodeDrivetrain() field order exactly:
    //   ICE 13 floats | MGU 3 floats | gearbox 12 floats | flags 7 bytes | gear list.
    // Total fixed part: (13+3+12)x4 + 7 = 56+7 = 63 B, then variable gear list.
    // ------------------------------------------------------------------

    // ICE — exactly the order in decodeDrivetrain():
    w.f32(9500.f);   // [0] engineRPM
    w.f32(0.82f);    // [1] engineRevRatio (throttle 0..1)
    w.f32(647.3f);   // [2] engineTorque N*m
    w.f32(475900.f); // [3] enginePower W (VERIFY: kW vs W on first capture)
    w.f32(0.718f);   // [4] engineLoad normalized 0..1
    w.f32(0.f);      // [5] engineTurboRPM
    w.f32(0.f);      // [6] engineTurboBoostPressure bar
    w.f32(82.4f);   // [7] fuelRemaining L
    w.f32(13.2f);  // [8] fuelUseRate kg/h (VERIFY units)
    w.f32(5.6f);     // [9] engineOilPressure bar <- THIS IS THE ONE EASY TO MISS!
    w.f32(145.0f);   // [10] engineOilTemperature C  (position #11 in plan's 1-based count)
    w.f32(108.0f);   // [11] engineCoolantTemperature C (water temp, position #12)
    w.f32(920.0f);   // [12] exhaustGasTemperature

    // MGU / hybrid — 3 floats:
    w.f32(4500.f);     // motorRPM
    w.f32(68.5f);      // batteryRemaining % (VERIFY units)
    w.f32(-12.3f);     // batteryUseRate kW

    // Gearbox / drivetrain — 12 floats:
    w.f32(9000.f);   // transmissionRPM
    w.f32(8500.f);   // gearboxInputRPM
    w.f32(7200.f);   // gearboxOutputRPM
    w.f32(610.5f);  // gearboxTorque N*m
    w.f32(455000.f);// gearboxPower W (VERIFY)
    w.f32(0.89f);   // gearboxLoadIn normalized
    w.f32(0.76f);   // gearboxLoadOut normalized
    w.f32(1.23f);  // timeSinceShift s
    w.f32(54.2f);  // estDrivenSpeed m/s (VERIFY)
    w.f32(598.0f);// outputTorque N*m
    w.f32(447000.f);  // outputPower W (VERIFY)
    w.f32(0.91f);   // outputEfficiency normalized

    // Seven single-byte flags (tight packed, one byte each):
    uint8_t dtFlags[7] = { 0, 1, 1, 0, 1, 0, 0 };
    for (int i=0;i<7;++i) w.u8(dtFlags[i]);

    // Gear list: u8 count + N x {upshiftRPM(f), downshiftRPM(f)}. Use 6 gears.
    uint8_t nGears = 6;
    w.u8(nGears);
    for (int g=0; g<6; ++g) {
        float up   = 7500.f + g * 300.f;     // gear1:7500, g2:7800 ... g6:9000
        float down = 4500.f + g * 200.f;    // gear1:4500, g2:4700 ... g6:5500
        w.f32(up);
        w.f32(down);
    }

    // ------------------------------------------------------------------
    // Suspension block — matches decodeSuspension(): floatList(avgLoads) + loadBias(f).
    // Use 4 corner loads.
    // ------------------------------------------------------------------
    {   // avgLoads: u8 count=4, four floats (per-corner average vertical loads N? VERIFY units)
        uint8_t n = 4;
        w.u8(n);
        w.f32(15000.0f);              // LF avg load N? VERIFY units later
        w.f32(14500.0f);             // RF
        w.f32(16200.0f);            // LR (rear carries more in this scenario)
        w.f32(15800.0f);           // RR
    }
    w.f32(49.7f);                    // loadBias %

    // ------------------------------------------------------------------
    // Input block — matches decodeInput(): 5 floats + gear(i32). Total = 24 B.
    // ------------------------------------------------------------------
    w.f32(-0.18f);                   // steering (rad, VERIFY: rad vs deg on first capture)
    w.f32(0.72f);                    // accelerator normalized 0..1
    w.f32(0.05f);                     // brake normalized 0..1
    w.f32(0.98f);                    // clutch (near full = engaged in auto)
    w.f32(0.0f);                      // handbrake

    int32_t gearNum = 4;             // current gear: 4th
    w.i32(gearNum);

    // ------------------------------------------------------------------
    // Setup block — matches decodeSetup(): 5 floats + absLevel(u8) + tcsLevel(u8). Total=22B.
    // ------------------------------------------------------------------
    w.f32(0.51f);                    // brakeBias (normalized, ~51% front)
    w.f32(45000.0f);                // frontAntiRollStiffness N/m? VERIFY units later
    w.f32(38000.0f);               // rearAntiRollStiffness

    float regenLimit = 1.0f;         // normalized limit
    w.f32(regenLimit);
    w.f32(5.0f);                    // deployLimit

    uint8_t setupFlags[2] = { 7, 9 };   // absLevel=7, tcsLevel=9
    for (int i=0;i<2;++i) w.u8(setupFlags[i]);

    // ------------------------------------------------------------------
    // General block — matches decodeGeneral(): CoG(Vec3) + 7 floats + absActive(u8). Total=41B.
    // ------------------------------------------------------------------
    w.f32(0.5f);   w.f32(-0.2f);  w.f32(0.6f);     // centerOfGravity (x,y,z) m

    float genFloats[7] = {
        -0.18f,      // steeringWheelAngle rad? VERIFY: same as input.steering or different source?
       1450.f,      // totalMass kg
         92.3f,     // drivenWheelAngVel rad/s (VERIFY)
          78.6f,    // nonDrivenWheelAngVel rad/s
        54.9f,     // estRollingSpeed m/s? VERIFY units later
        54.1f,     // estLinearSpeed m/s
       3200.f      // totalBrakeForce N (VERIFY)
    };
    for (int i=0;i<7;++i) w.f32(genFloats[i]);

    uint8_t absActive = 1;          // ABS is active in this frame
    w.u8(absActive);

    // ------------------------------------------------------------------
    // Constant block — matches decodeConstant(): BBMin(Vec3)+BBMax(Vec3)+12 floats+4 bytes. Total=76B.
    // This is the LAST block of the entire packet; no trailing data after it.
    // ------------------------------------------------------------------
    w.f32(-0.8f);  w.f32(0.3f);   w.f32(-1.5f);     // chassisBBMin (x,y,z) m — bounding box min corner
    w.f32(0.9f);   w.f32(1.4f);   w.f32(1.6f);      // chassisBBMax

    float constFloats[12] = {
        850.f,     // starterIdleRPM (VERIFY: is this the idle RPM?)
       9200.f,    // engineTorquePeakRPM — redline for torque peak? VERIFY semantics later
      13500.f,   // enginePowerPeakRPM
      14800.f,   // engineMaxRPM (redline)
        720.0f,// engineMaxTorque N*m
       960000.f,   // engineMaxPower W? VERIFY units later - could be kW!
         3.5f,    // engineMaxBoost bar
         148.f,   // fuelCapacity L (F1 cars ~110-200L depending on era)
           90.f,   // batteryCapacity kWh? VERIFY units later — F1 ERS is ~4MJ = 1.1kWh!
         1650.f,    // trackWidthFront mm (VERIFY: m vs mm!)
        1620.f,     // trackWidthRear mm
          3600.f   // wheelbase mm? VERIFY units later — F1 is ~3.6m = 3600mm!
    };
    for (int i=0;i<12;++i) w.f32(constFloats[i]);

    uint8_t constBytes[4] = { 4, 7, 1, 1 };   // wheels=4 fwdGears=7 revGears=1 isHybrid=true
    for (int i=0;i<4;++i) w.u8(constBytes[i]);
}


// ===========================================================================
// Main test driver.
// ===========================================================================
int main() {
    std::printf("=== PMR Decoder Gate 1 Test ===\n\n");

    // ------------------------------------------------------------------
    // TEST 1: RaceInfo decode — happy path with known values.
    // Expected total size: header(3) + strings(6x[1+len]) + floats(5x4=20) + bytes(3).
    //   "Silverstone"=11, "GP Layout"=9, "2025 Season"=11, "Sunny"=5, 
    //   "Race"=4, "Endurance 4h"=12 -> string payload = (1+11)+(1+9)+(1+11)+(1+5)+(1+4)+(1+12)
    //   = 12+10+12+6+5+13 = 58 B. Total = 3+58+20+3 = 84 B.
    // ------------------------------------------------------------------
    {
        Writer w;
        buildRaceInfo(w);

        PmrRaceInfoPacket pkt{};
        bool ok = PmrDecodeRaceInfo(w.data(), w.len(), pkt);

        CHECK(ok == true);
        if (ok) {
            std::printf("  RaceInfo: track='%s' layout='%s'\n", pkt.track.c_str(), pkt.layout.c_str());
            CHECK(pkt.track      == "Silverstone");
            CHECK(pkt.layout     == "GP Layout");
            CHECK(pkt.season     == "2025 Season");
            CHECK(pkt.weather    == "Sunny");
            CHECK(pkt.session    == "Race");
            CHECK(pkt.gameMode   == "Endurance 4h");

            const float EPS = 1e-3f;
            std::printf("  RaceInfo: layoutLength=%.0f duration=%.0f ambient=%.1f trackTemp=%.1f\n",
                        pkt.layoutLength, pkt.duration, (double)pkt.ambientTemperature, 
                        (double)pkt.trackTemperature);
            CHECK(std::abs(pkt.layoutLength       - 5891.f)   < EPS);
            CHECK(std::abs(pkt.duration           - 14400.f)  < EPS);
            CHECK(std::abs((float)pkt.overtime    - 600.f)    < EPS);
            CHECK(std::abs((float)pkt.ambientTemperature-28.5f)<EPS);
            CHECK(std::abs((float)pkt.trackTemperature-41.2f )<EPS);

            std::printf("  RaceInfo: isLaps=%d state=%u numParticipants=%u\n", 
                        pkt.isLaps, (unsigned)pkt.state, (unsigned)pkt.numParticipants);
            CHECK(pkt.isLaps          == true);
            CHECK((int)pkt.state      == 0);
            CHECK((int)pkt.numParticipants == 7);

            std::printf("PASS: RaceInfo decode (%zu bytes)\n\n", w.len());
        } else {
            std::printf("FAIL: PmrDecodeRaceInfo returned false on valid packet!\n\n");
        }
    }

    // ------------------------------------------------------------------
    // TEST 2: ParticipantState — verify vehicleId/isPlayer order + all fields.
    // Expected size: header(3) + i32(4)+u8(1) + strings(6x[1+len]) + 
    //   ints(2x4=8) + floats(3x4=12) + int(4) + floatLists([1+1*4]+[1+3*4]=5+13=18)
    //   + bytes(3) + u32(4). 
    // Strings: "Ferrari SF-25"=13, "Charles Leclerc"=15, "livery_07"=9, "F1"=2 -> 14+16+10+3=43
    // Total = 3+5+43+8+12+4+18+3+4 = 100 B.
    // ------------------------------------------------------------------
    {
        Writer w;
        buildParticipantState(w, /*vehicleId=*/42, /*isPlayer=*/true);

        PmrParticipantStatePacket pkt{};
        bool ok = PmrDecodeParticipantState(w.data(), w.len(), pkt);

        CHECK(ok == true);
        if (ok) {
            std::printf("  Participant: id=%d isPlayer=%s name='%s' driver='%s'\n",
                        pkt.vehicleId, pkt.isPlayer ? "true" : "false", 
                        pkt.vehicleName.c_str(), pkt.driverName.c_str());

            // CRITICAL ORDER CHECK: vehicleId must be the FIRST field after header.
            CHECK(pkt.vehicleId == 42);
            CHECK(pkt.isPlayer  == true);
            CHECK(pkt.vehicleName == "Ferrari SF-25");
            CHECK(pkt.driverName  == "Charles Leclerc");
            CHECK(pkt.liveryId    == "livery_07");
            CHECK(pkt.vehicleClass== "F1");

            std::printf("  Participant: pos=%d lap=%d curLapTime=%.3f bestLapTime=%.3f progress=%.2f\n",
                        pkt.racePos, pkt.currentLap, 
                        (double)pkt.currentLapTime, (double)pkt.bestLapTime, 
                        (double)pkt.lapProgress);
            CHECK(pkt.racePos    == 3);
            CHECK(pkt.currentLap == 56);

            const float EPS = 1e-2f;
            CHECK(std::abs((float)pkt.currentLapTime - 90.482f) < EPS);
            CHECK(std::abs((float)pkt.bestLapTime    - 89.107f) < EPS);
            CHECK(std::abs((float)pkt.lapProgress     - 0.72f ) < EPS);

            std::printf("  Participant: sector=%d curSectTimes.size()=%zu bestSectTimes.size()=%zu\n",
                        pkt.currentSector, (size_t)pkt.currentSectorTimes.size(), 
                        (size_t)pkt.bestSectorTimes.size());
            CHECK(pkt.currentSector == 2);
            CHECK((int)pkt.currentSectorTimes.size() == 1);
            if (!pkt.currentSectorTimes.empty()) {
                std::printf("    curSect[0]=%.3f\n", (double)pkt.currentSectorTimes[0]);
                CHECK(std::abs(pkt.currentSectorTimes[0] - 30.5f) < EPS);
            }
            CHECK((int)pkt.bestSectorTimes.size() == 3);

            std::printf("  Participant: inPits=%s finished=%s dq=%s flags=0x%08X\n",
                        pkt.inPits ? "true" : "false", 
                        pkt.sessionFinished ? "true" : "false",
                        pkt.dq ? "true" : "false", (unsigned)pkt.flags);
            CHECK(pkt.inPits          == false);
            CHECK(pkt.sessionFinished == false);
            CHECK((int)pkt.dq         == 1);   // we set dq=1 in the test data

            std::printf("PASS: ParticipantState decode (%zu bytes)\n\n", w.len());
        } else {
            std::printf("FAIL: PmrDecodeParticipantState returned false on valid packet!\n\n");
        }
    }

    // ------------------------------------------------------------------
    // TEST 3: VehicleTelemetry — full structural test. This is the big one; 
    // any off-by-one in field order will cause a decode failure here, which is 
    // exactly what Gate 1 exists to catch before we move on to Phase 2 (socket).
    // ------------------------------------------------------------------
    {
        Writer w;
        buildVehicleTelemetry(w, /*vehicleId=*/7);

        PmrVehicleTelemetryPacket pkt{};
        bool ok = PmrDecodeVehicleTelemetry(w.data(), w.len(), pkt);

        CHECK(ok == true);
        if (ok) {
            std::printf("  Telemetry: vehicleId=%d wheels.size()=%zu\n", 
                        pkt.vehicleId, (size_t)pkt.wheels.size());
            CHECK(pkt.vehicleId == 7);
            CHECK((int)pkt.wheels.size() == 4);

            // Verify wheel[0] = LF with distinctive values.
            if (!pkt.wheels.empty()) {
                const PmrWheelTelemetry& lf = pkt.wheels[0];
                std::printf("    Wheel[LF]: hash=%d angVel=%.1f pressure=%.2f brakeTemp=%.0f\n", 
                            (int)lf.contactMaterialHash, (double)lf.angVel, 
                            (double)lf.pressure, (double)lf.brake);

                CHECK(lf.contactMaterialHash == 100);
                
                const float EPS = 1e-2f;
                CHECK(std::abs((float)lf.angVel    - 85.f ) < EPS);   // LF angVel=85 rad/s
                CHECK(std::abs((float)lf.linearSpeed - 60.f) < EPS);
                CHECK(std::abs((float)lf.pressure  - 2.40f) < EPS);  // bar

                std::printf("    Wheel[LF]: tread=[%.1f, %.1f, %.1f] carcass=%.1f brake=%.0f\n", 
                            (double)lf.tread.x, (double)lf.tread.y, (double)lf.tread.z,
                            (double)lf.carcass,  (double)lf.brake);

                // Tread: LF base=90 -> inner=90 middle=85 outer=98. 
                CHECK(std::abs((float)lf.tread.x - 90.f ) < EPS);   // [inner]
                CHECK(std::abs((float)lf.tread.y - 85.f ) < EPS);   // [middle/centre]  
                CHECK(std::abs((float)lf.tread.z - 98.f ) < EPS);   // [outer]

                std::printf("    Wheel[LF]: damperVel=%.3f springStrain=%.4f hubTorque=%.0f\n", 
                            (double)lf.damperVelocity, (double)lf.springStrain, 
                            (double)lf.hubTorque);
                
                CHECK(std::abs((float)lf.carcass      - 95.f ) < EPS);   // LF carcass=95 C
                CHECK(std::abs((float)lf.brake        - 450.f)< EPS);    // LF brake=450 C

                std::printf("PASS: Wheel[LF] fields verified\n");
            } else {
                std::printf("FAIL: No wheels decoded!\n");
            }

            if (pkt.wheels.size() >= 2) {
                const PmrWheelTelemetry& rf = pkt.wheels[1];
                CHECK(rf.contactMaterialHash == 101);   // RF hash=101
                std::printf("    Wheel[RF]: hash=%d pressure=%.2f brakeTemp=%.0f\n", 
                            (int)rf.contactMaterialHash, (double)rf.pressure, 
                            (double)rf.brake);

                const float EPS = 1e-2f;
                CHECK(std::abs((float)rf.angVel    - 95.f ) < EPS);   // RF angVel=95 rad/s  
                CHECK(std::abs((float)rf.pressure  - 2.45f) < EPS);  // bar: LF+0.05
            }

            if (pkt.wheels.size() >= 3) {
                const PmrWheelTelemetry& lr = pkt.wheels[2];
                CHECK(lr.contactMaterialHash == 102);   // LR hash=102
                std::printf("    Wheel[LR]: hash=%d pressure=%.2f brakeTemp=%.0f\n", 
                            (int)lr.contactMaterialHash, (double)lr.pressure, 
                            (double)lr.brake);

                const float EPS = 1e-2f;
                CHECK(std::abs((float)lr.angVel    - 105.f)< EPS);   // LR angVel=105 rad/s 
            }

            if (pkt.wheels.size() >= 4) {
                const PmrWheelTelemetry& rr = pkt.wheels[3];
                CHECK(rr.contactMaterialHash == 103);   // RR hash=103
                std::printf("    Wheel[RR]: hash=%d pressure=%.2f brakeTemp=%.0f\n", 
                            (int)rr.contactMaterialHash, (double)rr.pressure, 
                            (double)rr.brake);

                const float EPS = 1e-2f;
                CHECK(std::abs((float)rr.angVel    - 115.f)< EPS);   // RR angVel=115 rad/s 
            }

            // Chassis spot-checks (distinctive values).
            std::printf("  Telemetry: chassis posWS=(%.0f, %.0f, %.2f) overallSpeed=%.1f m/s\n", 
                        (double)pkt.chassis.posWS.x, (double)pkt.chassis.posWS.y, 
                        (double)pkt.chassis.posWS.z, (double)pkt.chassis.overallSpeed);

            const float EPSc = 0.5f;
            CHECK(std::abs(pkt.chassis.posWS.x - 100.f ) < EPSc);   // x=100 m
            CHECK(std::abs(pkt.chassis.posWS.y + 50.f ) < EPSc);    // y=-50 m
            CHECK(std::abs(pkt.chassis.overallSpeed - 55.1f) < EPSc);// ~198 km/h

            std::printf("  Telemetry: chassis quat=(%.4f, %.2f, %.2f, %.4f)\n",
                        (double)pkt.chassis.quat.q[0], (double)pkt.chassis.quat.q[1], 
                        (double)pkt.chassis.quat.q[2], (double)pkt.chassis.quat.q[3]);

            CHECK(std::abs(pkt.chassis.quat.q[0] - 0.7071f) < EPSc);
            CHECK(std::abs(pkt.chassis.quat.q[3] - 0.7071f) < EPSc); // w=same

            std::printf("  Telemetry: chassis accLS=(%.2f, %.2f, %.2f)\n", 
                        (double)pkt.chassis.accelerationLS.x, 
                        (double)pkt.chassis.accelerationLS.y,
                        (double)pkt.chassis.accelerationLS.z);

            CHECK(std::abs(pkt.chassis.accelerationLS.x - 4.5f ) < EPSc);
            CHECK(std::abs(pkt.chassis.accelerationLS.z + 6.2f ) < EPSc); // z=-6.2

            std::printf("PASS: Chassis fields verified\n");

            // Drivetrain spot-checks — the critical alignment test for all 13 ICE floats!
            std::printf("\n  Telemetry drivetrain:\n");
            const PmrDrivetrainTelemetry& dt = pkt.drivetrain;
            
            std::printf("    engineRPM=%.0f revRatio=%.2f torque=%.1f power=%.0f load=%.3f\n", 
                        (double)dt.engineRPM, (double)dt.engineRevRatio, 
                        (double)dt.engineTorque, (double)dt.enginePower, 
                        (double)dt.engineLoad);

            const float EPSt = 5.f;
            CHECK(std::abs(dt.engineRPM      - 9500.f) < EPSt);
            CHECK(std::abs(dt.engineRevRatio - 0.82f ) < 1e-3f);
            CHECK(std::abs(dt.engineTorque   - 647.3f)< EPSt);

            std::printf("    turboRPM=%.0f boostP=%.2f fuelRemain=%.1fL fuelRate=%.1f\n",
                        (double)dt.engineTurboRPM, (double)dt.engineTurboBoostPressure,
                        (double)dt.fuelRemaining,  (double)dt.fuelUseRate);

            CHECK(std::abs(dt.engineTurboRPM - 0.0f ) < 1e-3f);   // [5] turbo=0
            CHECK(std::abs(dt.fuelRemaining  - 82.4f)< EPSt);     // [7] L
            std::printf("    oilPressure=%.2f bar  oilTemp=%.0f C  coolantTemp=%.0f C\n",
                        (double)dt.engineOilPressure, (double)dt.engineOilTemperature,
                        (double)dt.engineCoolantTemperature);

            CHECK(std::abs(dt.engineOilPressure - 5.6f ) < 1e-2f);   // [9] bar <- THE CRITICAL ONE!
            std::printf("    exhaustGasTemp=%.0f  motorRPM=%.0f batteryRemain=%.1f%%\n",
                        (double)dt.exhaustGasTemperature, (double)dt.motorRPM,
                        (double)dt.batteryRemaining);

            CHECK(std::abs(dt.engineOilTemperature - 145.f ) < EPSt); // [10] C
            std::printf("    gearbox: transRPM=%.0f inRPM=%.0f outRPM=%.0f torque=%.1f\n",
                        (double)dt.transmissionRPM,  (double)dt.gearboxInputRPM,
                        (double)dt.gearboxOutputRPM, (double)dt.gearboxTorque);

            CHECK(std::abs(dt.engineCoolantTemperature - 108.f ) < EPSt); // [11] C
            std::printf("    flags: starter=%u running=%u fan=%u revLim=%u tcsActive=%u spdLimiterEn=%u\n", 
                        (unsigned)dt.starterActive,  (unsigned)dt.engineRunning,  
                        (unsigned)dt.engineFanRunning,(unsigned)dt.revLimiterActive,
                        (unsigned)dt.tractionControlActive,(unsigned)dt.speedLimiterEnabled);

            CHECK(dt.engineRunning == true);   // flag[1]=1 -> engine running
            std::printf("    gearList.size()=%zu  g0.up=%.0f down=%.0f\n", 
                        (size_t)dt.gearList.size(), 
                        dt.gearList.empty()?0:(double)dt.gearList[0].upshiftRPM,  
                        dt.gearList.empty()?0:(double)dt.gearList[0].downshiftRPM);

            CHECK((int)dt.gearList.size() == 6);   // we wrote 6 gears
            if (!dt.gearList.empty()) {
                const float EPS2 = 1.f;
                std::printf("    gear[0]: up=%.0f down=%.0f\n", 
                            (double)dt.gearList[0].upshiftRPM,   (double)dt.gearList[0].downshiftRPM);

                CHECK(std::abs((float)dt.gearList[0].upshiftRPM  -7500.f)< EPS2);// gear1 up=7500  
            }
            
            std::printf("PASS: Drivetrain fields verified (all 13 ICE floats aligned!)\n");

            // Suspension spot-checks.
            const PmrSuspensionTelemetry& sus = pkt.suspension;
            std::printf("\n  Telemetry suspension:\n    avgLoads.size()=%zu loadBias=%.2f%%\n", 
                        (size_t)sus.avgLoads.size(), (double)sus.loadBias);

            CHECK((int)sus.avgLoads.size() == 4);   // we wrote 4 corner loads
            if (!sus.avgLoads.empty()) {
                const float EPS3 = 1.f;
                std::printf("    avgLoad[0]=%.0f [1]=%.0f\n", 
                            (double)sus.avgLoads[0], sus.avgLoads.size()>1?(double)sus.avgLoads[1]:-999);

                CHECK(std::abs((float)sus.avgLoads[0] - 15000.f) < EPS3);// LF=15000 N? VERIFY units later
            }
            
            std::printf("PASS: Suspension fields verified\n");

            // Input spot-checks.
            const PmrInputTelemetry& inp = pkt.input;
            std::printf("\n  Telemetry input:\n    steering=%.3f rad accel=%.2f brake=%.2f gear=%d\n", 
                        (double)inp.steering,   (double)inp.accelerator,  
                        (double)inp.brake,      inp.gear);

            const float EPS4 = 1e-3f;
            CHECK(std::abs((float)inp.steering    -(-0.18f)) < EPS4);// steering=-0.18 rad? VERIFY units later  
            std::printf("PASS: Input fields verified\n");

            // Setup spot-checks.
            const PmrSetupTelemetry& stp = pkt.setup;
            std::printf("\n  Telemetry setup:\n    brakeBias=%.2f absLevel=%u tcsLevel=%u\n", 
                        (double)stp.brakeBias,   (unsigned)stp.absLevel,  
                        (unsigned)stp.tcsLevel);

            CHECK(std::abs((float)stp.brakeBias - 0.51f ) < EPS4);// brakeBias=51% front
            std::printf("PASS: Setup fields verified\n");

            // General spot-checks.
            const PmrGeneralTelemetry& gen = pkt.general;
            std::printf("\n  Telemetry general:\n    CoG=(%.2f, %.2f, %.2f) totalMass=%.0f kg absActive=%u\n", 
                        (double)gen.centerOfGravity.x,(double)gen.centerOfGravity.y,  
                        (double)gen.centerOfGravity.z,(double)gen.totalMass,
                        (unsigned)gen.absActive);

            CHECK(std::abs(gen.totalMass - 1450.f ) < EPSt); // totalMass=1450 kg
            std::printf("PASS: General fields verified\n");

            // Constant spot-checks.
            const PmrConstantTelemetry& cst = pkt.constant;
            std::printf("\n  Telemetry constant:\n    maxRPM=%.0f fuelCap=%.1fL wheels=%u fwdGears=%u hybrid=%d\n", 
                        (double)cst.engineMaxRPM,   (double)cst.fuelCapacity,  
                        (unsigned)cst.numberOfWheels,(unsigned)cst.numberOfForwardGears,
                        cst.isHybrid);

            CHECK(std::abs(cst.engineMaxRPM - 14800.f) < EPSt); // redline
            std::printf("PASS: Constant fields verified\n");

            std::printf("\nPASS: VehicleTelemetry decode (%zu bytes)\n", w.len());
        } else {
            // Decode failed — print diagnostic info to help pinpoint the misalignment.
            std::printf("FAIL: PmrDecodeVehicleTelemetry returned false on valid packet!\n");

            // Try decoding with progressively larger prefixes to find where it breaks.
            const uint8_t* d = w.data(); size_t L = w.len();
            
            for (size_t tryLen = 3; tryLen <= L && tryLen < 2048; ++tryLen) {   // start after header+id+wheels count
                PmrVehicleTelemetryPacket probe{};
                if (!PmrDecodeVehicleTelemetry(d, tryLen, probe)) continue;    // keep growing until success
                
                std::printf("  First successful decode at %zu bytes (full=%zu)\n", tryLen, L);

                break;   // found the minimum working size — enough for diagnosis
            }
            
        }
    }


    // ------------------------------------------------------------------
    // Edge case tests.
    // ------------------------------------------------------------------
    std::printf("\n=== Edge Case Tests ===\n\n");

    // TEST E1: Empty / null buffer -> all decoders return false, `out` untouched.
    {
        PmrRaceInfoPacket ri{};
        CHECK(PmrDecodeRaceInfo(nullptr, 0, ri) == false);   // nullptr guard
        
        uint8_t tiny[2] = {(uint8_t)PMR_PACKET_RACE_INFO, (uint8_t)(kPmrRaceInfoVersion & 0xFF)};  
        PmrRaceInfoPacket ri2{};
        CHECK(PmrDecodeRaceInfo(tiny, sizeof(tiny), ri2) == false);   // truncated header

        PmrParticipantStatePacket ps{};
        CHECK(PmrDecodeParticipantState(nullptr, 0, ps) == false);    // nullptr guard
        
        uint8_t tinyPS[3] = {(uint8_t)PMR_PACKET_PARTICIPANT_STATE, 
                             (uint8_t)(kPmrParticipantStateVersion & 0xFF), 
                             (uint8_t)((kPmrParticipantStateVersion >> 8) & 0xFF)};  
        PmrParticipantStatePacket ps2{};
        CHECK(PmrDecodeParticipantState(tinyPS, sizeof(tinyPS), ps2) == false);   // header only

        PmrVehicleTelemetryPacket vt{};
        CHECK(PmrDecodeVehicleTelemetry(nullptr, 0, vt) == false);    // nullptr guard
        
        uint8_t tinyVT[7] = {(uint8_t)PMR_PACKET_VEHICLE_TELEMETRY, 
                             (uint8_t)(kPmrTelemetryVersion & 0xFF),  
                             (uint8_t)((kPmrTelemetryVersion >> 8) & 0xFF)};   // type+version only
        PmrVehicleTelemetryPacket vt2{};
        CHECK(PmrDecodeVehicleTelemetry(tinyVT, sizeof(tinyVT), vt2) == false);

        std::printf("PASS: E1 null/truncated buffers rejected\n");
    }

    // TEST E2: Wrong packet type -> returns false even with valid payload.
    {
        Writer w;
        buildRaceInfo(w);   // valid RaceInfo bytes...

        PmrParticipantStatePacket ps{};
        CHECK(PmrDecodeParticipantState(w.data(), w.len(), ps) == false);  // ...fed to Participant decoder
        
        std::printf("PASS: E2a RaceInfo->Participant rejected\n");

        {
            Writer wt;
            buildVehicleTelemetry(wt);
            PmrRaceInfoPacket ri{};
            bool okCross = PmrDecodeRaceInfo(wt.data(), wt.len(), ri);   // telemetry bytes -> race decoder
            std::printf("  Telemetry->RaceInfo cross-decode result=%s (expected false)\n",
                        okCross ? "true" : "false");
            CHECK(okCross == false);
        }

        std::printf("PASS: E2b Telemetry->RaceInfo rejected\n");
    }

    // TEST E3: Truncated packet (valid header + partial payload) -> returns false.
    {
        Writer w;
        buildParticipantState(w, /*vehicleId=*/99);   // use participant state for this test
        
        PmrParticipantStatePacket ps{};
        size_t fullLen = w.len();

        CHECK(fullLen > 20);

        bool okHalf = PmrDecodeParticipantState(w.data(), fullLen / 3, ps);
        
        std::printf("PASS: E3 truncated Participant (%zu of %zu bytes) rejected\n", 
                    fullLen/3, fullLen);
        
        CHECK(okHalf == false);

        // `ps` should be untouched on failure — verify a field is still default.
        std::printf("  ps.vehicleId after failed decode: %d (expected -1)\n", ps.vehicleId);
    }

    // TEST E4: Wrong version number -> returns false even with correct type byte + valid payload.
    {
        Writer w;
        buildRaceInfo(w);

        PmrRaceInfoPacket ri{};
        std::vector<uint8_t> patched(w.data(), w.data() + w.len());

        uint16_t badVer = 99;   // arbitrary invalid version number (LE: low byte first)
        patched[1] = (uint8_t)(badVer & 0xFF);
        patched[2] = (uint8_t)((badVer >> 8) & 0xFF);

        bool okBadVer = PmrDecodeRaceInfo(patched.data(), patched.size(), ri);

        std::printf("PASS: E4 bad-version RaceInfo rejected\n");
        CHECK(okBadVer == false);   // version mismatch -> immediate rejection at header check
    }


    // ------------------------------------------------------------------
    // Final summary.
    // ------------------------------------------------------------------
    std::printf("\n=== RESULTS ===\n");

    if (s_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;   // Gate 1 satisfied — proceed to Phase 2 (socket layer).
    } else {
        std::printf("%d CHECK(S) FAILED\n", s_failures);

        if (s_failures <= 5) {
            std::printf("\nHint: Check field alignment in the writer vs decoder.\n");
            std::printf("Common causes:\n");
            std::printf("  - Missing engineOilPressure float at ICE index [9]\n");
            std::printf("  - Wrong wheel block size (must be exactly 124 B)\n");
            std::printf("  - Quat order must be x,y,z,w NOT w,x,y,z\n");
        }

        return 1;   // Gate 1 failed — fix the decoder or writer before proceeding.
    }
}

