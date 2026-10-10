//---------------------------------------------------------------------------
// AcsEvoLiveCapture.h — AC EVO live capture from shared memory.
//
// AC EVO exposes three shared memory objects (session-scoped, "Local\" prefix):
//   Local\acevo_pmf_physics  -> SPageFilePhysics
//   Local\acevo_pmf_graphics -> SPageFileGraphicEvo
//   Local\acevo_pmf_static   -> SPageFileStaticEvo
//
// Provides functions to open/read/close those objects and to feed a single
// frame (physics + graphics) into the LapStats accumulators.
//---------------------------------------------------------------------------

#pragma once

#include <string>
#include "AcsEvoSharedMemory.h"
#include "MotecSetupAnalyzer.h"

// Open the three AC EVO shared memory objects and map them into our address
// space. Returns true on success; on failure, errorMessage contains a
// description.
bool AcsEvoOpenSharedMemory(std::string& errorMessage);

// Read a single consistent physics frame.
// Returns true if a valid frame was read.
bool AcsEvoReadPhysics(SPageFilePhysics& out);

// Read a single consistent graphics frame.
// Returns true if a valid frame was read.
bool AcsEvoReadGraphics(SPageFileGraphicEvo& out);

// Read the static object (track name, session, lap length, ...).
// Returns true on success.
bool AcsEvoReadStatic(SPageFileStaticEvo& out);

// Unmap and close all three shared memory handles.
void AcsEvoCloseSharedMemory();

// Feed one shared-memory frame (physics + graphics) into the LapStats
// accumulators. Unit conversions live in mapping.json EVOM1 LIVE rows: this
// function only extracts RAW values from the frames and lets the generic
// table-driven feed apply factor/offset per row; multi-value derived samples
// (damper velocity, handling sample, steer/yaw) stay post-table by design.
// timeSeconds is the wall-clock time (GetTickCount64()/1000.0).
void AcsEvoFeedLap(LapStats& lap, const SPageFilePhysics& phys,
                   const SPageFileGraphicEvo& gfx, double timeSeconds);

// Returns a non-empty warning string if any EVOM1 LIVE mapping row failed to
// resolve at startup (empty table or missing raw channel names). Empty = OK.
std::string AcsEvoGetChannelTableWarning();

// Returns true when the car is on track (game live, car on the track).
bool AcsEvoIsOnTrack(const SPageFileGraphicEvo& gfx);

// Returns true when the car is in the pit lane, pit entry/exit, or pit box.
bool AcsEvoIsInPits(const SPageFileGraphicEvo& gfx);

// Detect a lap change via the npos wrap (0.0 -> 1.0 per lap).
// Returns true when the position wrapped from near the end to near the start.

