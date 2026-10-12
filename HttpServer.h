//---------------------------------------------------------------------------
// HttpServer.h — Minimal embedded HTTP server for the Tablet Report Viewer.
//
// Phase B of docs/TABLET_REPORT_VIEWER_PLAN.md: a tiny Winsock GET-only server,
// thread-per-request, that serves two endpoints on 0.0.0.0:<port>:
//   GET /            -> tablet page (placeholder until Phase C swaps in real HTML)
//   GET /api/report  -> current JSON payload (always 200; starts as empty document)
//
// Design constraints:
// - No external dependencies (hand-rolled Winsock, per plan §10).
// - Port is ALWAYS a parameter — never hardcoded here.
// - Payload + version are mutex-guarded: handler threads read them while the UI
//   thread calls setPayload() after every analysis completion (§B2/B5).
// - stop() is idempotent and safe to call from any context; repeated start/stop
//   cycles must not leak sockets or hang (Gate B requirement).
//---------------------------------------------------------------------------

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class TabletReportServer {
public:
    explicit TabletReportServer(int port);
    ~TabletReportServer();

    // Non-copyable, non-movable (owns a socket + thread).
    TabletReportServer(const TabletReportServer&) = delete;
    TabletReportServer& operator=(const TabletReportServer&) = delete;

    // Binds the listening socket and launches the accept-loop thread.
    // Returns false when the port is already in use or Winsock init fails.
    bool start();

    // Stops accepting, closes open connections, joins all threads. Idempotent:
    // a second call (or a call before any successful start) returns immediately.
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    int port() const { return port_; }

    // Updates the configured port. Only meaningful while stopped (start() binds it).
    void setPort(int newPort) { if (!isRunning()) port_ = newPort; }

    // Atomically replaces the served payload and bumps the monotonically
    // increasing version stamp that polling tablets compare against (§B5).
    void setPayload(const std::string& json);

private:
    static constexpr int kMaxConcurrentConnections = 8;   // plan §10 cap
    static const char* kEmptyDocumentJson;                // served before first analysis

    static void acceptLoop(TabletReportServer* self);     // runs on its own thread
    static void handleConnection(SOCKET clientSock, TabletReportServer* self);   // slot guard + exception catch-all
    static void handleConnectionInner(SOCKET clientSock, TabletReportServer* self);  // actual request handling

    int port_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    bool wsaHeld_ = false;   // set in start(), cleared by the first stop() that runs — plain bool: only touched on the UI thread
    std::atomic<int> activeHandlers_{0};   // bounded to kMaxConcurrentConnections
    std::vector<std::thread> handlerThreads_; // track handlers for graceful join
    std::mutex threadsMutex_;                // guards handlerThreads_

    mutable std::mutex payloadMutex_;      // guards payload_, version_ (read by handlers)
    std::string payload_;                  // current JSON document, empty doc at startup
    long long version_ = 1;                // bumped on every setPayload() call
};

#endif // HTTPSERVER_H
