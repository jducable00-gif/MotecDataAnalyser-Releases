# Live Capture — Frame Synchronisation Plan (seq / packetId change-detection)

**Status:** Implemented (commit `45c3f02`); all sections §4–§10 are in the codebase as described below.
**Scope:** `Ams2LiveCapture`, `AcsEvoLiveCapture` (+ their call site in `MotecSetupForm.cpp`). PMR UDP is out of scope (§7: no changes needed).

---

## 1. Goal

Today the live reader **polls unconditionally every 16 ms** (`LiveTimer.Interval = 16`) and processes whatever it reads, even if nothing new was written since the last tick. The `mSequenceNumber` (AMS2) / `packetId` (AC EVO) fields are used **only as a torn-read guard**, not to detect "a new frame arrived".

We want each simulator's reader to:
1. Detect that genuinely **new** data was written before processing it.
2. Skip ticks where nothing changed (no duplicate/stale work).
3. Keep the existing torn-write protection (never act on a half-written buffer).
4. For AC EVO, surface / use the **actual frame rate** so we can keep up with fast sessions.

---

## 2. Important limitation to set expectations correctly

Shared memory is a **"latest value" store**, not a ring buffer or queue: every write overwrites the previous one. Therefore frames written *between* two of our polls that were already overwritten are **not recoverable** — no amount of sequence-number logic can bring them back, because they simply don't exist anymore in the single shared block.

What change-detection on `mSequenceNumber` / `packetId` **does** guarantee:
- ✅ We never process a stale/duplicate frame (skip when unchanged).
- ✅ On every tick we always pick up the newest available value exactly once.
- ✅ We detect an in-progress write and retry next tick instead of reading torn data.

To maximise "capture as many frames as possible", pair change-detection with an **adaptive poll interval** that tracks the sim's real frame rate (see §5). That is the best achievable semantics for a single-buffer shared-memory source.

---

## 3. Update frequencies per block (reference)

| Shared memory block | Writer cadence | Change token used |
|---------------------|----------------|-------------------|
| `Local\acevo_pmf_physics` (`SPageFilePhysics`)   | Every physics tick        | `packetId` |
| `Local\acevo_pmf_graphics` (`SPageFileGraphicEvo`) | Every rendered image (≈ display FPS) — **actual rate readable via `player_fps` / `player_fps_avg`** | `packetId` |
| `Local\acevo_pmf_static`  (`SPageFileStaticEvo`)   | Once at session load      | *(no per-frame token; read once on connect)* |
| AMS2 shared memory        | Every rendered frame (≈ display FPS) | `mSequenceNumber` |

> Note: physics and graphics pages update at **different** rates. A "new live frame" for AC EVO should be considered when *either* page's `packetId` advanced, but most per-band/visual channels come from the graphics page.

---

## 4. Design — AMS2 (`Ams2LiveCapture`)

### Current behaviour
```cpp
bool Ams2ReadFrame(Ams2SharedMemory& out) {
    unsigned int seq1 = sm->mSequenceNumber;
    if (seq1 & 1) return false;              // write in progress -> skip this tick
    memcpy(&out, sm, sizeof(*sm));
    unsigned int seq2 = sm->mSequenceNumber;
    if (seq1 != seq2) return false;          // torn read -> retry next tick
    return true;                             // clean copy (may be a duplicate of last frame!)
}
```

### Proposed behaviour — add change-detection on `mSequenceNumber`
Introduce an explicit status so the caller can distinguish "no new data" from "torn, try again":

```cpp
enum class FrameReadStatus { NoNewData, TornRetry, NewFrame };

// file-static: last sequence number we actually processed
static unsigned int s_lastProcessedSeq = 0;

FrameReadStatus Ams2ReadFrame(Ams2SharedMemory& out) {
    if (g_pSharedMem == NULL) return FrameReadStatus::NoNewData;
    const auto* sm = static_cast<const Ams2SharedMemory*>(g_pSharedMem);

    unsigned int seq1 = sm->mSequenceNumber;
    if (seq1 & 1)                 return FrameReadStatus::TornRetry; // mid-write, retry next tick
    memcpy(&out, sm, sizeof(*sm));
    unsigned int seq2 = sm->mSequenceNumber;

    if (seq1 != seq2)             return FrameReadStatus::TornRetry; // torn read
    if (seq1 == s_lastProcessedSeq) return FrameReadStatus::NoNewData;// nothing new since last tick
    s_lastProcessedSeq = seq1;                          // commit: this is a fresh frame
    return FrameReadStatus::NewFrame;
}
```

- Reset `s_lastProcessedSeq` to 0 on connect / session reset so the first real frame after (re)connect always counts as new.
- Call site (`MotecSetupForm.cpp`, ~line 2590): only call `processLiveFrame(frame)` when status == `NewFrame`.

---

## 5. Design — AC EVO (`AcsEvoLiveCapture`)

### Current behaviour
`AcsEvoReadPhysics()` and `AcsEvoReadGraphics()` each do a double-check on their page's `packetId`:
```cpp
int id1 = sm->packetId;   // (no odd/even convention here — just equality)
memcpy(...);
int id2 = sm->packetId;
if (id1 != id2) return false;  // torn read only
```

### Proposed behaviour — change-detection on both `packetId`s + expose real FPS
Track the last-processed packet ID per page:

```cpp
static int s_lastPhysPacket   = -1;
static int s_lastGfxPacket    = -1;

enum class EvoReadStatus { NoNewData, TornRetry, NewFrame };

EvoReadStatus AcsEvoReadFrame(SPageFilePhysics& physOut, SPageFileGraphicEvo& gfxOut) {
    // 1. physics page (per-tick cadence)
    int p1 = smPhys->packetId;
    memcpy(&physOut, smPhys, sizeof(physOut));
    if (smPhys->packetId != p1) return EvoReadStatus::TornRetry;

    // 2. graphics page (per-rendered-image cadence)
    int g1 = smGfx->packetId;
    memcpy(&gfxOut, smGfx, sizeof(gfxOut));
    if (smGfx->packetId != g1) return EvoReadStatus::TornRetry;

    bool physNew  = (physOut.packetId != s_lastPhysPacket);
    bool gfxNew   = (gfxOut.packetId  != s_lastGfxPacket);
    if (!physNew && !gfxNew) return EvoReadStatus::NoNewData; // nothing new on either page

    s_lastPhysPacket = physOut.packetId;
    s_lastGfxPacket  = gfxOut.packetId;
    return EvoReadStatus::NewFrame;   // at least one page advanced -> process once this tick
}
```

- Reset both `s_last*` to -1 on connect / session reset.
- Call site: only call `processLiveFrameEVO(phys, gfx)` when status == `NewFrame`.

### Retrieve the actual frame rate (graphics page)
The graphics struct already carries real FPS values — no new plumbing needed in shared memory:
```cpp
int32_t fpsNow  = gfx.player_fps;      // instantaneous
int32_t fpsAvg  = gfx.player_fps_avg;  // smoothed -> prefer this for decisions
```

Use `player_fps_avg` to drive an **adaptive poll interval** so the reader keeps up with fast sessions (see §6). Also surface it in the live status line / diagnostic dump (`ac_evo_diag.txt`) as a sanity check.

### Plausibility cross-check between cycles (step 5 — torn-read hardening)
The AC EVO docs state explicitly that shared memory **can be read while the game is writing to it**, and recommend re-reading `packetId` around the copy — but this is *not* a formal guarantee against a partial/torn read. The existing double-check on `packetId` (steps 1–4) catches most cases, so we add one more layer: **compare key channels across consecutive accepted frames** and reject an implausible jump as a suspected torn/corrupt sample.

```cpp
// file-static snapshot of the last *accepted* physics frame's guard fields
static SPageFilePhysics s_lastPhys{};   // zeroed on connect/reset
static bool             s_haveLastPhys  = false;

bool AcsEvoPlausible(const SPageFilePhysics& cur, const SPageFilePhysics& prev) {
    if (!s_haveLastPhys) return true;                 // first frame: nothing to compare against
    auto saneDelta = [](float a, float b, float maxStep) {
        double d = std::abs((double)a - (double)b);   // use double for the comparison itself
        return d <= maxStep;                          // per-tick step limits below are generous on purpose
    };
    if (!saneDelta(cur.speedKmh, prev.speedKmh, 30.0f))      return false; // >30 km/h in one tick is implausible
    if (!saneDelta(cur.gas,     prev.gas,       1.5f))        return false;
    if (!saneDelta(cur.brake,   prev.brake,     1.5f))        return false;
    // tyre core temps move slowly: a >20 °C jump in one tick is almost certainly torn data
    for (int i = 0; i < 4; ++i)
        if (!saneDelta(cur.tyreCoreTemperature[i], prev.tyreCoreTemperature[i], 20.0f)) return false;
    // gear can change by more than one on a blip, but not wildly: cap at ±3
    if (std::abs((int)(cur.gear - prev.gear)) > 3)            return false;
    return true;
}
```

Integration in `AcsEvoReadFrame` (after the torn-check passes and before committing):
- If `!AcsEvoPlausible(physOut, s_lastPhys)` → treat as **suspect**: do NOT commit this frame's tokens (`s_last*Packet`) so it is re-evaluated next tick; optionally count a diagnostic counter / log to the diag dump. The *previous accepted* values remain in `LapStats` (we simply skip feeding this one).
- Only when plausible, update `s_lastPhys = physOut; s_haveLastPhys = true;`.

> **Tuning note:** thresholds are deliberately generous — they exist only to catch gross corruption (half-written floats), not to model real physics. If a legitimate high-G / hard-brake transient ever trips them, raise the limit rather than removing the check. Keep this as an *optional* guard behind a flag so it can be disabled if false-positives are observed on exotic content.

---

## 6. Adaptive poll interval (optional but recommended)

The fixed `LiveTimer.Interval = 16` is fine at ~60 fps, but if AC EVO renders faster than that we will skip intermediate frames (unavoidable per §2). To minimise skips:
- Each tick, read the current sim FPS (`player_fps_avg` for EVO; AMS2 has no direct field — fall back to measuring `mSequenceNumber` deltas over a short window if desired).
- Compute target interval = clamp(1000 / fps, min=4 ms, max=33 ms) and update `LiveTimer->Interval`.
- This keeps poll period ≤ frame time so we catch (nearly) every written frame.

> Keep the 4 ms floor to avoid a busy-loop; keep the 33 ms ceiling as a sane default when FPS is unknown/low.

---

## 7. PMR UDP — no change expected
PMR arrives over **UDP datagrams at ~60 Hz**, which are inherently discrete packets (a stream, not an overwrite buffer). Each received packet *is* new data by nature; there is no "latest value" aliasing problem and no sequence token to gate on.

**Action:** none required for frame-sync purposes. *(If the PMR protocol ever exposes a per-packet counter we could add optional drop detection, but that's out of scope now.)*

---

## 8. Implementation checklist

- [ ] `Ams2LiveCapture.h/.cpp`: introduce `FrameReadStatus`; change `Ams2ReadFrame` to return it; add file-static last-seq + reset on connect/session-reset.
- [ ] `MotecSetupForm.cpp` (AMS2 branch, ~line 2590): process only when status == `NewFrame`.
- [ ] `AcsEvoLiveCapture.h/.cpp`: introduce `EvoReadStatus`; change the combined read to return it; add per-page last-packet tracking + reset on connect/session-reset.
- [ ] `MotecSetupForm.cpp` (EVO branch): process only when status == `NewFrame`.
- [ ] *(Optional, behind a flag)* Add the inter-cycle plausibility cross-check (`AcsEvoPlausible`) to reject suspected torn frames; reset snapshot on connect/session-reset.
- [ ] Surface AC EVO real FPS (`player_fps_avg`) in live status / diag dump.
- [ ] *(Optional)* Adaptive `LiveTimer->Interval` from measured/declared FPS with 4–33 ms clamp, for both sims (AMS2 via seq-delta measurement).
- [ ] Verify no behavioural regression on lap detection / pit transitions when ticks are skipped due to "no new data".

**UI — Summary tab (§10):**

- [ ] `MotecSetupForm.h`: add session counters (`liveFrameCount`, start-time) + reset in connect/session-reset paths.
- [ ] Increment counter on every accepted **NewFrame** (both AMS2 and EVO branches).
- [ ] In report generation, compute average Hz = frames / elapsed seconds; append one line to Summary tab content (§10a format). For AC EVO also show `player_fps_avg` comparison.
- [ ] Set `PageControlReport->ActiveTab = TabSummary` at form startup (constructor/OnShow) AND after every `UpdateReportTabs()` call (§10b; direct component reference, no index assumption).

---

## 9. Edge cases & risks

| Case | Handling |
|------|----------|
| First frame after connect/reset | last-token initialised so the first real write is treated as `NewFrame`. |
| Sim paused / no writes for a while | status stays `NoNewData`; we simply do nothing (no spurious laps). Correct. |
| Very high FPS (> poll rate) → `packetId` jumps by >1 between two reads | **This means OUR reader missed intermediate writes** (poll too slow), NOT that AC EVO dropped frames internally. The sim wrote every packet into shared memory; they were simply overwritten before we polled again. Inherent to single-buffer shared memory (§2). Adaptive interval reduces the gap but cannot eliminate it. When logging/diagnosing, label this as "reader lag / missed polls", never as "sim frame drop". |
| Torn write mid-copy | existing double-check retained → `TornRetry`, re-read next tick (no data corruption). |
| Subtle torn read that passes the `packetId` check (docs: not a formal guarantee) | inter-cycle plausibility cross-check (§5 step 5) rejects gross implausible jumps; frame is skipped, previous accepted values retained. Optional/flagged so it can be disabled if false-positives appear on exotic content. |
| Legitimate hard transient trips the plausibility guard (e.g. very aggressive braking / gear blip) | thresholds are deliberately generous and tunable — raise the limit rather than removing the check; or disable via flag for that session type. |
| Sequence/packet wraps around 32-bit boundary after very long sessions | use unsigned wrap-safe comparison (`!=`), never `<`; tokens are only compared for equality so this is safe. |

---

## 10. UI — Summary tab: measured collection frequency + default selection

### 10a. Display session-average collection rate in the Summary report

In `MotecSetupForm.cpp`, during live capture, accumulate two counters (reset on connect / session-reset):

```cpp
// Form members (add to MotecSetupForm.h)
int64_t   liveFrameCount    = 0;   // total accepted frames this session
double    liveSessionTimeS  = 0.0; // wall-clock seconds since first frame of the session
```

- Increment `liveFrameCount` each time a **NewFrame** is processed (both AMS2 and EVO paths).
- Track elapsed time: store a `TDateTime startTimeEvo/Ams2` on connect; compute `(Now - start)` in seconds when generating the report.

In `generateLiveReport()` / `UpdateReportTabs()`, append to the **Summary** tab content (not per-frame, just one line):

```
Collection rate: 58.3 Hz avg over 1247 frames / 21.4 s   [target ~60 Hz]
```

- For AC EVO also show `player_fps_avg` alongside so the user can compare *sim render FPS* vs *our measured capture rate*:
  ```
  Sim FPS (AC EVO): 59 avg | Captured: 58.3 Hz → 100% of frames captured
  ```
- If ratio < ~90 %, flag it in the summary as a warning ("reader lag — consider lowering LiveTimer interval").

> **Important:** this is a *session average* (total accepted frames ÷ total elapsed time), NOT a per-frame instantaneous value. One line, computed once at report generation. No rolling window needed for v1.

### 10b. Ensure "Summary" tab is selected by default

Two moments to enforce:
1. **At application startup** — when `TFormMotecSetup` is created / shown, set the page-control (tab) index to the Summary tab explicitly in the form's constructor or `OnShow`.
2. **After every report generation** (`generateLiveReport()` → `UpdateReportTabs()`) — after populating all tabs, force-select the Summary tab so the user always lands on it:

Verified component names (from `MotecSetupForm.dfm`, line ~128–136):

- Page control: **`PageControlReport`** (`TPageControl`)
- Summary tab sheet: **`TabSummary`** (`TTabSheet`, Caption = `'Summary'`) — it is the **first** child, so `ActivePageIndex == 0`.

```cpp
// At end of UpdateReportTabs():
PageControlReport->ActiveTab = TabSummary;   // direct reference, no index assumption
```

> Direct component reference (`ActiveTab = TabSummary`) is preferred over an integer index — immune to future tab reordering. Same call in the form constructor / `OnShow` for startup default.

---

## 11. Verification plan

- Run a live AC EVO session; confirm the diag dump shows `player_fps_avg` and that processed-frame count ≈ (session seconds × fps) within tolerance, with no duplicate processing.
- **Interpreting packetId deltas:** if the graphics/physics `packetId` advances by more than 1 between two accepted reads, log it as **"reader lag / missed polls"** — i.e., our poll interval was slower than the sim's write rate and intermediate writes were overwritten before we sampled them. This is *not* evidence that AC EVO dropped frames internally; the sim published every packet into shared memory. Do not report a multi-unit jump as a "sim frame drop".
- Confirm lap detection / pit-return logic still fires correctly when some ticks are skipped as "no new data".
- Repeat for AMS2 at 60 Hz and a higher refresh rate; confirm `mSequenceNumber` gating skips unchanged polls without dropping real frames.
