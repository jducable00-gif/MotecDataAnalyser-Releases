//---------------------------------------------------------------------------
// test_pmr_socket.cpp — Gate 2 standalone self-test for the PMR socket layer.
//
// Verifies (per docs/PMR_UDP_LIVE_CAPTURE_PLAN.md, GATE 2):
//   S1: PmrOpenSocket() fails cleanly with an error string when the port is
//       already occupied by another UDP socket.
//   S2: PmrOpenSocket() succeeds on a free loopback port; PmrIsConnected().
//   S3: A sender thread (raw Winsock sendto over 127.0.0.1) emits the three
//       hand-crafted packets repeatedly while the main thread loops
//       PmrDrainAllPackets() at ~10 Hz for a few seconds — per-tick counts are
//       printed, total > 0 across multiple ticks (no blocking, no crash).
//   S4: Decoded telemetry via PmrLastTelemetry() matches the builder inputs.
//   S5: Stopping the sender -> drain returns 0 cleanly and promptly (no hang);
//       last-telemetry cache stays valid afterwards.
//   S6: Close/re-open lifecycle is idempotent.
//
// The C# reference app simpleudpserver.exe is a viewer, not a sender — hence
// this in-process UDP send thread as the test source of truth (same wire bytes
// as Gate 1 via pmr_test_common.h).
//
// Build with bcc32c:
//   call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat"
//   cd scripts
//   bcc32c -I.. -o test_pmr_socket.exe ..\PmrLiveCapture.cpp test_pmr_socket.cpp ws2_32.lib
//---------------------------------------------------------------------------

#include <winsock2.h>          // MUST precede windows.h (SOCKET type)
#pragma comment(lib, "Ws2_32.lib")
#include <windows.h>           // Sleep / CreateThread / GetTickCount

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#include "pmr_test_common.h"   // Writer + packet builders (identical wire data to Gate 1)


// Satisfy the extern in MotecSetupAnalyzer.h (pulled transitively via PmrLiveCapture.h).
Thresholds g_thresholds = Thresholds();

static int s_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { ++s_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while(0)


// ===========================================================================
// Sender thread state (pre-C++11 toolchain: Win32 threads + interlocked flag).
// The sender owns its own UDP socket and never touches the receiver's statics.
// ===========================================================================
static const int kTestPort = 7590;

static SOCKET s_senderSock   = INVALID_SOCKET;
static volatile LONG s_stopSender = 0;          // InterlockedExchange to stop
static std::vector<uint8_t> s_pktRaceInfo, s_pktParticipant, s_pktTelemetry;


// One burst: send all three packet types. ~20 ms between bursts => ~150 pps total.
static DWORD WINAPI SenderProc(LPVOID) {
    sockaddr_in dst = {};
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 — unicast loopback, no multicast needed
    dst.sin_port        = htons((u_short)kTestPort);

    const struct sockaddr* dstAddr = reinterpret_cast<const struct sockaddr*>(&dst);   // C++: no implicit derived->base
    while (!InterlockedCompareExchange(&s_stopSender, 0, 0)) {
        sendto(s_senderSock, (const char*)s_pktRaceInfo.data(),      (int)s_pktRaceInfo.size(),     0, dstAddr, sizeof(dst));
        sendto(s_senderSock, (const char*)s_pktParticipant.data(),   (int)s_pktParticipant.size(),  0, dstAddr, sizeof(dst));
        sendto(s_senderSock, (const char*)s_pktTelemetry.data(),     (int)s_pktTelemetry.size(),    0, dstAddr, sizeof(dst));
        Sleep(20);
    }
    return 0;
}


// ===========================================================================
static void testBindFailure() {
    std::printf("--- S1: bind failure on occupied port ---\n");

    // Occupy the port with a plain UDP socket (no SO_REUSEADDR, like PmrOpenSocket).
    SOCKET occ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(occ != INVALID_SOCKET);
    sockaddr_in sa = {};
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons((u_short)kTestPort);
    int br = bind(occ, (sockaddr*)&sa, sizeof(sa));
    CHECK(br == 0);

    // PmrOpenSocket must fail with a non-empty error message.
    std::string err;
    bool ok = PmrOpenSocket(kTestPort, err);
    CHECK(!ok);
    CHECK(!err.empty());
    if (!ok) std::printf("   (expected failure: %s)\n", err.c_str());

    closesocket(occ);
}


static void testMainFlow() {
    std::printf("--- S2-S5: open, drain loop at ~10 Hz, verify decode, stop sender ---\n");

    // Build the three datagrams once (identical wire bytes to Gate 1).
    { Writer w; buildRaceInfo(w);            s_pktRaceInfo.assign(w.data(), w.data() + w.len()); }
    std::printf("   raceInfo %zu B\n",      s_pktRaceInfo.size());
    { Writer w; buildParticipantState(w);    s_pktParticipant.assign(w.data(), w.data() + w.len()); }
    std::printf("   participant %zu B\n",   s_pktParticipant.size());
    { Writer w; buildVehicleTelemetry(w);    s_pktTelemetry.assign(w.data(), w.data() + w.len()); }  // the big one (968 B)
    std::printf("   telemetry %zu B\n",     s_pktTelemetry.size());

    // S2: open on the free port.
    std::string err;
    bool ok = PmrOpenSocket(kTestPort, err);
    CHECK(ok);
    if (!ok) { std::printf("   OPEN FAILED: %s — aborting main flow\n", err.c_str()); return; }
    CHECK(PmrIsConnected());

    // Sender socket + thread.
    s_senderSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(s_senderSock != INVALID_SOCKET);
    InterlockedExchange(&s_stopSender, 0);
    HANDLE hThread = CreateThread(NULL, 0, SenderProc, NULL, CREATE_SUSPENDED, NULL);
    CHECK(hThread != NULL);
    if (hThread) { ResumeThread(hThread); std::printf("   sender thread started -> 127.0.0.1:%d\n", kTestPort); }

    // S3: drain loop at ~10 Hz for ~3 s, printing per-tick counts.
    const int kTicks = 30;                  // 30 x 100 ms = 3 seconds
    int totalDrained = 0, ticksWithData = 0;
    DWORD tStart = GetTickCount();
    for (int i = 0; i < kTicks && !InterlockedCompareExchange(&s_stopSender, 0, 0); ++i) {
        Sleep(100);                         // ~10 Hz pump cadence (same as the VCL timer in production)
        int n = PmrDrainAllPackets();       // must return promptly — never blocks
        totalDrained += n;
        if (n > 0) ++ticksWithData;
        std::printf("   [tick %2d] drained=%3d  total=%4d\n", i + 1, n, totalDrained);
    }
    DWORD tElapsed = GetTickCount() - tStart;
    CHECK(totalDrained > 0);                // gate: packet counts > 0 ...
    CHECK(ticksWithData >= kTicks / 2);     // ... sustained across multiple ticks (no stall)

    // S4: decoded telemetry must match the builder inputs.
    const PmrVehicleTelemetryPacket* t = PmrLastTelemetry();
    CHECK(t != NULL);
    if (t) {
        float EPSf  = 1e-2f;                // wheel/chassis floats are exact round-trips; small eps for safety
        float EPSbig= 5.f;                  // large-magnitude values (RPM, W, N*m)

        CHECK(t->vehicleId == 7);
        CHECK((int)t->wheels.size() == 4);
        if ((int)t->wheels.size() == 4) {
            const PmrWheelTelemetry& lf = t->wheels[0];
            const PmrWheelTelemetry& rr = t->wheels[3];
            CHECK(lf.contactMaterialHash == 100);
            CHECK(std::fabs(lf.pressure - (2.40f + 0 * 0.05f)) < EPSf);          // LF pressure bar
            CHECK(std::fabs(rr.brake    - (450.f + 3 * 20.f))   < EPSf);         // RR brake temp C = 510
            CHECK(lf.tread.x == 90.0f && lf.tread.y == 85.0f && lf.tread.z == 98.0f);  // inner/middle/outer

            const PmrWheelTelemetry& rf = t->wheels[1];
            CHECK(std::fabs(rf.damperVelocity - (-0.17f)) < EPSf);               // RF damper m/s
        }
        CHECK(std::fabs(t->chassis.overallSpeed  - 55.1f)   < EPSf);
        CHECK(std::fabs(t->chassis.velocityWS.x  - 55.0f)   < EPSf);

        const PmrDrivetrainTelemetry& d = t->drivetrain;
        CHECK(std::fabs(d.engineRPM             - 9500.f)    < EPSbig);
        CHECK(std::fabs(d.engineOilPressure     - 5.6f)      < EPSf);            // the easy-to-miss [9] slot
        CHECK(std::fabs(d.engineCoolantTemperature - 108.0f) < EPSf);
        CHECK((int)d.gearList.size() == 6);
        if ((int)d.gearList.size() == 6) {
            CHECK(std::fabs(d.gearList[5].upshiftRPM   - (7500.f + 5 * 300.f)) < EPSbig);
            CHECK(std::fabs(d.gearList[2].downshiftRPM - (4500.f + 2 * 200.f)) < EPSbig);
        }

        const PmrInputTelemetry& in = t->input;
        CHECK(in.gear == 4);
        CHECK(std::fabs(in.accelerator - 0.72f) < EPSf);

        const PmrSetupTelemetry& s = t->setup;
        CHECK(s.absLevel == 7 && s.tcsLevel == 9);
        CHECK(std::fabs(s.brakeBias - 0.51f) < EPSf);

        const PmrConstantTelemetry& c = t->constant;
        CHECK(c.numberOfWheels == 4 && c.isHybrid == 1);
        CHECK(std::fabs(c.wheelbase - 3600.f) < EPSbig);
    } else {
        std::printf("   (no telemetry decoded — S4 skipped)\n");
    }

    // S5: stop the sender; drain must return to 0 cleanly and promptly.
    InterlockedExchange(&s_stopSender, 1);
    if (hThread) {
        DWORD wr = WaitForSingleObject(hThread, 5000);   // hard cap — a hang would fail the gate
        CHECK(wr == WAIT_OBJECT_0);
        CloseHandle(hThread); hThread = NULL;
    }

    int leftover = PmrDrainAllPackets();                  // flush anything in flight (may be > 0)
    std::printf("   after stop: flushed %d, then draining idle...\n", leftover);
    DWORD tIdleStart = GetTickCount();
    for (int i = 0; i < 5; ++i) {                         // five consecutive empty drains at ~10 Hz
        int n = PmrDrainAllPackets();
        if (i == 4) CHECK(n == 0);                        // gate: drain returns 0 cleanly, no hang
    }
    DWORD tIdleMs = GetTickCount() - tIdleStart;
    std::printf("   idle drains took %lu ms total\n", (unsigned long)tIdleMs);

    const PmrVehicleTelemetryPacket* t2 = PmrLastTelemetry();
    CHECK(t2 != NULL && t2->vehicleId == 7);              // cache stays valid after sender stops
}


static void testLifecycle() {
    std::printf("--- S6: close / re-open lifecycle ---\n");

    PmrCloseSocket();
    CHECK(!PmrIsConnected());
    PmrCloseSocket();                                     // idempotent — must not crash

    std::string err;
    bool ok = PmrOpenSocket(kTestPort, err);              // port is free again after S5's sender exited
    CHECK(ok);
    if (!ok) std::printf("   RE-OPEN FAILED: %s\n", err.c_str());
    CHECK(PmrIsConnected());

    int n = PmrDrainAllPackets();                         // nothing pending -> 0 immediately (non-blocking proof)
    CHECK(n == 0);

    PmrCloseSocket();
}


// ===========================================================================
int main() {
    std::printf("=== PMR Gate 2: socket layer self-test ===\n");

    WSADATA wsa;                                                // init Winsock for this process's own raw sockets
    CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);              // (PmrOpenSocket does its own lazy startup too — ref-counted per process)

    testBindFailure();   // S1 — must run before the real open (occupies kTestPort briefly)
    testMainFlow();      // S2-S5
    testLifecycle();     // S6

    std::printf("===========================================\n");
    if (s_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", s_failures);
    return 1;
}
