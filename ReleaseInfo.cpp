//---------------------------------------------------------------------------
// ReleaseInfo.cpp - Implementation du fetch WinHTTP + parse JSON GitHub.
// Voir ReleaseInfo.h pour le contrat (fetch async, repo prive via credentials
// Windows, cache INI gere par la form).
//---------------------------------------------------------------------------

// Pas de vcl.h ici : ce fichier n'utilise que WinHTTP + std (pas de types VCL).
// L'ordre winsock2 -> windows -> winhttp est garanti par ReleaseInfo.h.
#pragma hdrstop

#include "ReleaseInfo.h"   // winsock2/windows/winhttp + pragma comment(lib, winhttp)
#include "RuleConfig.h"    // JsonParser / JsonValue

namespace {

std::wstring toWideUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    while (!w.empty() && w.back() == L'\0') w.pop_back();   // le -1 incluait l'NUL final
    return w;
}

// GET https://api.github.com/repos/<owner>/<repo>/releases/latest via WinHTTP.
// Repo prive sans token : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY delegue les
// credentials Windows du compte connecte (memes mecanismes que git push ici).
bool httpGetLatestRelease(const std::string& owner, const std::string& repo,
                          int timeoutMs, AppRelease& out) {
    if (owner.empty() || repo.empty()) return false;

    const std::wstring ua = toWideUtf8(std::string("MotecDataAnalyser/") + kAppVersion + " (+release-check)");
    HINTERNET session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) return false;

    bool ok = false;
    do {
        std::wstring wPath = toWideUtf8("/repos/" + owner + "/" + repo + "/releases/latest");
        HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) break;

        // lpszReferrer / acceptTypes : NULL (equivalents de WINHTTP_NO_REFERER /
        // WINHTTP_DEFAULT_ACCEPT_TYPES) — le header winhttp.h n'est pas inclus.
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", wPath.c_str(), NULL,
                                               NULL, NULL, WINHTTP_FLAG_SECURE);
        if (!request) { WinHttpCloseHandle(connect); break; }

        // Timeouts reels (ms) : resolve / connect / send / receive.
        const DWORD t = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : 8000u;
        WinHttpSetTimeouts(request, t, t, t, t);

        std::wstring headers = L"Accept: application/vnd.github+json\r\n";
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), NULL, 0, 0, 0)) {
            WinHttpCloseHandle(request); WinHttpCloseHandle(connect); break;
        }

        DWORD status = 404; DWORD sz = sizeof(status);
        if (!WinHttpReceiveResponse(request, NULL) || !WinHttpQueryHeaders(
                request, RELEASEINFO_QRY_STATUS,   // STATUS_CODE | FLAG_NUMBER
                NULL /*WINHTTP_HEADER_NAME_BY_INDEX*/, &status, &sz, NULL)) {
            WinHttpCloseHandle(request); WinHttpCloseHandle(connect); break;
        }

        if (status < 200 || status >= 300) {   // 401/403 = pas d'acces au repo prive ; 404 = aucun release
            WinHttpCloseHandle(request); WinHttpCloseHandle(connect); break;
        }

        std::string body;
        char buf[8192];
        for (;;) {
            DWORD bytes = 0;
            if (!WinHttpReadData(request, buf, sizeof(buf), &bytes) || bytes == 0) break;
            body.append(buf, bytes);
        }

        ok = parseLatestReleaseJson(body, out);   // defini plus bas dans ce fichier
    } while (false);

    WinHttpCloseHandle(session);   // ferme egalement les handles connect/request enfants.
    return ok;
}

const char* kMonthsFr[12] = { "janv.", "fevr.", "mars", "avr.", "mai", "juin",
                              "juil.", "aout", "sept.", "oct.", "nov.", "dec." };
// NB : sans accents volontairement (litteraux CP1252 bcc32c, style du projet).

const char* kMonthsEn[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

} // namespace

// ===============================
bool parseLatestReleaseJson(const std::string& json, AppRelease& out) {
    JsonValue root;
    std::string err;
    if (!JsonParser::parse(json, root, err)) return false;

    const JsonValue* tag = root.find("tag_name");
    if (!tag || tag->type != JsonValue::String || tag->str.empty()) return false;

    out.tagName = tag->str;
    if (const JsonValue* v = root.find("html_url"))     if (v->type == JsonValue::String)  out.htmlUrl = v->str;
    if (const JsonValue* v = root.find("published_at")) if (v->type == JsonValue::String)  out.publishedAt = v->str;
    if (const JsonValue* v = root.find("body"))         if (v->type == JsonValue::String)  out.body = v->str;
    return true;
}

// ===============================
std::string formatReleaseDate(const std::string& isoUtc, bool french) {
    // "2026-10-24T18:30:00Z" -> jour + mois abrege + annee. Le reste est ignore.
    if (isoUtc.size() < 10) return isoUtc;   // pas assez long pour YYYY-MM-DD
    int year = 0, month = 0, day = 0;
    for (int i = 0; i < 4 && isoUtc[i] >= '0' && isoUtc[i] <= '9'; ++i)
        year = year * 10 + (isoUtc[i] - '0');
    if (isoUtc[4] != '-') return isoUtc;
    for (int i = 5; i < 7 && isoUtc[i] >= '0' && isoUtc[i] <= '9'; ++i)
        month = month * 10 + (isoUtc[i] - '0');
    if (isoUtc[7] != '-') return isoUtc;
    for (int i = 8; i < 10 && isoUtc[i] >= '0' && isoUtc[i] <= '9'; ++i)
        day = day * 10 + (isoUtc[i] - '0');
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31) return isoUtc;

    const char* mname = french ? kMonthsFr[month - 1] : kMonthsEn[month - 1];
    std::string s;
    if (french) {
        // "24 oct. 2026"
        s.append(std::to_string(day)).append(" ").append(mname).append(" ")
           .append(std::to_string(year));
    } else {
        // "Oct 24, 2026"
        s.append(mname).append(" ").append(std::to_string(day))
          .append(", ").append(std::to_string(year));
    }
    return s;
}

// ===============================
bool fetchLatestReleaseSync(const std::string& owner, const std::string& repo,
                            AppRelease& out, int timeoutMs) {
    // Le host est code en dur dans httpGetLatestRelease (api.github.com).
    return httpGetLatestRelease(owner, repo, timeoutMs, out);
}

// ===============================
// État global du module : le worker écrit ici, la form lit via takePendingRelease().
// Statiques de portée fichier -> vivent toute la durée du processus ; un thread
// détaché ne peut donc jamais référencer de mémoire libérée (aucun objet VCL).
namespace {
    std::mutex   g_releaseMutex;
    AppRelease   g_latest{};
    bool         g_hasPending = false;
}

void fetchLatestReleaseAsync(const std::string& owner, const std::string& repo) {
    try {
        std::thread([owner, repo]() mutable {
            AppRelease rel;
            if (fetchLatestReleaseSync(owner, repo, rel)) {
                std::lock_guard<std::mutex> lk(g_releaseMutex);
                g_latest = std::move(rel);
                g_hasPending = true;   // la form viendra le récupérer via takePendingRelease()
            }
        }).detach();
    } catch (...) { /* thread creation failed : on reste sur la version locale */ }
}

bool takePendingRelease(AppRelease& out) {
    std::lock_guard<std::mutex> lk(g_releaseMutex);
    if (!g_hasPending) return false;
    g_hasPending = false;
    out = g_latest;   // copie : petit struct de 4 strings, une fois par session
    return true;
}
