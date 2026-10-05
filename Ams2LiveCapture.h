//---------------------------------------------------------------------------
// Ams2LiveCapture.h — AMS2 live capture from shared memory.
//
// Provides functions to open/read/close the AMS2 shared memory object
// ($pcars2$) and to feed a single frame into the LapStats accumulators.
//---------------------------------------------------------------------------

#pragma once

#include <string>
#include "Ams2SharedMemory.h"
#include "MotecSetupAnalyzer.h"

// Open the AMS2 shared memory object and map it into our address space.
// Returns true on success; on failure, errorMessage contains a description.
bool Ams2OpenSharedMemory(std::string& errorMessage);

// Read a single consistent frame from shared memory.
// Uses the mSequenceNumber double-check pattern to avoid torn reads.
// Returns true if a valid frame was read.
bool Ams2ReadFrame(Ams2SharedMemory& out);

// Unmap and close the shared memory handle.
void Ams2CloseSharedMemory();

// Feed one shared-memory frame into the LapStats accumulators.
// timeSeconds is the wall-clock time (GetTickCount64()/1000.0) used for
// damper velocity derivation.
void Ams2FeedLap(LapStats& lap, const Ams2SharedMemory& frame, double timeSeconds);

// Returns true when the car is on track (not in pits, game is playing).
bool Ams2IsOnTrack(const Ams2SharedMemory& frame);

// Returns true when the car is in the pit lane or garage.
bool Ams2IsInPits(const Ams2SharedMemory& frame);

// Returns true if the AMS2 process (AMS2.exe) is currently running.
bool Ams2IsProcessRunning();
