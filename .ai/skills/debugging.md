# Skill: Investigation & Compile Debugging (MotecDataAnalyser)

Procedural guide for diagnosing runtime issues and resolving C++ Builder 12 compile errors specific to this project. Referenced by the agent when a user reports "it's broken", "CPU is high", "laps aren't detected", or a build fails.

---

## 1. Live Capture Investigation (thread + buffer architecture)

### Symptom → First Check Table

| Symptom | Most likely cause | Where to look |
|---------|------------------|---------------|
| CPU > 5% with no sim running | Thread not idling — missing `sleep_for` in the else branch of `captureThreadFunc()` | `MotecSetupForm.cpp::captureThreadFunc` |
| CPU high WITH sim connected (EVO) | Poll interval too short for write rate, or yield-only spin returned | EVO branch: must be `sleep_for(1ms)` not `yield()` |
| Laps detected but data is garbage / NaNs | Torn read — sequence/packetId double-check failing silently; frame pushed before both pages committed | `AcsEvoReadFrame` per-page token logic, `Ams2ReadFrame` seq parity check |
| No laps at all (AMS2) after migration | Drain branch in `LiveTimerTimer` not reached: wrong `liveMode`, or buffer never fills because thread condition `(liveActive && liveConnected)` is false | Check LED state + `MemoSummary` for "Connecting..." vs connected message |
| Stale data after profile switch (EVO→AMS2) | `frameBuffer` still holds frames from old union member; drain reads wrong variant | `resetLiveSession()` must clear buffer under mutex |
| Reconnect hangs / thread never re-arms | Thread was destroyed on disconnect instead of idling. Teardown condition in `stopLiveCapture(bool)` must NOT join when checkbox stays checked | `MotecSetupForm.cpp::stopLiveCapture` conditional join logic |
| FPS label frozen at 0 | `updateLiveAdaptiveInterval()` not called, or drain branch returns early because buffer is empty (thread not producing) | Verify thread condition + that sim process is actually running (`Ams2IsProcessRunning`) |

### Diagnostic Sequence (step-by-step)

1. **Confirm the thread exists and is in the right state.**
   - `captureThread != nullptr`? If null, connection timer never fired or mode mismatched.
   - Is it idling (else branch)? Check that `liveActive && liveConnected && liveMode == LIVE_X` all true simultaneously on UI thread at the moment of interest.

2. **Confirm frames are being produced.**
   - Add a temporary counter: increment an atomic in the capture loop's NewFrame path, read it from timer every 5 s → log to `MemoSummary`. If count is flat, reads return NoNewData/TornRetry exclusively.
   - For EVO: check `liveEvoTornReads` — if climbing rapidly, shared memory mapping offset or page size mismatch with the sim version.

3. **Confirm frames are being drained.**
   - In `LiveTimerTimer`, log `localFrames.size()` after swap. If always 0 while thread is producing → mutex contention (shouldn't happen) OR drain branch not entered (wrong mode).

4. **Confirm processing produces state changes.**
   - For AMS2: check that `Ams2IsOnTrack(frame)` returns true at some point during a known on-track segment. If the struct fields are all zero → memcpy from shared memory is reading wrong address / sim wrote to a different mapping name.

5. **CPU profiling (if still high after fixes).**
   - Task Manager → Details tab → sort by CPU for `Project1.exe`. Should be < 2% idle, < 8% with EVO at 333 Hz polling.
   - If one thread is pegged: it's the capture loop missing its sleep. If multiple threads: check that only ONE capture thread exists (no duplicate start on reconnect).

### Shared Memory Specifics

- **AMS2**: mapping name is `"ams2"` (case-sensitive, `OpenFileMappingA`). The struct must match byte-for-byte what the sim writes — a version mismatch in `mVersion` field means layout drift.
- **AC EVO**: two separate mappings (`"ac_evo_physics_page_file"`, `"ac_evo_graphics_page_file"`). Each has its own `packetId`. A torn read = physics page advanced but graphics hasn't (or vice versa) → the per-page token commit in `AcsEvoReadFrame` handles this.
- **PMR**: no shared memory — UDP socket, non-blocking recv on timer. Different failure mode: packet loss shows as gaps in sector transitions, not torn data.

---

## 2. C++ Builder 12 (bcc32c) Compile Gotchas

These are errors that `get_errors` / Pylance static analysis does NOT catch but the actual bcc32c build reveals.

### Union + NSDMI → Implicitly Deleted Default Constructor

**Error:**
```
[bcc32c Error] call to implicitly-deleted default constructor of 'TFormMotecSetup::CapturedFrame'
  MotecSetupForm.h(XXX): default constructor of 'CapturedFrame' is implicitly deleted because variant field 'evo' has a non-trivial default constructor
```

**Cause:** Any struct with in-class member initializers (`int x = 0;`) gets a user-declared (non-trivial) default ctor. A union whose variant contains such a struct cannot have its implicit default ctor generated. `= default` does NOT help — it's the same thing being deleted.

**Fix:** Provide an explicit empty body:
```cpp
struct CapturedFrame {
    union { ... } ;
    double timestampMs = 0.0;
    CapturedFrame() {}   // user-provided body suppresses implicit-delete rule
};
```
Safe ONLY if every code path assigns all members before use (which the capture thread does).

### `__fastcall` Signature Mismatch

VCL event handlers MUST be `void __fastcall TForm::Handler(TObject *Sender)`. If you add a parameter or change return type, bcc32c gives an opaque "incompatible function pointer" at the `.dfm` stream-load stage (runtime), not compile time. The IDE form designer will also reject it silently.

### STL RAD Studio vs Standard C++17

- `std::optional`, `std::variant`: NOT available in RAD Studio 23.0's libstdc++. Use tagged unions or sentinel values instead.
- `std::string_view`: Available but be careful — VCL `UnicodeString` does not implicitly convert to it. Explicit `.data()` + length needed.
- Structured bindings (`auto [a,b] = ...`): Work in Clang frontend, but some edge cases with VCL types fail at link time due to missing mangled symbols in the RTL.

### PCH and Include Order

`Project1PCH1.h` includes `<vcl.h>` + `<tchar.h>`. Any file that uses `std::thread`, `std::mutex`, etc. must include those AFTER the PCH (the `.cbproj` forces PCH first). If you see "unknown type name 'atomic'" it's a missing `#include <atomic>` in the specific TU, not a PCH issue.

### Linker: Missing Symbol from Dynamic RTL Package

If you get `[ilink32 Error] Unresolved external` for a VCL class method you're sure exists:
- The `.bpl` package isn't in the link path → `rsvars.bat` wasn't sourced before msbuild.
- OR you're referencing a component from a package not listed in `Project1.cbproj` `<RequiredPackages>`.

### Practical Rule

**Always build with CBuilder (or `scripts\build.ps1`) as ground truth.** Pylance/Clang static analysis is useful for typos and logic errors but will NOT predict bcc32c-specific rejections. If in doubt, compile before pushing.

---

## 3. General Debugging Heuristics for This Codebase

- **`MemoSummary->Lines->Add(...)`** is the primary runtime log. Add temporary messages liberally during investigation; remove before commit.
- **The form's `OnDestroy` / `stopLiveCapture(true)` path** must be airtight: any thread that outlives the form = access violation on exit (the 0xEEDFADE crash pattern from prior sessions).
- **GDI+ objects** (`Graphics`, `Bitmap`) are NOT thread-safe. All rendering stays on UI thread; capture thread only fills POD structs into the buffer.
- **`GetTickCount64()`** wraps at ~584 years — safe for session use, but don't store it in a 32-bit field.
