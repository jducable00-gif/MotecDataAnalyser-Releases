//---------------------------------------------------------------------------
// HttpServer.cpp — Minimal embedded HTTP server for the Tablet Report Viewer.
// Implementation of Phase B (docs/TABLET_REPORT_VIEWER_PLAN.md §B1/§B2).
//
// Hand-rolled Winsock, GET-only, thread-per-request:
//   - accept-loop on its own std::thread gated by running_
//   - one detached handler thread per connection, capped at 8 concurrent
//   - only the first request line is parsed (no body handling)
//   - routes: "/" -> placeholder HTML (Phase C swaps in the real page),
//             "/api/report" -> stored JSON payload (always 200; initial state
//             is a valid empty document per plan §7), anything else -> 404
//   - every response carries "Connection: close"; no keep-alive.
//---------------------------------------------------------------------------

// Winsock 2 must be included BEFORE any windows header in this translation unit.
// This file is compiled standalone by the Gate-B test (no PCH), and inside the VCL
// project it comes before vcl.h's <windows.h>, so winsock2.h always wins first.
#include "HttpServer.h"
#include "TabletReportPage.h"    // Phase C: self-contained tablet HTML page

#include <cstdio>                // serverLog() diagnostics (fopen_s/fprintf)
#include <ctime>                 // timestamps in the diagnostic log

#pragma comment(lib, "ws2_32")   // socket/accept/send — verify at link time

// ---------------------------------------------------------------------------
// Winsock lifecycle. A process-wide refcount lets start/stop be cycled safely
// (Gate B requires repeated cycles without leaks or hangs).
// ---------------------------------------------------------------------------
static std::atomic<int> g_wsaRefCount{0};   // process-wide Winsock refcount (start/stop cycles)

// Bumps the refcount; performs WSAStartup only when we are the first instance.
static bool ensureWsa() {
    int prev = g_wsaRefCount.fetch_add(1);
    if (prev == 0) {
        WSADATA wsaData;
        if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            g_wsaRefCount.store(0);   // roll back — nobody else is using it yet
            return false;
        }
    }
    return true;
}

// Drops the refcount; performs WSACleanup only when we are the last instance.
static void releaseWsa() {
    int old = g_wsaRefCount.fetch_sub(1);   // value BEFORE our decrement
    if (old <= 1) ::WSACleanup();           // we removed the final reference
}

// Static member definition (declared in HttpServer.h). Served when no payload set yet.
const char* TabletReportServer::kEmptyDocumentJson =
    "{\"version\":0,\"meta\":{\"hasData\":false},"
    "\"sections\":{},\"lapStats\":null}";

namespace {

// One-line placeholder page. Phase C6 replaces this with the real tablet HTML;
// serving a stub here is explicitly allowed by Gate B.
const char* kPlaceholderHtml =
    "<!DOCTYPE html>\n"
    "<html><head><meta charset=\"utf-8\">"
    "<title>Motec Data Analyser - Tablet Report</title></head>"
    "<body style='font-family:sans-serif;padding:2rem'>"
    "<h1>Tablet Report Viewer</h1>"
    "<p>Server is running. The report page will be available in the next build.</p>"
    "</body></html>\n";

std::string makeResponse(const char* statusLine, const std::string& contentType,
                         const std::string& body) {
    // Content-Length must count BYTES (payload is UTF-8), not chars.
    char lenBuf[32];
    snprintf(lenBuf, sizeof(lenBuf), "%zu", body.size());
    return std::string("HTTP/1.1 ") + statusLine + "\r\n"
         "Content-Type: " + contentType + "\r\n"
         "Cache-Control: no-store\r\n"
         "Connection: close\r\n"
         "Content-Length: " + lenBuf + "\r\n"
         "\r\n" + body;
}

bool sendAll(SOCKET sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        // Winsock: pass 0 for flags — no SIGPIPE on Windows, so MSG_NOSIGNAL is not needed.
        int n = ::send(sock, data.data() + static_cast<int>(sent),
                       static_cast<int>(data.size() - sent), 0);
        if (n <= 0) return false;   // client went away — nothing to do
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Reads all headers until CRLFCRLF or limit, ensuring the receive buffer is drained
// to prevent TCP RST when closing the socket.
std::string readAllHeaders(SOCKET sock) {
    std::string data;
    char buf[1024];
    int n;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0) {
        data.append(buf, n);
        if (data.find("\r\n\r\n") != std::string::npos) break;
        if (data.size() > 8192) break; // garbage — bail out
    }
    return data;
}

// Splits "GET /path HTTP/1.1" into method + path. Returns false on malformed input.
bool parseRequestLine(const std::string& line, std::string& method, std::string& path) {
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    method = line.substr(0, sp1);
    path   = line.substr(sp1 + 1, sp2 - sp1 - 1);
    // Strip any query string: "/api/report?v=3" -> "/api/report".
    size_t qm = path.find('?');
    if (qm != std::string::npos) path.erase(qm);
    return !method.empty() && !path.empty();
}

void closeSocket(SOCKET& sock) {
    if (sock != INVALID_SOCKET) {
        ::closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

// Best-effort diagnostic log next to the exe — used only on handler failure, so a
// missing/unwritable file must never affect serving. Anchored to the module dir so it
// lands beside Project1.exe no matter what working directory launched the app.
void serverLog(const std::string& msg) {
    char exePath[MAX_PATH] = {};
    if (!::GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return;
    char* slash = strrchr(exePath, '\\');                       // strip filename -> dir\ (keep the backslash)
    if (slash && slash != exePath) *++slash = '\0';
    std::string full = std::string(exePath) + "tablet_server.log";
    FILE* f = nullptr;
    fopen_s(&f, full.c_str(), "a");
    if (!f) return;
    char ts[32];
    time_t t = time(nullptr);
    struct tm tmv{};
    localtime_s(&tmv, &t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(f, "[%s] %s\n", ts, msg.c_str());
    fclose(f);
}

// RAII: releases one handler slot on EVERY exit path (normal return or exception).
struct HandlerSlotGuard {
    std::atomic<int>& counter;
    explicit HandlerSlotGuard(std::atomic<int>& c) : counter(c) {}
    ~HandlerSlotGuard() { counter.fetch_sub(1); }
};
} // anonymous namespace — free helpers above; class members below need private access.
// ---------------------------------------------------------------------------
// Per-connection handler. Runs on a detached thread, bounded by the caller's
// activeHandlers_ counter so at most kMaxConcurrentConnections run at once.
// ---------------------------------------------------------------------------

void TabletReportServer::handleConnection(SOCKET clientSock, TabletReportServer* self) {
    // Releases the slot taken by acceptLoop() on every exit path. Without this the
    // counter only ever grew: after 8 connections per process lifetime EVERY new
    // connection was dropped at capacity -> instant RST (tablet ERR_CONNECTION_RESET).
    HandlerSlotGuard guard(self->activeHandlers_);

    try {
        handleConnectionInner(clientSock, self);
    } catch (const std::exception& e) {
        serverLog(std::string("handler exception: ") + e.what());
    } catch (...) {
        serverLog("handler unknown exception");
    }
}

void TabletReportServer::handleConnectionInner(SOCKET clientSock, TabletReportServer* self) {
    // Bound request time: tablets poll every ~3 s; anything slower is noise.
    DWORD timeoutMs = 5000;
    ::setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    ::setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    std::string method, path;
    bool ok = parseRequestLine(readAllHeaders(clientSock), method, path);

    if (!ok) {
        sendAll(clientSock, makeResponse("400 Bad Request", "text/plain; charset=utf-8",
                                         "Bad request\r\n"));
    } else if (method != "GET") {
        // GET-only server: no body parsing for other verbs.
        sendAll(clientSock, makeResponse("405 Method Not Allowed", "text/plain; charset=utf-8",
                                         "Method not allowed\r\n"));
    } else if (path == "/api/report") {
        std::string payload;
        {
            std::lock_guard<std::mutex> lock(self->payloadMutex_);
            // Copy under the mutex: setPayload() may swap it from the UI thread.
            payload = self->payload_.empty() ? kEmptyDocumentJson : self->payload_;
        }
        sendAll(clientSock, makeResponse("200 OK", "application/json; charset=utf-8", payload));
    } else if (path == "/" || path == "/index.html") {
        // Phase C: serve the self-contained tablet report page.
        sendAll(clientSock, makeResponse("200 OK", "text/html; charset=utf-8", tabletIndexHtml()));
    } else {
        sendAll(clientSock, makeResponse("404 Not Found", "text/plain; charset=utf-8",
                                         "Not found\r\n"));
    }

    closeSocket(clientSock);
}


// ---------------------------------------------------------------------------
// Accept loop. Runs on its own thread; polls running_ every <=500 ms so stop()
// is prompt without a dedicated wake-up mechanism.
// ---------------------------------------------------------------------------
void TabletReportServer::acceptLoop(TabletReportServer* self) {
    while (self->running_.load(std::memory_order_acquire)) {
        // 500 ms timeout so the loop re-checks running_ promptly on stop().
        DWORD timeoutMs = 500;
        ::setsockopt(self->listenSocket_, SOL_SOCKET, SO_RCVTIMEO,
                     (const char*)&timeoutMs, sizeof(timeoutMs));

        SOCKET clientSock = ::accept(self->listenSocket_, nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (!self->running_.load(std::memory_order_acquire)) break;  // stopping — expected
            if (err != WSAETIMEDOUT && err != WSAECONNABORTED) continue; // transient, keep looping
        } else {
            int active = self->activeHandlers_.fetch_add(1);
            if (active >= TabletReportServer::kMaxConcurrentConnections) {
                ::closesocket(clientSock);   // at capacity — drop the connection
                self->activeHandlers_.fetch_sub(1);
            } else {
                std::thread(handleConnection, clientSock, self).detach();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// TabletReportServer public API
// ---------------------------------------------------------------------------
TabletReportServer::TabletReportServer(int port) : port_(port), payload_() {
    // Payload starts empty; handlers serve kEmptyDocumentJson until the first setPayload().
}

TabletReportServer::~TabletReportServer() {
    stop();
}

bool TabletReportServer::start() {
    if (running_.load(std::memory_order_acquire)) return true;   // already up — idempotent

    if (!ensureWsa()) return false;   // WSAStartup failed — no reference taken to roll back later

    wsaHeld_ = true;                  // from here on we own one WSA ref until stop() releases it (exactly once)

    listenSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int reuseAddr = 1;   // allow quick re-bind after stop() in tests / restarts
    sockaddr_in addr{};
    bool ok = (listenSocket_ != INVALID_SOCKET) &&
              (::setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
                            (const char*)&reuseAddr, sizeof(reuseAddr)) == 0);
    if (ok) {
        // Bind to all interfaces so tablets on the LAN can reach us.
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(static_cast<u_short>(port_));
        ok = (::bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) &&
             (::listen(listenSocket_, 128) != SOCKET_ERROR);   // backlog: Windows' SOMAXCONN is only ~5 — a burst of parallel requests (browser preconnect, tablet + PC at once) would overflow it and the OS RSTs the excess. 128 keeps them queued until a handler slot frees up.
    }

    if (!ok) {
        closeSocket(listenSocket_);      // port in use / invalid — report failure to caller. wsaHeld_ stays true: stop() releases it exactly once.
        return false;                    // (stop() is idempotent and will clean up via the CAS below)
    }

    running_.store(true, std::memory_order_release);
    acceptThread_ = std::thread(acceptLoop, this);   // joinable — joined in stop()
    return true;
}

void TabletReportServer::stop() {
    bool wasRunning = running_.exchange(false);   // accept loop re-checks this and exits within 500 ms

    closeSocket(listenSocket_);      // idempotent: also clears a half-failed start's socket (if any)

    if (!wasRunning && !wsaHeld_) return;   // never started, no ref held -> pure no-op on repeat calls

    if (acceptThread_.joinable()) {
        acceptThread_.join();            // bounded: the loop polls running_ every <=500 ms
    }

    if (wsaHeld_) { wsaHeld_ = false; releaseWsa(); }   // last instance down -> WSACleanup inside here, exactly once per start()
}

void TabletReportServer::setPayload(const std::string& json) {
    std::lock_guard<std::mutex> lock(payloadMutex_);   // handlers read under this mutex too
    payload_ = json;
    ++version_;                                        // internal stamp (JSON carries its own version field)
}
