# PMR Live Capture via UDP — Implementation Plan

> **Goal:** Add a third live-capture backend for the game *PMR*, which exposes telemetry over UDP on `127.0.0.1` (default port 7576), alongside AMS2 and AC EVO (both shared-memory today).
>
> **Rule of engagement:** Each step below must be **implemented AND its gate test passed** before the next step begins. Do not skip ahead. If a gate fails, fix it in place — do not proceed to the following step.

---

## Language & Toolchain Constraint (READ FIRST)

| Aspect | Rule |
|--------|------|
| Implementation language | **C++ only** (C++Builder / VCL), matching all existing source files (`AcsEvoLiveCapture.cpp`, `MotecSetupForm.cpp`, etc.) |
| C# reference code in `data/reference/PMR/udpSample/` | **Read-only protocol documentation.** It exists solely to document the binary wire format (field order, byte sizes, endianness). Do NOT port it line-by-line. Do NOT add any .NET dependency, P/Invoke bridge, or C# interop layer. The final app must remain a pure native Win32/VCL executable with no managed-code runtime requirement. |
| Socket API | Native **Winsock 2** (`ws2_32.lib`) — same as what the OS provides to any C++ program on Windows. No third-party networking library (no Boost.Asio, no libuv). |
| Build system | Embarcadero C++Builder project file `Project1.cbproj`. New files are added there; nothing else changes in build tooling. |

In short: the C# sample tells you *what bytes to expect on the wire*. You write a **C++ struct + manual byte reader** that consumes those same bytes natively, exactly as `AcsEvoLiveCapture.cpp` reads shared-memory structs today — just from UDP datagrams instead of mapped memory.

---

## Phase 0 — Prerequisites & Reference (read-only)

No code changes. Confirm you have read and understood these references:

- [ ] Read `data/reference/PMR/udpSample/docs/UDPTelemetryProtocol.h` — authoritative binary layout for all 3 packet types (`RaceInfo`, `ParticipantState`, `VehicleTelemetry`).
- [ ] Read `data/reference/PMR/udpSample/SimpleUDPServer/{UDPThread.cs, UDPProtocol.cs, ConversionHelper.cs}` **as protocol documentation only** (it is C#). Extract from it: byte-order (little-endian), dynamic-string format (1-byte len + N bytes UTF8), and float-list / wheel-list framing. You will re-implement this logic natively in C++ — do not copy the .NET code, its `BitConverter` calls, or any managed types into the project.
- [ ] Read `AcsEvoLiveCapture.cpp:218–377` (`AcsEvoFeedLap`) — the exact field-by-field template for populating a `LapStats`, including unit conventions (PSI, cm, deg/s, %).
- [ ] Read `MotecSetupAnalyzer.h`: line 29 (`ChannelProfile`), lines 1075–1146 (`addHandlingSample`, `addSteerYaw`, `DamperCornerStats::addSample`), line 1176 (`buildDataProviderFromLap`), line ~1447 (`pickDamperHistogram`).
- [ ] Read `MotecSetupForm.cpp`: combo items (~line 685), mode mapping (~lines 795, 2215), connect timer handler (~line 2247), data pump handler (~line 2350), EVO frame processor (`processLiveFrameEVO`, ~line 2487), stop logic (~line 2297).
- [ ] Read `Project1.cbproj` lines 85–210 — source file listing and linked libraries.

**Gate 0:** You can answer, without re-opening files: (a) what are the three PMR packet types and their version numbers? (b) which `LapStats` fields does EVO's feed populate that PMR must also fill for full parity? (c) where in `MotecSetupForm.cpp` is a new backend branch added to both connect and data-pump paths?

---

## Phase 1 — Protocol Structs & Binary Decoder (new file, no UI dependency)

Create **`PmrLiveCapture.h`** and **`PmrLiveCapture.cpp`**. This phase produces pure decode logic with zero socket or VCL dependencies. It can be unit-tested in isolation.

### Step 1.1 — Header: protocol structs + public API declarations
- [ ] Create `PmrLiveCapture.h`.
- [ ] Define C++ structs mirroring the PMR binary layout (little-endian, packed). Include at minimum:
  - `struct PmrVec3 { float x,y,z; };`
  - `struct PmrQuat { float w,x,y,z; };` *(note: verify field order against protocol — C# sample reads x,y,z,w)*
  - `struct PmrWheelTelemetry { int contactMaterialHash; float angVel, linearSpeed; PmrVec3 slideLS, forceLS, momentLS; float contactRadius, pressure, inclination, slipRatio, slipAngle; PmrVec3 tread; float carcass, internalAir, wellAir, rim, brake; float springStrain, damperVelocity; float hubTorque, hubPower, wheelTorque, wheelPower; };`
  - `struct PmrChassis { ... }`, `PmrDrivetrain { ... }`, `PmrSuspension { ... }`, `PmrInput { ... }`, `PmrSetup { ... }`, `PmrGeneral { ... }`, `PmrConstant { ... }`.
  - Top-level: `struct PmrVehicleTelemetryPacket { uint8_t packetType; uint16_t version; int32_t vehicleId; std::vector<PmrWheelTelemetry> wheels; PmrChassis chassis; PmrDrivetrain drivetrain; PmrSuspension suspension; PmrInput input; PmrSetup setup; PmrGeneral general; PmrrConstant constant; };`
  - `struct PmrRaceInfoPacket { ... }`, `PmrParticipantStatePacket { ... }`.
- [ ] Declare the public API (free functions, matching AMS2/EVO convention):
  ```cpp
  // Socket lifecycle
  bool PmrOpenSocket(int port, std::string& err);
  void PmrCloseSocket();

  // Non-blocking read: returns true if a packet was decoded into `out`.
  // type is set to the UDPPacketType enum value.
  bool PmrReadPacket(uint8_t& outType, void* outStruct);

  // Feed one telemetry frame into an in-progress lap (mirrors AcsEvoFeedLap).
  void PmrFeedLap(LapStats& lap, const PmrVehicleTelemetryPacket& pkt, double timeSeconds);

  // Session metadata accessors (set from RaceInfo / ParticipantState packets)
  std::string PmrGetTrackName();
  std::string PmrGetDriverName(int vehicleId);
  int         PmrGetCurrentSector(int vehicleId);   // for lap detection
  bool        PmrIsInPits(int vehicleId);

  // Tread ordering flag (swappable if real data shows different order)
  void PmrSetTreadOrder(bool innerFirst);           // default true: [0]=Inner,[1]=Middle,[2]=Outer
  ```
- [ ] Include guard, `#include "MotecSetupAnalyzer.h"` for `LapStats`.

### Step 1.2 — Binary reader helper (in .cpp)
- [ ] Implement a small internal class/struct `PmrReader` in **plain C++** (no managed types, no P/Invoke). It wraps a `(const uint8_t* data; size_t len; size_t pos;)` triple with methods:
  - `bool readU8(uint8_t&)`, `readU16(uint16_t&)`, `readI32(int32_t&)`, `readF32(float&)`. Each copies the raw little-endian bytes directly into a native type (on x86/x64 Windows this is a straight memcpy — no byte-swapping needed, matching how C#'s `BitConverter` behaves on LE hosts).
  - `std::string readString()` — reads 1-byte length, then N bytes as UTF-8.
  - Template: `template<typename T> std::vector<T> readList(std::function<void(PmrReader&,T&)> decoder)` — reads count byte, loops.
  - Bounds-check every read; return false on overflow so a malformed packet can't corrupt memory (this replaces the C# sample's implicit array-indexing with explicit safety).

### Step 1.3 — Decode functions (in .cpp)
- [ ] `bool PmrDecodeRaceInfo(const uint8_t* data, size_t len, PmrRaceInfoPacket& out)` — parse all fields per protocol; version-check (`m_packetVersion == 1`), return false on mismatch. All three decode functions are pure C++ operating on a raw byte buffer — the same style as `DecodeMotecLd()` in `MotecToCSV.cpp`, which already hand-parses binary Motec data natively.
- [ ] `bool PmrDecodeParticipantState(...)` — same pattern (version 1). Parse dynamic strings for vehicle/driver/livery/class names, sector time float-lists, flags.
- [ ] `bool PmrDecodeVehicleTelemetry(...)` — version check (`== 2`). Decode wheel list (variable count), chassis vec3/quat blocks, drivetrain floats + gear sub-list, suspension avgLoads float-list, input/setup/general/constant sections.

### Step 1.4 — Unit-test the decoder against a captured packet
- [ ] Obtain one real datagram: run `data/reference/PMR/udpSample/bin/simpleudpserver.exe` (or PMR itself with UDP enabled) and capture **one** telemetry datagram to a file (`pmr_sample.bin`). *(If you can't get live data, hand-craft a minimal valid packet from the protocol spec — at minimum: header + 4 wheels + chassis + drivetrain.)*
- [ ] Write a small standalone test (a `main()` in a scratch `.cpp` compiled separately, or add to an existing test harness if one exists) that calls `PmrDecodeVehicleTelemetry` on the captured bytes and prints key fields: vehicleId, wheel count, LF pressure, LF tread[0..2], damperVelocity per corner, engineRPM, coolant temp.
- [ ] **Verify** printed values are physically plausible (e.g., tyre pressures 30–45 PSI-equivalent, RPMs in thousands, temps 60–120 °C). If a field is wildly off → offset/endianness bug; fix before proceeding.

> **GATE 1:** The decoder compiles standalone and correctly parses at least one real (or hand-crafted) telemetry packet with all key fields showing plausible values. No socket code, no VCL — pure byte→struct conversion verified.
>
> ⛔ Do NOT proceed to Phase 2 until Gate 1 passes.

---

## Phase 2 — Socket Layer (non-blocking UDP on loopback)

Extend `PmrLiveCapture.cpp` with Winsock lifecycle and a non-blocking receive path. This runs inside the VCL main-thread timer, so **it must never block**.

### Step 2.1 — WSAStartup / WSACleanup
- [ ] Add static init: call `WSAStartup(MAKEWORD(2,2), &wsaData)` once (lazy on first `PmrOpenSocket`); pair with `WSACleanup()` in `PmrCloseSocket()`. Use a reference count or simple bool guard.

### Step 2.2 — Socket creation + bind
- [ ] In `PmrOpenSocket(port, err)`:
  - Create socket: `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)`.
  - Set **non-blocking**: `u_long mode = 1; ioctlsocket(s, FIONBIO, &mode);` (or use zero-timeout on recvfrom).
  - Bind to `{ INADDR_ANY or 127.0.0.1, port }`. Use `INADDR_ANY` so it works whether PMR unicasts to loopback explicitly or via multicast join. *(If the game uses multicast by default — group 224.0.0.150 per readme.txt — also call `setsockopt(IP_ADD_MEMBERSHIP)`; make this configurable later, start with unicast/ANY for simplicity since user will set PMR to `multicast=false`.)*
  - Store the socket handle in a file-static variable (matching AMS2/EVO's module-level static pattern).

### Step 2.3 — Non-blocking drain read
- [ ] Implement `PmrReadPacket(type, outStruct)`:
  - Call `recvfrom()` with zero timeout; if it returns `WSAEWOULDBLOCK` → no data pending → return false immediately (non-blocking guarantee).
  - On success: dispatch on first byte (`packetType`) to the appropriate decode function from Phase 1. Store decoded result in a file-static struct so callers can access fields without re-parsing.
  - Return true if a packet was successfully received AND decoded; false otherwise.

### Step 2.4 — Drain-all helper (for the timer tick)
- [ ] Add `int PmrDrainAllPackets()` that loops calling `PmrReadPacket` until it returns false, returning the count of packets processed this tick. This ensures we don't drop frames if multiple datagrams arrived between 16ms ticks.

### Step 2.5 — Link Winsock
- [ ] Add `#pragma comment(lib, "Ws2_32.lib")` at top of `PmrLiveCapture.cpp`.
- [ ] Ensure `<winsock2.h>` is included **before** any windows header in this translation unit (the PCH pulls in `<windows.h>`; guard with a local include-order check or put the winsock2 include first in the .cpp).

> **GATE 2:** `PmrOpenSocket(7576, err)` succeeds when called from a test harness while PMR (or the C# sample server) is emitting UDP on that port. A loop calling `PmrDrainAllPackets()` at ~10 Hz prints packet counts > 0 and decodes telemetry without crashing or blocking. Unplugging/stopping the sender → drain returns 0 cleanly, no hang.
>
> ⛔ Do NOT proceed to Phase 3 until Gate 2 passes.

---

## Phase 3 — Feed Function: PMR Telemetry → LapStats

Implement `PmrFeedLap()` in `PmrLiveCapture.cpp`. This is the core mapping from one decoded telemetry frame into our accumulator struct, mirroring `AcsEvoFeedLap` (AcsEvoLiveCapture.cpp:218–377) field-by-field.

### Step 3.1 — Core driving stats
- [ ] Set `lap.rideHeightUnit = RideHeightUnit::Centimeters;`.
- [ ] Map and `.add()` to StatSummary fields (with unit conversions as named constants at top of function):
  - Speed: `pkt.chassis.m_overallSpeed` → km/h *(verify units — if m/s, ×3.6)*.
  - RPM: `pkt.drivetrain.m_engineRPM`.
  - Throttle %: `pkt.input.m_accelerator * 100.0f` (if normalized 0..1) or direct %.
  - Brake pedal %: same pattern for `m_brake`.
  - Steer angle deg: `pkt.general.m_steeringWheelAngle` *(verify rad vs degrees; if radians ×57.296)*.
  - Yaw rate deg/s: `pkt.chassis.m_angularVelocityLS.y * DEG_PER_RAD` (y-axis = yaw in LS frame).
  - Lateral G / Longitudinal G: from `m_accelerationLS.x` and `.z` *(verify axis convention — x=lateral, z=longitudinal typical; divide by 9.81 if m/s²)*.

### Step 3.2 — Wheel data (×4)
- [ ] For each wheel index i ∈ {0:LF, 1:RF, 2:LR, 3:RR} *(verify order against protocol / real data; PMR may use a different ordering than EVO)*:
  - **Wheel speed km/h**: `wheels[i].m_linearSpeed` (if m/s → ×3.6) or derive from `m_angVel * wheelRadius`.
  - **Brake temp °C**: `wheels[i].brake`.
  - **Tyre pressure PSI**: `wheels[i].pressure` *(convert if bar: ×14.5038; if kPa: /6.89476)* → `.add()` to `lap.pressureLF/RF/LR/RR`.
  - **Slip ratio %**: `wheels[i].slipRatio * 100.0f` (match EVO convention of feeding %) → `lap.wheelSlip*`.

### Step 3.3 — Tyre temperatures (×4 wheels × 3 points = 12)
- [ ] For each wheel, read the `tread[3]` vector:
  - Default mapping (`innerFirst=true`): `[0]=Inner`, `[1]=Middle/Centre`, `[2]=Outer`.
  - `.add()` to `lap.tyre{Inner,Centre,Outer}Temp{LF/RF/LR/RR}`.
- [ ] Also set the surface-centre temps used by front/rear diff checks: `lap.surfTempCFL.add(tread[1])` etc. (same as EVO feeds middle temp).

### Step 3.4 — Suspension & damper velocity
- [ ] **Travel mm**: `wheels[i].springStrain * kSpringStrokeMm` *(if normalized 0..1, multiply by max stroke in mm; if already meters ×1000)* → `.add()` to `lap.susTravel*`.
- [ ] **Direct damper velocity** (the key advantage over AMS2/EVO): `wheels[i].damperVelocity` *(verify units — likely m/s → ×1000 for mm/s, matching our histogram range of −205..+205 mm/s)* → `.add()` to the **direct histograms**:
  ```cpp
  lap.suspVelLF.add(velMmS);   // preferred path via pickDamperHistogram()
  lap.suspVelRF.add(...);
  lap.suspVelLR.add(...);
  lap.suspVelRR.add(...);
  ```
- [ ] **Fallback travel-derived** (also populate, costs nothing): `lap.damperLF.addSample(travelMm, timeSeconds)` etc. This gives the rule engine a second source if direct velocity is ever missing/zero for some frames.

### Step 3.5 — Thermal / fuel / environmental
- [ ] Oil temp: `pkt.drivetrain.m_engineOilTemperature` → `lap.oilTemp.add()`.
- [ ] Water/cooldown temp: `pkt.drivetrain.m_engineCoolantTemperature` → `lap.waterTemp.add()`.
- [ ] Fuel level %: `pkt.drivetrain.m_fuelRemaining / pkt.constant.m_fuelCapacity * 100.0f` (if both in litres) or direct if already normalized → `lap.fuelLevel.add()`.

### Step 3.6 — Handling evidence + steer/yaw correlation
- [ ] Compute local variables: `steer`, `yaw`, `latG`, `longG`, `brake%`, `throttle%`, `suspAsymmetry = (travelLF - travelRF) / 2.0`.
- [ ] Call `lap.addHandlingSample(steer, yaw, latG, longG, brake, throttle, suspAsymmetry);` — same signature as EVO (~line 365 of AcsEvoLiveCapture.cpp).
- [ ] Call `lap.addSteerYaw(steer, yaw);`.

### Step 3.7 — Row count + optional fields
- [ ] Increment `lap.rowCount++`.
- [ ] *(Optional / best-effort)* Ride heights: PMR has no direct per-corner ride-height channel. **Leave empty initially** (`aeroHeight0..3` stay at count=0). The rule engine degrades gracefully — Rake virtual channel simply won't resolve, and `buildDataProviderFromLap` skips the ride-height block when counts are 0. Add derivation from spring-strain baseline later only if needed.

### Step 3.8 — Session metadata accessors
- [ ] Implement `PmrGetTrackName()` (from cached RaceInfo), `PmrGetDriverName(id)`, `PmrGetCurrentSector(id)` (returns last-seen `m_currentSector` for that vehicleId, default -1 if unknown), `PmrIsInPits(id)` (last-seen `m_inPits`).
- [ ] These are populated as a side-effect of `PmrReadPacket()` when RaceInfo / ParticipantState packets arrive — store in file-static maps keyed by vehicleId.

### Step 3.9 — Alias resolution: NO new mapping needed for live mode (verify, don't implement)
This is the contract that makes PMR behave exactly like AMS2/AC EVO downstream of `PmrFeedLap`:

- [ ] **Understand & confirm** how aliases are fed in live mode: when a lap finalizes, rules run via `evaluateLapWithRules()` (`analyze_motec_setup.cpp`), which calls `buildDataProviderFromLap(lap)` — this builds the alias gateway by reading **populated `LapStats` fields directly**, not raw channel names. That is why AMS2 and AC EVO have no entries in `MotecDataAnalyser.mapping.json`: live mode never consults it for rule evaluation; only file-based (CSV/LD) parsing uses profile mappings to name channels before they land in the same `LapStats` fields.
- [ ] **Therefore:** do NOT add a PMR section to `mapping.json`, and do NOT create any new alias entries, as part of live-capture integration. The only requirement is that Step 3.1–3.7 populate every `LapStats` field the rules consume (the same set EVO populates). If an alias resolves empty in a PMR report, the bug is a missing/zeroed field in `PmrFeedLap`, not a mapping gap — fix it there.
- [ ] **Verify** by cross-checking: list every `AnalysisAlias` read by the 4 active rules (`EngineTemperatureRule`, `TyreTemperatureRule`, `TyrePressureRule`, `HandlingBalanceRule`) via their `requiredInputs()`, and confirm each one maps to a field that Steps 3.1–3.7 populate (e.g., water/oil temp → Step 3.5, tyre temps/pressures/slip → 3.2–3.3, steer/yaw/Gs/handling samples → 3.6). Any alias with no corresponding populated field is a gap to close in this phase before the gate.

> **GATE 3:** Compile the full project with Phase 1–3 code included (even if not yet wired to UI). Write/extend the standalone test from Gate 2: feed ~60 consecutive decoded telemetry frames through `PmrFeedLap` into a fresh `LapStats`, then print its key averages — speed, RPM, steerAngle, yawRate, all 12 tyre temps, 4 pressures, oil/water temp, damper histogram totalCount per corner. **All values must be non-zero and physically plausible.** The direct-velocity histograms (`suspVel*`) should have `totalCount ≈ frame count`. Additionally: build a `DataProvider` from the test lap via `buildDataProviderFromLap()` (as `evaluateLapWithRules` does) and assert that every alias in Step 3.9's checklist resolves to a non-empty value — this proves PMR feeds the aliases exactly like AMS2/EVO do, with zero mapping changes.
>
> ⛔ Do NOT proceed to Phase 4 until Gate 3 passes.

---

## Phase 4 — UI Wiring (MotecSetupForm)

Wire the new backend into the existing VCL form so a user can select "PMR", start/stop live capture, and see reports. All changes are in `MotecSetupForm.h/.cpp` (+ one line in `.dfm` if needed). No changes to downstream report/chart code — it consumes `liveLaps` unchanged.

### Step 4.1 — Enums
- [ ] In **`MotecSetupAnalyzer.h:29`**: add `Pmr = 3` to `enum class ChannelProfile`. *(Keep existing values stable; append at end after Generic.)*
- [ ] In **`MotecSetupForm.h:30`**: extend `enum LiveMode { LIVE_AMS2=0, LIVE_EVO=1, LIVE_PMR=2 };`.

### Step 4.2 — Profile combo + INI persistence
- [ ] In the form constructor / init where items are added (~line 685): add `ChannelProfileCombo->Items->Add("PMR");` after "AMS2". The existing `ReadChannelProfileSetting()` already persists/reads `(int)profile`, so selecting PMR (index 3) is saved and restored automatically.
- [ ] Update the status label at ~line 786 to include a "PMR" case in its ternary/if-chain.

### Step 4.3 — Mode mapping
- [ ] In `LiveCaptureCheckBoxClick` (~lines 2195–2205) and any other site that maps profile→liveMode (e.g., ~line 795, ~line 2215): extend the logic so `ChannelProfile::Pmr → LIVE_PMR`. Current pattern is a ternary `(profile == EvoM1) ? EVO : AMS2`; change to an if-chain or switch.

### Step 4.4 — Connect path (LiveConnectTimerTimer, ~line 2247)
- [ ] Add a branch: `if (liveMode == LIVE_PMR)` → read port from INI (`[PMR] Port`, default 7576), call `PmrOpenSocket(port, err)`. On success: disable connect timer, start LiveTimer, update status label ("Live: connected to PMR on UDP :port").
- [ ] **No process-snapshot check** for PMR (unlike AMS2's Toolhelp32). If the socket binds successfully we're "connected" — data flow confirms it.

### Step 4.5 — Data pump path (LiveTimerTimer, ~line 2350)
- [ ] Add a branch: `if (liveMode == LIVE_PMR)` → call `PmrDrainAllPackets()`. For each telemetry packet received this tick, invoke the new handler **`processLiveFramePmr()`** with the decoded struct + current time.

### Step 4.6 — New frame processor: processLiveFramePmr
- [ ] Implement as a private method of `TFormMotecSetup`, mirroring `processLiveFrameEVO` (~line 2487) in structure but using PMR's lap-detection logic:
  - **Lap start**: pit→track transition (`!wasInPits && PmrIsOnTrack`) OR first telemetry after connect. Initialize a fresh `liveCurrentLap`, set metadata (driver, vehicle, track from accessors), reset accumulators.
  - **Per-lap boundary (sector wrap)**: if previous sector was ≥3 and current is ≤1 → lap complete. Call existing `finalizeLiveLap()` which pushes into `liveLaps` + generates report if needed. Start next lap immediately. *(This handles the "one full circuit" trigger robustly at any refresh rate — no time-wrap fragility.)*
  - **Pit entry**: track→pit transition → finalize current lap (same as EVO).
  - While on-track and a lap is in progress: call `PmrFeedLap(liveCurrentLap, pkt, nowSeconds)`.
- [ ] Reuse existing state members (`liveActive`, `liveInPit`, `liveLapInProgress`, `liveCurrentLap`, `liveLaps`) — no new member variables needed beyond a small PMR-specific tracker: `int lastSector = -1;` (add to the live-state block in `.h`).

### Step 4.7 — Stop / disconnect path
- [ ] In `stopLiveCapture()` (~line 2297): add `if (liveMode == LIVE_PMR) PmrCloseSocket();` alongside the existing AMS2/EVO close calls, in both the reconnection branch and full-stop branch.

### Step 4.8 — INI default for PMR port
- [ ] In the threshold/INI load section: read `[PMR] Port` with default `7576`. If missing from file on first save, write it (matching existing auto-create pattern). Add a small helper or inline in the connect branch if simpler.

> **GATE 4:** Full project compiles cleanly in C++Builder (no errors/warnings beyond pre-existing ones). Manual smoke test:
> 1. Launch PMR with UDP telemetry enabled on localhost:7576 (`multicast=false`).
> 2. In MotecDataAnalyser, select **"PMR"** from the profile dropdown.
> 3. Check **Live Capture** → status shows "connected to PMR".
> 4. Drive one full lap (or use a replay) → confirm sector wrap triggers lap finalization; `liveLaps` grows by 1 per circuit.
> 5. Uncheck Live Capture → averaged report appears in all tabs: tyre temps show 12-point grid, pressures populated, brake/oil/water temps present, damper histograms non-empty (from direct velocity).
> 6. Re-check and uncheck again without driving → no crash, clean disconnect/reconnect cycle works.
>
> ⛔ Do NOT proceed to Phase 5 until Gate 4 passes end-to-end with real PMR data.

---

## Phase 5 — Build Integration & Polish

### Step 5.1 — Project file (Project1.cbproj)
- [ ] Add source entries next to the other live-capture pairs (~lines 187–205):
  ```xml
  <CppCompile Include="PmrLiveCapture.cpp"><DependentOn>PmrLiveCapture.h</DependentOn></CppCompile>
  ...
  <None Include="PmrLiveCapture.h"/>
  ```
- [ ] Confirm `ws2_32.lib` is linked (via the `#pragma comment(lib,...)` in Phase 2 — verify it resolves at link time; if C++Builder needs an explicit entry, add to `<AllPackageLibs>` / linker settings).

### Step 5.2 — Documentation
- [ ] Update **README.md** and/or **agents.md**: document the new PMR live-capture source (how to enable UDP in-game: `multicast=false port=7576`), the INI key, and that it's a third profile option alongside Evo/AMS2.
- [ ] Add an entry to the Source Map table in agents.md for `PmrLiveCapture.h/.cpp`.

### Step 5.3 — Final regression check
- [ ] Verify AMS2 live capture still works (select "AMS2", connect, drive a lap) — no regressions from enum shifts or shared-state changes.
- [ ] Verify AC EVO live capture still works if testable on this machine.
- [ ] Confirm file-based analysis (CSV / LD import) is unaffected by the new `ChannelProfile::Pmr` value (it should be ignored in that path).

> **GATE 5:** All three backends compile and their respective connect/disconnect cycles work without cross-interference. Documentation updated. No regressions in existing functionality.
>
> ✅ **Done.** PMR live capture is fully integrated.

---

## Appendix A — Unit Conversion Constants (verify against real data)

Place these at the top of `PmrFeedLap` as named constants so they're trivially correctable after first live session:

```cpp
// --- VERIFY THESE AGAINST REAL PMR OUTPUT ON FIRST CAPTURE ---
static const double kSpeedScale      = 3.6;    // m/s → km/h (if chassis speed is m/s)
static const double kPressureToPsi   = 14.5038;// bar → PSI (or /6.89476 if kPa)
static const double kSteerRad2Deg    = 57.2958; // radians → degrees (if steer is radian-based)
static const double kAccelToG        = 1.0/9.81;// m/s² → g
static const double kDamperVelScale  = 1000.0; // m/s → mm/s (histogram range: ±205 mm/s)
static const float  kSpringStrokeMm  = 120.f;   // max suspension travel in mm (for normalized strain)
```

## Appendix B — PMR In-Game UDP Configuration Reminder

PMR must be configured to emit telemetry via **unicast** on localhost:
> Game settings → Telemetry/UDP → Enable, `multicast=false`, port=7576.

This is a user-side prerequisite (not code). If "Live Capture" shows connected but no data flows, the first thing to check is that PMR's UDP output is actually enabled and on the correct port. The C# sample server (`simpleudpserver.exe`) can be used as an alternative test source during development if PMR isn't running.

## Appendix C — File Change Summary

All new code is native C++ (C++Builder/VCL). No .NET/C# files are added to the project.

| File | Action | Phases |
|------|--------|--------|
| `PmrLiveCapture.h` | **NEW** — plain C++ header, no managed types | 1, 2, 3 |
| `PmrLiveCapture.cpp` | **NEW** — native Winsock + manual binary parsing (style of `MotecToCSV.cpp`) | 1, 2, 3 |
| `MotecSetupAnalyzer.h:29` | Add `Pmr = 3` to ChannelProfile | 4.1 |
| `MotecSetupForm.h:30` + live-state block | Add `LIVE_PMR`, `lastSector` member | 4.1, 4.6 |
| `MotecSetupForm.cpp` (~lines 685, 795, 2195–2215, 2247, 2350, ~2487 new fn, 2297) | Combo item, mode map, connect branch, pump branch, `processLiveFramePmr()`, stop close | 4.2 – 4.7 |
| `Project1.cbproj` (~lines 91/176 libs, ~187–205 sources) | Add file pair + ws2_32.lib | 5.1 |

**Explicitly NOT modified:** `MotecDataAnalyser.mapping.json`, the alias definitions in `RuleConfig.cpp`, and all rule classes — live mode feeds aliases through populated `LapStats` fields (see Step 3.9), so no mapping/alias work is part of this integration.
| `MotecDataAnalyser.ini` (runtime auto-create) | `[PMR] Port=7576` default | 4.8 |
| `README.md`, `agents.md` | Document new source | 5.2 |
