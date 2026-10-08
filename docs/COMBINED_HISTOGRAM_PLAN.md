# Combined Damper Velocity Histogram — Implementation Plan

## Overview

Replace the current flat 2×2 grid of four damper TPaintBoxes with a **TPageControl** containing two tabs:

- **"Grid"** (tab index 0) — existing layout, unchanged rendering.
- **"Combined"** (tab index 1) — new single-chart view showing all four corner velocity distributions as colored line/area curves (LF red, FR blue-purple, RL magenta-pink, RR cyan-lightblue), with per-corner legend rows carrying Rebound Hi%/Lo% and Bump Hi% + Avg [mm/s] values.

Adding a future 3rd layout = just add another TTabSheet in DFM. Active tab persists to `MotecDataAnalyser.ini`.

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| TPageControl over checkbox/radio | VCL handles show/hide/repaint automatically; extensible by adding a 3rd tab with zero logic changes; consistent with existing `PageControlReport` pattern in the same form. |
| Colors fixed per corner (LF red, FR blue-purple, RL magenta-pink, RR cyan) | Matches reference screenshot; no user color config needed. |
| Header stats computed at paint time from `bins[]` + new per-corner `totalCount` | No extra data structures; Hi% = share beyond ±slowRange on that side ×100; Lo% = within it; Avg [mm/s] = mean velocity on that side. |
| Y axis: percent, auto nice ticks (~5–6) from max bin % over valid corners | Shared scale inherent since one chart. X fixed −200..+200 step 50. Slow band shaded ±`damperSlowSpeedRange`. |
| "Zoom Linked" is a static label only | No zoom/pan interactivity in this iteration. |

## Relevant Files

- `MotecSetupForm.h` — struct + new component pointers + handler/method declarations (~lines 60–95).
- `MotecSetupForm.cpp` — constructor init (~line 640), INI helpers (pattern at lines 139–157), `UpdateSuspension()` (ends ~2295), `resetAnalysisGraphics()` (~1228), new renderer + handlers near `DrawModernHistogram` (line 1100).
- `MotecSetupForm.dfm` — restructure damper region into TPageControl with two tabs; move four paintboxes inside tab 1, add combined box in tab 2. Reference: existing `PageControlReport` for DFM syntax of nested PageControls/TabSheets/PaintBoxes.
- Reuse from `MotecSetupAnalyzer.h`: `VelocityHistogram` bin geometry (line 775), `g_thresholds.damperSlowSpeedRange`, `.histogramYOffset`.

## Execution Rules

> **Each step MUST be fully completed and verified before starting the next.** Do not skip ahead.
> After each phase, run a build to confirm no compilation errors before proceeding.

---

## Phase 1 — Header: Data + API Surface (`MotecSetupForm.h`)

- [ ] **Step 1:** Extend `SuspensionCornerData` struct (line ~87): add field `std::size_t totalCount = 0;`.
- [ ] **Step 2:** Add new component pointer declarations in the private section alongside existing paintbox pointers:
      ```cpp
      TPageControl *DamperLayoutTabs;
      TPaintBox    *DamperCombinedPaintBox;
      ```
- [ ] **Step 3:** Declare event handlers and methods (private `__fastcall` for events, public for the renderer):
      - Private: `void __fastcall DamperCombinedPaintBoxPaint(TObject* Sender);`
      - Private: `void __fastcall DamperLayoutTabsChange(TObject* Sender);`
      - Public:  `void DrawCombinedDamperChart(Gdiplus::Graphics& graphics, int width, int height);`

**Gate:** Build must compile cleanly (header-only changes; no .cpp edits yet). If the project requires a full build to validate headers, run it now.

---

## Phase 2 — Data Population (`MotecSetupForm.cpp`)

- [ ] **Step 4:** In `UpdateSuspension()` (~line 2260): after selecting the active histogram per corner and filling `suspensionData[i].bins[]`, add:
      ```cpp
      suspensionData[i].totalCount = hist.totalCount;
      ```
      Keep everything else identical (fractions, shared max computation).

- [ ] **Step 5:** In `resetAnalysisGraphics()` (~line 1230): inside the existing per-corner loop that zeroes bins, add:
      ```cpp
      suspensionData[i].totalCount = 0;
      ```
      After the four-box invalidation block at the end of this function, also invalidate the combined box (guard with null check like the others).

**Gate:** Build must compile cleanly. No visual change yet — data is stored but not displayed anywhere new.

---

## Phase 3 — DFM Restructure + Tab Persistence

- [ ] **Step 6a:** In `MotecSetupForm.dfm`, create a new top-level object (sibling of the existing four paintboxes, placed just before them in file order):
      ```text
      object DamperLayoutTabs: TPageControl
        Left = 330
        Top = 184
        Width = 792
        Height = 306
        ActivePage = TabDamperGrid
        OnChange = DamperLayoutTabsChange
        TabOrder = N   // next available
      ```

- [ ] **Step 6b:** Inside `DamperLayoutTabs`, add the first tab sheet:
      ```text
      object TabDamperGrid: TTabSheet
        Caption = 'Grid'
      ```
      Move (re-parent) all four existing paintbox objects (`DamperFLPaintBox`, `DamperFRPaintBox`, `DamperRLPaintBox`, `DamperRRPaintBox`) **inside** this tab sheet. Adjust their Left/Top to be relative to the new parent:
      - FL: Left=8, Top=4, Width=391, Height=152
      - FR: Left=396, Top=4, Width=390, Height=152
      - RL: Left=8, Top=158, Width=391, Height=152
      - RR: Left=396, Top=158, Width=390, Height=152

- [ ] **Step 6c:** Inside `DamperLayoutTabs`, add the second tab sheet with its paintbox:
      ```text
      object TabDamperCombined: TTabSheet
        Caption = 'Combined'
        object DamperCombinedPaintBox: TPaintBox
          Align = alClient
          OnPaint = DamperCombinedPaintBoxPaint
        end
      end
      ```

- [ ] **Step 6d:** Remove the four original top-level paintbox objects from their old positions (they now live inside `TabDamperGrid`). Verify no orphaned DFM entries remain.

- [ ] **Step 7a:** Add INI helper functions near the existing `ReadReportLanguageSetting()` / `SaveReportLanguageSetting()` (~line 139–157):
      ```cpp
      int ReadHistogramLayoutSetting() { /* read [General] HistogramLayout, default 0 */ }
      void SaveHistogramLayoutSetting(int index) { /* write [General] HistogramLayout = index */ }
      ```

- [ ] **Step 7b:** In the constructor `TFormMotecSetup(TComponent*)` (~line 640, near where `LanguageCombo->ItemIndex` is set), add:
      ```cpp
      DamperLayoutTabs->ActivePageIndex = ReadHistogramLayoutSetting();
      ```

- [ ] **Step 7c:** Implement the event handler (can be a one-liner):
      ```cpp
      void __fastcall TFormMotecSetup::DamperLayoutTabsChange(TObject* Sender) {
          SaveHistogramLayoutSetting(DamperLayoutTabs->ActivePageIndex);
      }
      ```

**Gate:** Build must compile cleanly. Run the app: verify both tabs exist, "Grid" tab shows the four histograms exactly as before (pixel-identical), switching to "Combined" shows an empty dark box (renderer not yet implemented). Restart → active tab persists via INI.

---

## Phase 4 — Combined Chart Renderer (`MotecSetupForm.cpp`)

Implement `DrawCombinedDamperChart(Gdiplus::Graphics& graphics, int width, int height)`. This is the largest single step; sub-steps below are all part of this one function:

- [ ] **Step 8a:** Background + layout zones.
      - Fill entire area with dark charcoal `(255, 30, 30, 34)` (matching existing style).
      - Define margins: `marginLeft = 40`, `marginRight = 10`, `headerHeight = 88` px from top, `xAxisHeight = 22` px from bottom.
      - Chart area = remaining rectangle between header and x-axis labels.

- [ ] **Step 8b:** Header / legend block (top zone).
      - Row 0: left-aligned "<totalSamples> Samples" in light grey; right-aligned static text "Zoom Linked".
        Total samples = sum of `suspensionData[i].totalCount` for all valid corners.
      - Column header row: centered group captions **"Rebound"** (over the Hi%/Lo% pair) and **"Bump"** (over its Hi%), far-right column caption **"Avg [mm/s]"**. All in bold white, 9pt Segoe UI.
      - Four data rows (one per corner LF/FR/LR/RR): colored label "Damper Vel FL" etc., then numeric values: Rebound Hi%, Lo%; Bump Hi% + Avg. Values rendered **in the same color as that corner's curve**. Invalid corners → "--".

- [ ] **Step 8c:** Compute per-corner stats from `bins[]` (41 fractions) and `totalCount`:
      - For each side (rebound = bins where binCenter < 0; bump = binCenter > 0):
        - Hi% = sum of fraction for |binCenter| > slowRange × 100
        - Lo% = sum of fraction for |binCenter| ≤ slowRange × 100
      - Avg [mm/s] on that side = Σ(binCenter_i × count_i) / totalCount_side (reconstruct from bins; if totalCount is 0 → "--").

- [ ] **Step 8d:** Grid + axes.
      - Subtle grid lines: vertical at every x-tick, horizontal at every y-tick, color `(60, 70, 70, 70)`.
      - Y-axis ticks: compute `displayMax = maxBinFraction × (1 + histogramYOffset/100)` across all valid corners. Generate ~5 nice-number tick values from 0 to displayMax×100 (%). Draw labels left-aligned in the margin.
      - X-axis ticks: fixed −200, −150, …, +200 (step 50 mm/s). Map bin center → pixel x using `x = marginLeft + ((v+205)/410) × chartWidth`. Draw labels below the chart area.
      - Vertical dashed line at x=0 (velocity zero crossing), slightly brighter than grid.

- [ ] **Step 8e:** Slow-speed band shading.
      - Fill a semi-transparent rectangle from `x(-slowRange)` to `x(+slowRange)`, full chart height, color `(50, 120, 140, 255)` (light blue tint). Same visual as the existing mode's shaded zone.

- [ ] **Step 8f:** Draw curves for each valid corner.
      - For corners in order LF(0), FR(1), LR(2), RR(3): if `suspensionData[i].valid`:
        - Build a point array of (x, y) from the 41 bin centers: x = mapped pixel, y = chartBottom − fraction × scaleY.
        - Draw filled area under curve with alpha ~50 in corner color.
        - Draw polyline on top with width 2px in full-opacity corner color.
      - Colors (Gdiplus::Color ARGB): LF `(237,64,59)`, FR `(86,84,244)`, LR `(236,72,153)`, RR `(0,190,230)`.

- [ ] **Step 8g:** Localization.
      - Wrap all user-facing strings ("Samples", "Zoom Linked", "Rebound", "Bump", "Avg [mm/s]", corner names "Damper Vel FL" etc.) in `tr()`.
      - Verify tab captions ('Grid', 'Combined') are also covered by `TranslateUI()` — add entries if missing.

**Gate:** Build must compile cleanly. Run the app, load a data file with 4 valid corners: switch to "Combined" tab → verify all four colored curves render, slow band visible, axes labeled correctly, legend values present and plausible (spot-check LF rebound Hi% + Lo% ≈ 100%).

---

## Phase 5 — Paint Dispatch + Live Update Wiring

- [ ] **Step 9:** Implement the paint handler:
      ```cpp
      void __fastcall TFormMotecSetup::DamperCombinedPaintBoxPaint(TObject* Sender) {
          TPaintBox* pb = (TPaintBox*)Sender;
          Gdiplus::Graphics graphics(pb->Canvas->Handle);
          graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
          DrawCombinedDamperChart(graphics, pb->Width, pb->Height);
      }
      ```

- [ ] **Step 10:** In `UpdateSuspension()` final invalidation block (currently unconditionally invalidates all four boxes), replace with active-tab-aware logic:
      ```cpp
      if (DamperLayoutTabs != NULL) {
          if (DamperLayoutTabs->ActivePageIndex == 0) {
              // invalidate the four grid boxes
          } else {
              DamperCombinedPaintBox->Invalidate();
          }
      }
      ```

**Gate:** Build must compile cleanly. Full functional test below.

---

## Phase 6 — Verification & Acceptance Tests

- [ ] **Test A (no regression):** Load an AMS2 CSV/LD file with 4 valid corners on the default "Grid" tab → confirm pixel-identical behavior to before this feature was added.
- [ ] **Test B (combined view correctness):** Switch to "Combined" tab: chart shows 4 colored curves, slow band, axes; legend values match manual bin sums for at least LF rebound Hi%/Lo% and RR bump Avg.
- [ ] **Test C (AC Evo / derived velocity path):** Load an AC Evo file → valid corners render on both tabs; any invalid corner row shows "--" without crash or NaN text.
- [ ] **Test D (live capture + tab switch):** Start live capture, switch between tabs mid-session: only the active tab's paintbox repaints each frame, no flicker of hidden boxes. Data updates correctly in whichever view is visible.
- [ ] **Test E (persistence):** Set "Combined" as active → close app → reopen → "Combined" tab is selected on startup. Switch back to "Grid", repeat — both directions persist via INI `[General] HistogramLayout`.
- [ ] **Test F (localization):** Switch LanguageCombo to French → header/column captions + tab captions are localized (`tr()` coverage in `TranslateUI()`).

---

## Scope Boundaries / Exclusions

- "Zoom Linked" is a **static label** — no zoom/pan interactivity.
- Existing 4-box rendering (`DrawModernHistogram`) and data pipeline untouched apart from the additive `totalCount` storage. The four paintboxes are re-parented into TTabSheet but their geometry/rendering logic stays identical.
- Colors fixed per corner; no new thresholds.ini keys needed.

## Future Extensibility (why tab control wins)

Adding a 3rd layout later: drop in another TTabSheet + its PaintBox(es) in DFM, add one paint handler, extend the invalidation switch in Step 10 by one case. No enum refactor, no new toggle widget, INI persistence works automatically since it stores an index.
