# Tablet Session Report Viewer — Implementation Plan (v3)

> **Status:** Planning. Not yet implemented.
> **Supersedes** v2 and the earlier in-chat sketch; this file is the source of truth for scope, data flow, and steps.
> **Toolchain/build notes:** see [`.ai/skills/AGENTS.md`](../.ai/skills/AGENTS.md). Architecture context: [`agents.md`](../agents.md) at repo root.

---

## 1. Goal & Decision Summary

Replicate a completed session analysis on a tablet as a **web page served by the desktop app over LAN** — not a native mobile app, and not a manually-exported file. The server is **always-on when enabled**; every time new analysis completes in the app, connected browsers are automatically updated with no manual "Publish" step.

| Question | Answer | Why (grounded in this codebase) |
|----------|--------|---------------------------------|
| Web page vs native app? | **Web page.** | Analysis output is already reduced to compact aggregates (`ReportResult` text + one averaged `LapStats`: ~70 min/max/avg/count fields, four 41-bin damper histograms). No raw per-sample traces are retained. The desktop charts (tyre-temp heat cells, brake bars, damper velocity histograms) are simple bar/histogram renderings trivially reproduced in HTML5 canvas/CSS — no capability forces a native app. |
| Live vs post-session? | **Post-session only.** | Per user requirement; avoids streaming complexity during capture. |
| Connectivity? | **LAN only** (no internet needed). | User confirmed LAN is fine, WLAN not required. No HTTPS/auth on a trusted local network. |
| What to show? | **Text findings/recommendations AND visual charts.** | Both are part of "the result". Text comes from `ReportResult`; charts come from the numeric fields in `LapStats`. |
| How does data reach the tablet? | **Auto-push on every analysis; client polls for updates.** No manual publish button. Server runs continuously while enabled (started at app launch); each completed analysis atomically swaps the JSON payload; connected browsers poll `/api/report` every ~3 s and re-render when a version stamp changes. | Simplest reliable model with our minimal GET-only Winsock server: no SSE/WebSocket protocol complexity, negligible LAN load for 1–2 tablets polling at 0.3 req/s. |

---

## 2. Architecture Overview (Auto-Publish Model)

```
Desktop C++ Builder VCL App (MotecDataAnalyser.exe)
│
├── Settings form (TFormMotecSettings): "Tablet Viewer" tab
│     • Enable/Disable toggle   -> [WebServer] Enabled = 0|1      (main INI, default 0)
│     • Port edit               -> [WebServer] Port    = <int>     (main INI, default 8765)
│       persisted via TIniFile; changes take effect immediately (start/stop server live)
│
├── App launch: if WebServer.Enabled == 1 → start HTTP server on 0.0.0.0:<port>
│     • Status bar / small label shows: "Tablet viewer active — http://<LAN-IP>:<port>"
│     • Server serves (GET-only, thread-per-request):
│         GET /            → index.html   (self-contained page; CSS+JS inlined)
│         GET /api/report  → current JSON payload { version, meta, sections{...}, lapStats{...} }
│       Payload starts as an "empty" document (hasData=false); updated on every analysis.
│
├── Every time analysis completes (file import OR live-capture stop):
│     └── Automatically: buildReportJson(result, avgLap) → server.setPayload(newJson)
│         • No user action required — the swap is atomic (mutex-protected string replace).
│         • The `version` field in JSON increments so polling clients can detect change.
│
├── Tablet browser (any modern mobile Safari/Chrome, same LAN subnet)
│   ├── Loads index.html → fetches /api/report on load
│   ├── Polls /api/report every ~3 s via setInterval; compares `version` to last-seen:
│     • If changed → re-render sections + charts from new payload (smooth DOM update).
│     • If unchanged → no-op (minimal CPU/battery impact).
│   └── Renders:
│         • 7 text section cards with severity/recommendation styling
│         • Tyre temps — 4×3 heat map, °C per cell, colour scale ~60→120°C
│         • Brake temps — 4 corner bars/cells with avg/max labels (~200–600°C)
│         • Damper histograms — 4× canvas bar charts (41 bins + mean line & ±1σ band), shared Y-scale
```

**Key properties:**
- **Zero manual steps for the user**: enable once in Settings → server runs; analyze files normally → tablet updates automatically. No "Publish" button, no "Stop Publishing".
- When a section has no numeric data (e.g., before any analysis), its chart is hidden and only text shows — graceful degradation (§7).
- Disabling the toggle in Settings stops the server immediately (clean thread join); re-enabling restarts it. App exit always stops it.

---

## 3. JSON Payload Schema (`GET /api/report`)

Produced by a new writer (Phase A). Only fields needed for charts are serialized; any field whose `count == 0` / not valid is omitted so the tablet can hide that chart.

```jsonc
{
  "meta": {
    "carName": "...",            // from session metadata if available, else ""
    "trackName": "...",          // same
    "lapCountIncluded": 12,      // LapSelectionInfo.includedLaps.size() (0 when none)
    "hasData": true              // false → tablet shows a prominent text notice + sections only
  },

  // The 7 ReportSection entries — pre-formatted localized prose from the desktop.
  // Keys use the enum names; content is already EN or FR per the app's selected language.
  "sections": {
    "Summary":       {"content":"...","hasRecommendation":false},
    "General":       {"content":"...","hasRecommendation":true },
    "Tyre":          {...},
    "Brake":         {...},
    "Suspension":    {...},      // includes damper recommendations text (getDamperHistogram/pickDamperHistogram output)
    "EngineCooling": {...},
    "Handling":      {...}
  },

  // Numeric aggregates for chart rendering. Omit any sub-object whose source count==0.
  "lapStats": {
    "tyreTempsAvg": [85.2, ...],   // 12 doubles: FL_i,c,o FR_... RL_... RR_... (avg per cell)
    "tyreTempsMin": [...],          // optional 12-doubles, omit if not valid
    "tyreTempsMax": [...],

    "brakeTempAvg": [350.0, ...],   // 4 doubles: FL FR RL RR
    "brakeTempMax": [420.0, ...],

    "damperHistograms": {           // one per corner; omit a corner if its histogram totalCount==0
      "LF": {"bins":[ /*41 ints*/ ],"mean":-5.3,"stdDev":87.2},
      "RF": {...},
      "RL": {...},
      "RR": {...}
    }
  }
}
```

Notes:
- `hasData` is the single flag that tells the tablet whether to show charts at all vs a text-only notice (§7).
- UTF-8 throughout; French accented characters must survive round-trip (writer escapes non-printables, keeps valid UTF-8 bytes intact — verify in §9 step 2/3).

---

## 4. Phases & Steps

> **Execution order — strict sequential, one phase at a time.** Implement the phases in order (A → B → C → D → E) and do **not** start work on a later phase until every step of all prior phases is complete AND that phase's gate (the bold line under each table below) passes. A gate requires: clean build via `scripts\build.ps1 -Clean` (zero errors/warnings, C++ Builder 12 Clang Win32) **plus** the listed checks for that specific phase. Gates are cumulative — later phases re-exercise earlier behaviour; §8 is then run in full as a final pass after Phase E, not before it.

### Phase A — JSON serialization (desktop side)

| # | Step | File(s) | Notes / Dependencies |
|---|------|---------|---------------------|
| A1 | Add a small header-only JSON dumper: `jsonEscape(std::string)` + object/array/string/number emitters (~60 lines, no new library). Must be UTF-8 safe. | **NEW** `ReportJsonWriter.h` (header-only) | Standalone; do not reuse RuleConfig's read-only parser types. No VCL dependency — pure `std::string` in/out so it stays testable and portable across the Clang/bcc32c STL quirks noted in AGENTS.md. |
| A2 | Implement `buildReportJson(const ReportResult&, const LapStats&) -> std::string`. Serialize per §3: all 7 sections; then lapStats chart fields, omitting any with `count==0`/invalid. Set `meta.hasData = (any section has content) && (lapCountIncluded>0 || charts present)`. | **NEW** `ReportJsonWriter.h` (or split a `.cpp`) | Depends on: `LapReportGenerator.h` (`ReportResult`, `SectionData`, `ReportSection`) and `MotecSetupAnalyzer.h` (`LapStats`, damper histogram accessors e.g. `pickDamperHistogram`). Map each of the 12 tyre cells / 4 brake corners to their exact `StatSummary` members — confirm field names against `LapStats` (line ~936) when implementing; do not guess indices. |
| A3 | Dev-time sanity: log/`OutputDebugStringW` a sample payload after analyzing a file from `data\`, validate with any JSON checker before wiring the server. | dev-only | Catches schema/index mistakes early, cheaply. |

**Gate A:** clean build; A3 done — an analyzed real session produces a payload that validates as JSON matching §3 (7 section keys present, array lengths correct when present: `tyreTempsAvg`=12 / `brakeTemp*`=4 / histogram `bins`=41, French accents intact). Only then start Phase B.

### Phase B — Embedded HTTP server (desktop side)

| # | Step | File(s) | Notes / Dependencies |
|---|------|---------|---------------------|
| B1 | **NEW** `HttpServer.h/.cpp` — class `TabletReportServer`: ctor takes port; methods `start()`, `stop()`, `isRunning()`; `setPayload(const std::string&)`. Internally: Winsock listening socket bound to INADDR_ANY, an accept-loop thread (`std::thread`) gated by a `std::atomic<bool> running_`, and one detached handler thread per connection. | **NEW** `HttpServer.h/.cpp` | Link via `#pragma comment(lib,"ws2_32")`. Call `WSAStartup()` in start / `WSACleanup()` in stop (guard against double-init). Port is a parameter — never hardcoded here; the caller passes the configured value. |
| B2 | Request handling: parse only the first request line. Route `/` → HTML string from Phase C with `Content-Type: text/html; charset=utf-8`; `/api/report` → stored payload (always 200 — initial state is a valid empty document, §7) with `application/json`. Anything else → 404. Respond `Connection: close`, no keep-alive, GET-only (no body parsing). | **NEW** `HttpServer.cpp` | Protect the payload string + version counter with a mutex — handler threads read them while the main thread may call `setPayload()` after each analysis (§B5 / Phase E4). Cap concurrent connections (~8) to bound thread churn. Polling load is trivial: 1–2 tablets × ~0.3 req/s on LAN. |
| B3 | Register new files in the project: add `<CppCompile Include="HttpServer.cpp"><DependentOn>HttpServer.h</DependentOn></CppCompile>` and matching `<None .../>` entries, following the exact existing XML pattern (see `Project1.cbproj`, e.g. the RuleConfig/LapReportGenerator blocks around lines 207–267). | **EDIT** `Project1.cbproj` | Must match sibling entry formatting exactly or MSBuild/CB will mis-parse. Same for A's and C's new files (see their rows). |
| B4 | Wire into the main form: on app launch, if `[WebServer] Enabled==1` → call `server.start(configuredPort)` automatically; show URL in status bar (`"Tablet viewer active — http://<LAN-IP>:<port>"`). No button needed. On settings toggle change (live), start/stop the server immediately without app restart. | **EDIT** `MotecSetupForm.h/.cpp` (+ `.dfm`) — *correct name: `MotecSetupForm.*`* | Get local LAN IP via interface enumeration after Winsock is up; display plain text in status bar or a small label near existing live-capture indicators. Guard double-start by checking `isRunning()`. |
| B5 | Auto-publish on every analysis completion (file import OR live-capture stop): call `buildReportJson(result, avgLap)` → `server.setPayload(newJson)`. The JSON carries an incremented **`version`** integer so polling clients detect the change. No user action required — this is a one-line hook in the existing post-analysis code path (wherever `UpdateReportTabs()` / report generation currently finishes). | **EDIT** `MotecSetupForm.cpp`, uses `HttpServer::setPayload` + Phase A writer | Thread-safe via B2's mutex. Keeps one long-lived listener for app lifetime while enabled — no port rebind churn, no start/stop cycle per analysis. If server is not running (disabled), the call is a safe no-op (`if (!server.isRunning()) return;`). |

**Gate B:** clean build with `HttpServer.cpp` registered in `.cbproj`; on a test port: `start()` binds and serves, repeated/second `stop()` is idempotent with no thread leak or hang at exit; `GET /api/report` returns 200 + the exact payload passed to `setPayload()`. `/` may serve a one-line placeholder string until Phase C6 swaps in the real HTML. Only then start Phase C.

### Phase C — Tablet web page (static, compiled-in)

| # | Step | File(s) | Notes / Dependencies |
|---|------|---------|---------------------|
| C1 | **NEW** `TabletReportPage.h` exposing `const std::string& tabletIndexHtml()` returning a single self-contained HTML document (~300–450 lines, CSS+JS inlined). Mobile-first: one-column scroll; system font stack; no external CDN/framework (vanilla DOM + Canvas 2D) so it works fully offline once loaded over LAN. | **NEW** `TabletReportPage.h` (header-only string literal) | Compiled into the exe → no extra file beside `.exe`, consistent with the portable single-exe distribution model in `scripts\package.ps1`. JS fetches `/api/report` on load, then polls every ~3 s via `setInterval`; compares the JSON **`version`** field to last-seen — if changed, re-renders sections + charts; if unchanged, no-op. A small "last updated" timestamp in the page footer gives visual confirmation that auto-refresh is working. No manual Refresh button needed (a subtle one can remain as a fallback). |
| C2 | **Text sections** (the primary content): render each of the 7 section entries as a card — title header, recommendation/severity badge from `hasRecommendation`, body in `<p>`. Use collapsible `<details>/<summary>` for long sections so it stays scannable on tablet. Strings are already localized by the desktop; display verbatim (no client i18n). | inside C1 JS/CSS | This is what shows when there's no numeric data — must be complete and readable on its own (§7). |
| C3 | **Tyre temp heat map:** CSS grid 4 rows × 3 cols. Cell background interpolated blue(~60°C)→green(~95°C)→red(>120°C) from `tyreTempsAvg[i]`; numeric °C in each cell; corner labels as row headers. Render only if `lapStats.tyreTempsAvg` present (length 12). | inside C1 JS/CSS | Simple linear colour interpolation helper shared with brake temps. |
| C4 | **Brake temps:** 4 horizontal bars/corners, width ∝ max temp over ~200–600°C scale; label avg/max per corner. Render only if `brakeTempAvg`/`Max` present (length 4). | inside C1 JS/CSS | Canvas or CSS divs — canvas preferred for crisp bar+label control on small screens. |
| C5 | **Damper histograms:** one `<canvas>` per available corner; X = velocity bins −205..+205 mm/s (41 bars), Y = count with a shared max across all four corners for comparability; overlay mean as vertical line and ±1σ shaded band. Title each ("LF Bump/Rebound Velocity"). Sparse axis ticks (~every 5th) to avoid clutter on narrow screens. Render only the corners present in `damperHistograms`. | inside C1 JS/CSS | Canvas width = container, height ~150px. This mirrors the desktop's combined-view shared-Y behaviour (see repo memory: suspension/damper histograms). |
| C6 | Register header + reference it from B2's `/` handler. Add `<None Include="TabletReportPage.h">` to `Project1.cbproj`. | **EDIT** `HttpServer.cpp`, `Project1.cbproj` | No file I/O at runtime — the page is a string constant in the binary. |

**Gate C:** clean build; opening `http://localhost:<port>/` (desktop Chrome, mobile/tablet emulation) renders all 7 section cards plus tyre heat-map, brake bars and damper histograms from one real analyzed payload with values matching the desktop GDI+ views (§8 step 3); against the empty document it shows the §7 notice with charts hidden. Only then start Phase D.

### Phase D — Settings form: port + enable/disable (user requirement)

| # | Step | File(s) | Notes / Dependencies |
|---|------|---------|---------------------|
| D1 | Add a **new tab** `TabTablet` ("Tablet Viewer") to the settings dialog with a group box containing: an **enable checkbox/switch** (`ChkWebServerEnabled`) and a **port edit** (`EditWebPort`). Mirror the existing PMR network-tab pattern exactly (see `MotecSettingsForm.h`: `TabPmr`, `GroupPmrNetwork`, `LblPmrBindAddress`/`EditPmrBindAddress`, `LblPmrPort`/`EditPmrPort`) for layout, labels, and validation style. | **EDIT** `MotocSettingsForm.h/.cpp/.dfm` — *correct name: `MotecSettingsForm.*`* | Port edit validates an integer in 1024–65535; on invalid input revert to last valid value (same defensive pattern as the PMR port field). Checkbox is a plain VCL `TCheckBox`; no custom "flip switch" control needed — keep it consistent with existing form controls. |
| D2 | Persist both values in **`MotecDataAnalyser.ini`** under `[WebServer]`: `Enabled = 0/1`, `Port = <int>` (default 8765). Read on dialog open, write on Save — following the exact `TIniFile` pattern already used for PMR (`settings->ReadInteger("PMR","Port",7576)` / `WriteInteger(...)` in `MotecSetupForm.cpp`). | **EDIT** settings load/save path + main-form INI helpers | Keep it in the *main* UI-options INI (not thresholds.ini) since these are app/UI options, matching where PMR's port already lives. Add small getter/setter helpers next to the existing `getPmrPort`/`setPmrPort`-style functions so both the settings dialog and the main form read one source of truth. |
| D3 | **Performance rationale for the toggle (why it makes sense):** when *disabled*, no socket is created, no accept-loop thread exists → zero steady-state cost; this is what a user who never uses tablets gets by default-off or explicit off. When *enabled*, one lightweight accept-loop thread + per-connection handler threads exist for app lifetime — negligible overhead on LAN (1–2 tablets polling at ~0.3 req/s). The toggle gates whether the listener exists at all; there is no separate "publishing" sub-state in v3 since publishing is automatic and continuous while enabled. | design note — no code beyond D1/D2/B4 wiring | Document this in the tab's hint label: "When enabled, a local web server runs so connected tablets can view analysis results automatically." Default `Enabled = 0` on first run (auto-created INI) to avoid surprising a firewall prompt for users who don't need it. Toggling live (without app restart) starts/stops the server immediately via B4 wiring. |

**Gate D:** clean build; new "Tablet Viewer" tab loads and saves `[WebServer] Enabled`/`Port`; values survive an app relaunch from `MotecDataAnalyser.ini`; toggling in Settings live-starts/live-stops the running server (status indicator follows state) without restarting the app. Only then start Phase E.

### Phase E — Main-form wiring & lifecycle polish

| # | Step | File(s) | Notes / Dependencies |
|---|------|---------|---------------------|
| E1 | Status indicator: when server is running, show a small label or status-bar segment ("Tablet viewer active — http://<IP>:<port>"). When disabled/stoppped, hide it. No button to manage the lifecycle — that's handled by the Settings toggle (§D). If user disables in settings while viewing on tablet, browser gets connection-refused gracefully; re-enabling restores service without app restart. | **EDIT** `MotecSetupForm.cpp/.dfm` | Re-evaluate indicator visibility after any analysis/settings change and at form show. |
| E2 | Graceful shutdown: on form close / app exit call `server.stop()` → set `running_=false`, close listening socket, join handler threads with a short timeout; ensure no crash if the tablet still has the page open (browser just gets connection-refused — acceptable). Also stop when user disables via Settings toggle mid-session. | **EDIT** `MotecSetupForm.cpp` destructor/OnClose + settings-change handler + `HttpServer::stop()` | Use the atomic flag checked in both accept loop and handlers to break out cleanly. No dangling threads at exit. |
| E3 | Firewall note: first time server starts (app launch with Enabled=1, or re-enabling after a port change) triggers a one-time Windows Defender prompt for inbound on the configured port — add an info hint in the Settings tab and a short README section ("Tablet Viewer"). Do **not** auto-modify firewall rules (overkill; one prompt is fine). | **EDIT** `README.md` (+ optional `.dfm` hint) | Port being user-configurable means the allowed-port may change → re-prompt on port change is expected and acceptable. |
| E4 | Auto-publish hook: in the post-analysis code path (wherever report generation finishes — after `LapReportGenerator::generate*()` populates `ReportResult`), add a call to build JSON + push payload if server is running. This must fire for **both** file-import analysis and live-capture stop paths so tablets always get fresh data regardless of input source. | **EDIT** `MotecSetupForm.cpp` (post-analysis completion point) | One-line guard: `if (_tabletServer.isRunning()) _tabletServer.setPayload(buildReportJson(result, avgLap));`. No UI feedback needed beyond the status indicator already showing "active" — the tablet itself is the confirmation. |

**Gate E (final):** clean build; full end-to-end pass per §8 with a real second device on LAN: auto-publish from **both** input paths lands on the open tablet page within one ~3 s poll cycle, no manual refresh anywhere in the flow; settings persistence and all edge cases (§7/§8) verified. The feature is complete only when gates A–E have each passed.

---

## 5. New Files Summary

| File | Purpose |
|------|---------|
| **NEW** `ReportJsonWriter.h` (header-only, or +`.cpp`) | JSON dumper for `ReportResult` + chart fields of `LapStats` → UTF-8 `std::string` (§3 schema) |
| **NEW** `HttpServer.h` / `HttpServer.cpp` | Minimal Winsock GET-only HTTP server: `/` and `/api/report`, thread-per-request, atomic stop; port passed in (never hardcoded) |
| **NEW** `TabletReportPage.h` | Self-contained HTML/CSS/JS string for the tablet viewer page (text sections + 3 chart types), compiled into exe |

## 6. Files to Modify

| File | Change |
|------|--------|
| `MotecSettingsForm.{h,cpp,dfm}` | New "Tablet Viewer" tab: enable checkbox + port edit; load/save via TIniFile `[WebServer]` (mirror PMR network-tab pattern) — **Phase D** |
| `MotocSetupForm.{h,cpp,dfm}` *(= `MotecSetupForm.*`)* | Auto-start server at launch when enabled; auto-push payload on every analysis completion (file import + live-capture stop); status indicator label; settings-toggle → start/stop wiring; clean shutdown — **Phases B4/B5/E1–E4** |
| `Project1.cbproj` | `<CppCompile>`/`<None>` entries for the 3 new files (match existing XML pattern) + ensure `ws2_32.lib` linked via pragma in `HttpServer.cpp` — **Phase B3/C6** |

---

## 7. "No Analysis Yet" Behaviour

Since the page is always reachable while the server runs, it must handle the state before any analysis has completed:

- On first launch with `Enabled=1`, the payload starts as a valid empty document: `{ version:0, meta:{hasData:false,...}, sections:{} }`.
- The tablet renders a prominent notice ("No session data yet — analyze a file on the PC and this page will update automatically") **plus** whatever text is present in `sections`. Charts are hidden because their source arrays are absent (§3 omission rule).
- Once any analysis completes, B5/E4 pushes new JSON with incremented version; the tablet's 3-second poll picks it up within ~3 s and re-renders full content. No manual refresh needed.
- Rationale: text-first rendering means an empty session degrades gracefully rather than erroring out; auto-refresh means the transition from "empty" to "full data" is seamless for the tablet user.

---

## 8. Verification Plan

1. **Build:** `.\scripts\build.ps1 -Clean` → zero errors/warnings under C++ Builder 12 Clang Win32.
2. **JSON correctness:** enable the viewer and analyze a sample from `data\`; once auto-published, inspect `/api/report` in tablet (or desktop Chrome mobile-emulation) DevTools: valid JSON; all 7 sections present with correct keys; array lengths right (`tyreTempsAvg`=12 when present, `brakeTemp*`=4, each histogram `bins`=41); French accents intact as UTF-8.
3. **Visual parity vs desktop GDI+ paint boxes** for the same session: tyre heat-map colours/values match; damper histograms show plausible distributions with visible mean line + σ band on a shared Y-scale.
4. **Settings persistence:** set port to e.g. 9012 and disable in Settings → Save → relaunch app → values retained from `MotecDataAnalyser.ini` `[WebServer]`; server auto-starts on the configured (non-default) port when enabled; status indicator reflects state.
5. **Auto-publish flow:** with tablet page open, analyze a file on the PC → within ~3 s the tablet re-renders new sections + charts without any user action (`version` change detected by poll). Repeat with a second file / live-capture stop to confirm both input paths trigger updates (E4).
6. **Edge cases:**
   - Server enabled, nothing analyzed yet → tablet shows text notice, no charts, no crash (§7); first analysis auto-populates.
   - Disable in Settings while tablets connected → server stops immediately; `stop()` joins threads cleanly; browser gets connection-refused gracefully. Re-enable → service resumes without app restart.
   - Close app mid-view → clean exit (no hang/crash).
7. **Firewall:** first server start (launch with `Enabled=1`) prompts Windows Firewall for the configured port; after allowing, a tablet on the same subnet connects.

---

## 9. Scope Boundaries — Deliberately Excluded

- **No live/real-time streaming** during capture (post-session only).
- **No native mobile app** (iOS/Android) — web page suffices given aggregate-only data model and simple charts.
- **No HTTPS/authentication** — LAN-only, trusted network; adding TLS would require cert management for no real benefit here. Can be fronted later if ever needed.
- **No raw per-sample time-series traces on tablet** — `LapStats` retains only min/max/avg/count aggregates after parsing (no sample arrays). The viewer can therefore show exactly what the desktop shows today; trace-level detail would require upstream changes to retain samples, a separate feature.
- **No multi-user/concurrent-session management** beyond basic thread-per-request — designed for one analyst + their tablet(s) on LAN.

---

## 10. Decisions Log

| Decision | Rationale / Source |
|----------|--------------------|
| Web page over native app | Compact aggregate data model; charts trivially reproducible in canvas/CSS; far lower build/maintain cost than shipping iOS+Android binaries. |
| Desktop-embedded HTTP server, not file export | User wants the current app to "publish result" to whoever's connected on LAN — live serving avoids manual transfer steps. |
| Hand-written Winsock server (not cpp-httplib) | Zero external dependencies consistent with codebase style; scope is tiny (2 GET endpoints); avoids bcc32c/Clang third-party-header risk noted in AGENTS.md. |
| HTML served as compiled-in string, not disk file | No extra deployment artifact beside `.exe`; matches portable single-exe model from `scripts\package.ps1`. |
| **Port is a user option (not hardcoded)** — `[WebServer] Port` in main INI, edited via new Settings tab mirroring the PMR network-tab pattern. | User requirement; reuses existing TIniFile + settings-dialog conventions already proven for PMR bind/port. |
| **Enable/Disable toggle** gates server existence (on = listener runs for app lifetime; off = no socket). Default off on first run. Toggling live starts/stops without restart. | User requirement, justified by performance: zero cost when disabled (§D3); avoids surprise firewall prompts for non-users. |
| **Auto-publish model** — server auto-starts at launch when enabled; every analysis completion automatically pushes new JSON payload (no manual Publish button). Tablet polls `/api/report` every ~3 s and re-renders on version change. | User feedback: "Expect to be auto published (or refreshed) when new analysis is provided on the app if a web browser is connected." Polling chosen over SSE/WebSocket because our minimal GET-only Winsock server has no persistent-connection infrastructure; 0.3 req/s per tablet is negligible LAN load and avoids protocol complexity in ~250 lines of socket code. |
| **"No analysis yet" shows text + notice, does NOT block** (`meta.hasData=false`). Page always reachable while server runs. | User feedback — expects to see textual recommendations/content even before/without numeric data; charts hide gracefully via field omission. Auto-refresh makes the transition seamless. |
