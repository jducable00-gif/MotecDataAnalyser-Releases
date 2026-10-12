//---------------------------------------------------------------------------
// test_tablet_server.cpp — Gate C functional verification harness.
// Starts TabletReportServer on a test port, verifies:
//   1) GET / returns HTML with expected markers (section cards, tyre grid, etc.)
//   2) GET /api/report initially returns the empty document JSON
//   3) After setPayload(), GET /api/report returns exact payload
//   4) stop() is idempotent and clean
//---------------------------------------------------------------------------

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32")

#include "../HttpServer.h"
#include <cstdio>
#include <cstring>
#include <string>

// Stubs to satisfy cp32mt.lib's VCL-aware CRT wrapper in a console app.
extern "C" {
void __InitVCL();
void __ExitVCL() {}
}

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; } \
    else         { printf("PASS: %s\n", msg); } \
} while(0)

// Simple HTTP GET helper — connects to 127.0.0.1:<port>, sends request, reads full response.
static std::string httpGet(const char* path, int port) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return "";

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) { closesocket(s); WSACleanup(); return ""; }

    std::string req = std::string("GET ") + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    send(s, req.c_str(), (int)req.size(), 0);

    // Read until connection closes (server sends Connection:close).
    std::string resp;
    char buf[4096];
    int n;
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, n);
    }
    closesocket(s);
    WSACleanup();
    return resp;
}

// Extract body after "\r\n\r\n" from an HTTP response.
static std::string httpBody(const std::string& raw) {
    size_t pos = raw.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return raw.substr(pos + 4);
}

// Check that a string contains all given substrings.
static bool containsAll(const std::string& haystack, const char* const* needles, int count) {
    for (int i = 0; i < count; ++i)
        if (haystack.find(needles[i]) == std::string::npos) return false;
    return true;
}

int main() {
    printf("=== Gate C: TabletReportServer functional test ===\n\n");

    const int PORT = 18765; // unlikely to clash with anything real.

    // --- Start server (no payload set yet) ---
    TabletReportServer srv(PORT);
    bool started = srv.start();
    CHECK(started, "server start() returns true on test port");
    if (!started) { printf("Cannot continue without a running server.\n"); return 1; }

    // Small delay to let the accept loop thread spin up.
    Sleep(200);

    // --- Test 1: GET /api/report with no payload → empty document JSON ---
    std::string resp = httpGet("/api/report", PORT);
    CHECK(!resp.empty(), "GET /api/report returns a response");
    std::string body = httpBody(resp);
    const char* expectedEmpty[] = { "\"version\":0", "\"hasData\":false", "\"sections\":{}" };
    CHECK(containsAll(body, expectedEmpty, 3), "empty document has version:0, hasData:false, sections:{}");

    // --- Test 2: GET / returns HTML with key structural markers ---
    resp = httpGet("/", PORT);
    CHECK(!resp.empty(), "GET / returns a response");
    body = httpBody(resp);
    const char* htmlMarkers[] = {
        "<!DOCTYPE html>",
        "tyreGrid",          // tyre heat map container
        "brakeWrap",         // brake bars container
        "damperGrid",        // damper histograms container
        "sectionsWrap",      // section cards container
        "/api/report",       // poll endpoint in JS
        "lastVersion"        // version tracking variable
    };
    CHECK(containsAll(body, htmlMarkers, 7), "HTML contains all key structural markers");

    // --- Test 3: setPayload then GET /api/report returns exact payload ---
    const char* testJson = "{\"version\":42,\"meta\":{\"carName\":\"Test Car\",\"trackName\":\"Circuit X\","
                           "\"lapCountIncluded\":5,\"hasData\":true},"
                           "\"sections\":{\"Summary\":{\"content\":\"All good.\",\"hasRecommendation\":false}},"
                           "\"lapStats\":{\"tyreTempsAvg\":[82.1,83.0,84.5,81.9,82.7,83.8,80.5,81.2,82.0,79.8,80.6,81.4],"
                           "\"brakeTempAvg\":[350.0,345.0,340.0,338.0],\"brakeTempMax\":[420.0,415.0,410.0,405.0]}}";
    srv.setPayload(testJson);

    resp = httpGet("/api/report", PORT);
    body = httpBody(resp);
    CHECK(body.find("\"version\":42") != std::string::npos, "payload version 42 present after setPayload");
    CHECK(body.find("Test Car") != std::string::npos, "carName 'Test Car' in payload response");
    CHECK(body.find("tyreTempsAvg") != std::string::npos, "tyreTempsAvg key in payload response");

    // --- Test 4: stop() is idempotent (call twice) and clean ---
    srv.stop();
    printf("PASS: first stop() completed without hang\n");
    bool runningAfterStop = srv.isRunning();
    CHECK(!runningAfterStop, "isRunning()==false after stop()");

    // Second stop should be a safe no-op.
    srv.stop();
    printf("PASS: second stop() (idempotent) completed cleanly\n");

    // --- Test 5: restart works (start/stop cycle) ---
    bool restarted = srv.start();
    CHECK(restarted, "server can start again after stop (restart cycle)");
    if (restarted) {
        Sleep(100);
        resp = httpGet("/api/report", PORT);
        body = httpBody(resp);
        // After restart the payload is preserved in memory.
        CHECK(body.find("\"version\":42") != std::string::npos, "payload survives stop/start cycle");
        srv.stop();
    }

    printf("\n=== Result: %d failure(s) ===\n", g_failures);
    return (g_failures == 0) ? 0 : 1;
}
