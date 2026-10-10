//---------------------------------------------------------------------------
// pmr_test_common.h — shared helpers for the standalone PMR gate tests.
//
// Contains:
//   - Writer        : minimal little-endian binary writer (tight-packed, no padding)
//   - buildRaceInfo / buildParticipantState / buildVehicleTelemetry
//                     byte-exact datagram builders matching C# UDPProtocol.cs wire order
//
// Included by both test_pmr_decoder.cpp (Gate 1) and test_pmr_socket.cpp (Gate 2).
// All functions are inline so the header can be shared across translation units.
//---------------------------------------------------------------------------

#pragma once

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "PmrLiveCapture.h"


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
inline void buildRaceInfo(Writer& w) {
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
inline void buildParticipantState(Writer& w, int vehicleId = 42, bool isPlayer = true) {
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

inline void writeWheel(Writer& w, int idx /*0=LF 1=RF 2=LR 3=RR*/) {
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


inline void buildVehicleTelemetry(Writer& w, int vehicleId = 7) {
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
