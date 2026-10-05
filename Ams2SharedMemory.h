/*************************************************************************************************
  Ams2SharedMemory.h — Local copy of the AMS2 SharedMemory structure (version 14).
  Source: C:\Program Files (x86)\Steam\steamapps\common\Automobilista 2\Support\SharedMemory\
          AMS2_SharedMemoryExampleApp\SharedMemory.h
  Copyright (c) MWL. All rights reserved.
*************************************************************************************************/

#ifndef _AMS2_SHARED_MEMORY_HPP_
#define _AMS2_SHARED_MEMORY_HPP_

#include <windows.h>

// *** Types ***

enum
{
  SHARED_MEMORY_VERSION = 14
};

enum
{
  STRING_LENGTH_MAX = 64
};

enum
{
  STORED_PARTICIPANTS_MAX = 64
};

enum
{
  TYRE_COMPOUND_NAME_LENGTH_MAX = 40
};

// Tyres
enum
{
  TYRE_FRONT_LEFT = 0,
  TYRE_FRONT_RIGHT,
  TYRE_REAR_LEFT,
  TYRE_REAR_RIGHT,
  TYRE_MAX
};

// Vector
enum
{
  VEC_X = 0,
  VEC_Y,
  VEC_Z,
  VEC_MAX
};

// (Type#1) GameState
enum
{
  GAME_EXITED = 0,
  GAME_FRONT_END,
  GAME_INGAME_PLAYING,
  GAME_INGAME_PAUSED,
  GAME_INGAME_INMENU_TIME_TICKING,
  GAME_INGAME_RESTARTING,
  GAME_INGAME_REPLAY,
  GAME_FRONT_END_REPLAY,
  GAME_MAX
};

// (Type#2) Session state
enum
{
  SESSION_INVALID = 0,
  SESSION_PRACTICE,
  SESSION_TEST,
  SESSION_QUALIFY,
  SESSION_FORMATION_LAP,
  SESSION_RACE,
  SESSION_TIME_ATTACK,
  SESSION_MAX
};

// (Type#3) RaceState
enum
{
  RACESTATE_INVALID,
  RACESTATE_NOT_STARTED,
  RACESTATE_RACING,
  RACESTATE_FINISHED,
  RACESTATE_DISQUALIFIED,
  RACESTATE_RETIRED,
  RACESTATE_DNF,
  RACESTATE_MAX
};

// (Type#5) Flag Colours
enum
{
  FLAG_COLOUR_NONE = 0,
  FLAG_COLOUR_GREEN,
  FLAG_COLOUR_BLUE,
  FLAG_COLOUR_WHITE_SLOW_CAR,
  FLAG_COLOUR_WHITE_FINAL_LAP,
  FLAG_COLOUR_RED,
  FLAG_COLOUR_YELLOW,
  FLAG_COLOUR_DOUBLE_YELLOW,
  FLAG_COLOUR_BLACK_AND_WHITE,
  FLAG_COLOUR_BLACK_ORANGE_CIRCLE,
  FLAG_COLOUR_BLACK,
  FLAG_COLOUR_CHEQUERED,
  FLAG_COLOUR_MAX
};

// (Type#6) Flag Reason
enum
{
  FLAG_REASON_NONE = 0,
  FLAG_REASON_SOLO_CRASH,
  FLAG_REASON_VEHICLE_CRASH,
  FLAG_REASON_VEHICLE_OBSTRUCTION,
  FLAG_REASON_MAX
};

// (Type#7) Pit Mode
enum
{
  PIT_MODE_NONE = 0,
  PIT_MODE_DRIVING_INTO_PITS,
  PIT_MODE_IN_PIT,
  PIT_MODE_DRIVING_OUT_OF_PITS,
  PIT_MODE_IN_GARAGE,
  PIT_MODE_DRIVING_OUT_OF_GARAGE,
  PIT_MODE_MAX
};

// (Type#8) Pit Stop Schedule
enum
{
  PIT_SCHEDULE_NONE = 0,
  PIT_SCHEDULE_PLAYER_REQUESTED,
  PIT_SCHEDULE_ENGINEER_REQUESTED,
  PIT_SCHEDULE_DAMAGE_REQUESTED,
  PIT_SCHEDULE_MANDATORY,
  PIT_SCHEDULE_DRIVE_THROUGH,
  PIT_SCHEDULE_STOP_GO,
  PIT_SCHEDULE_PITSPOT_OCCUPIED,
  PIT_SCHEDULE_MAX
};

// (Type#9) Car Flags
enum CarFlags
{
  CAR_HEADLIGHT         = (1<<0),
  CAR_ENGINE_ACTIVE     = (1<<1),
  CAR_ENGINE_WARNING    = (1<<2),
  CAR_SPEED_LIMITER     = (1<<3),
  CAR_ABS               = (1<<4),
  CAR_HANDBRAKE         = (1<<5),
  CAR_TCS               = (1<<6),
  CAR_SCS               = (1<<7),
};

// (Type#10) Tyre Flags
enum
{
  TYRE_ATTACHED         = (1<<0),
  TYRE_INFLATED         = (1<<1),
  TYRE_IS_ON_GROUND     = (1<<2),
};

// (Type#11) Terrain Materials
enum
{
  TERRAIN_ROAD = 0,
  TERRAIN_LOW_GRIP_ROAD,
  TERRAIN_BUMPY_ROAD1,
  TERRAIN_BUMPY_ROAD2,
  TERRAIN_BUMPY_ROAD3,
  TERRAIN_MARBLES,
  TERRAIN_GRASSY_BERMS,
  TERRAIN_GRASS,
  TERRAIN_GRAVEL,
  TERRAIN_BUMPY_GRAVEL,
  TERRAIN_RUMBLE_STRIPS,
  TERRAIN_DRAINS,
  TERRAIN_TYREWALLS,
  TERRAIN_CEMENTWALLS,
  TERRAIN_GUARDRAILS,
  TERRAIN_SAND,
  TERRAIN_BUMPY_SAND,
  TERRAIN_DIRT,
  TERRAIN_BUMPY_DIRT,
  TERRAIN_DIRT_ROAD,
  TERRAIN_BUMPY_DIRT_ROAD,
  TERRAIN_PAVEMENT,
  TERRAIN_DIRT_BANK,
  TERRAIN_WOOD,
  TERRAIN_DRY_VERGE,
  TERRAIN_EXIT_RUMBLE_STRIPS,
  TERRAIN_GRASSCRETE,
  TERRAIN_LONG_GRASS,
  TERRAIN_SLOPE_GRASS,
  TERRAIN_COBBLES,
  TERRAIN_SAND_ROAD,
  TERRAIN_BAKED_CLAY,
  TERRAIN_ASTROTURF,
  TERRAIN_SNOWHALF,
  TERRAIN_SNOWFULL,
  TERRAIN_DAMAGED_ROAD1,
  TERRAIN_TRAIN_TRACK_ROAD,
  TERRAIN_BUMPYCOBBLES,
  TERRAIN_ARIES_ONLY,
  TERRAIN_ORION_ONLY,
  TERRAIN_B1RUMBLES,
  TERRAIN_B2RUMBLES,
  TERRAIN_ROUGH_SAND_MEDIUM,
  TERRAIN_ROUGH_SAND_HEAVY,
  TERRAIN_SNOWWALLS,
  TERRAIN_ICE_ROAD,
  TERRAIN_RUNOFF_ROAD,
  TERRAIN_ILLEGAL_STRIP,
  TERRAIN_PAINT_CONCRETE,
  TERRAIN_PAINT_CONCRETE_ILLEGAL,
  TERRAIN_RALLY_TARMAC,
  TERRAIN_MAX
};

// (Type#12) Crash Damage State
enum
{
  CRASH_DAMAGE_NONE = 0,
  CRASH_DAMAGE_OFFTRACK,
  CRASH_DAMAGE_LARGE_PROP,
  CRASH_DAMAGE_SPINNING,
  CRASH_DAMAGE_ROLLING,
  CRASH_MAX
};

// (Type#13) ParticipantInfo struct
typedef struct
{
  bool mIsActive;
  char mName[STRING_LENGTH_MAX];
  float mWorldPosition[VEC_MAX];
  float mCurrentLapDistance;
  unsigned int mRacePosition;
  unsigned int mLapsCompleted;
  unsigned int mCurrentLap;
  int mCurrentSector;
} ParticipantInfo;

// (Type#14) DrsState Flags
enum DrsState
{
  DRS_INSTALLED       = (1<<0),
  DRS_ZONE_RULES      = (1<<1),
  DRS_AVAILABLE_NEXT  = (1<<2),
  DRS_AVAILABLE_NOW   = (1<<3),
  DRS_ACTIVE          = (1<<4),
};

// (Type#15) ErsDeploymentMode
enum ErsDeploymentMode
{
  ERS_DEPLOYMENT_MODE_NONE = 0,
  ERS_DEPLOYMENT_MODE_OFF,
  ERS_DEPLOYMENT_MODE_BUILD,
  ERS_DEPLOYMENT_MODE_BALANCED,
  ERS_DEPLOYMENT_MODE_ATTACK,
  ERS_DEPLOYMENT_MODE_QUAL,
};

// (Type#16) YellowFlagState
enum YellowFlagState
{
  YFS_INVALID = -1,
  YFS_NONE,
  YFS_PENDING,
  YFS_PITS_CLOSED,
  YFS_PIT_LEAD_LAP,
  YFS_PITS_OPEN,
  YFS_PITS_OPEN2,
  YFS_LAST_LAP,
  YFS_RESUME,
  YFS_RACE_HALT,
  YFS_MAXIMUM,
};

// (Type#17) LaunchStage
enum LaunchStage
{
  LAUNCH_INVALID = -1,
  LAUNCH_OFF = 0,
  LAUNCH_REV,
  LAUNCH_ON,
};

// *** Shared Memory ***
typedef struct
{
  // Version Number
  unsigned int mVersion;
  unsigned int mBuildVersionNumber;

  // Game States
  unsigned int mGameState;
  unsigned int mSessionState;
  unsigned int mRaceState;

  // Participant Info
  int mViewedParticipantIndex;
  int mNumParticipants;
  ParticipantInfo mParticipantInfo[STORED_PARTICIPANTS_MAX];

  // Unfiltered Input
  float mUnfilteredThrottle;
  float mUnfilteredBrake;
  float mUnfilteredSteering;
  float mUnfilteredClutch;

  // Vehicle information
  char mCarName[STRING_LENGTH_MAX];
  char mCarClassName[STRING_LENGTH_MAX];

  // Event information
  unsigned int mLapsInEvent;
  char mTrackLocation[STRING_LENGTH_MAX];
  char mTrackVariation[STRING_LENGTH_MAX];
  float mTrackLength;

  // Timings
  int mNumSectors;
  bool mLapInvalidated;
  float mBestLapTime;
  float mLastLapTime;
  float mCurrentTime;
  float mSplitTimeAhead;
  float mSplitTimeBehind;
  float mSplitTime;
  float mEventTimeRemaining;
  float mPersonalFastestLapTime;
  float mWorldFastestLapTime;
  float mCurrentSector1Time;
  float mCurrentSector2Time;
  float mCurrentSector3Time;
  float mFastestSector1Time;
  float mFastestSector2Time;
  float mFastestSector3Time;
  float mPersonalFastestSector1Time;
  float mPersonalFastestSector2Time;
  float mPersonalFastestSector3Time;
  float mWorldFastestSector1Time;
  float mWorldFastestSector2Time;
  float mWorldFastestSector3Time;

  // Flags
  unsigned int mHighestFlagColour;
  unsigned int mHighestFlagReason;

  // Pit Info
  unsigned int mPitMode;
  unsigned int mPitSchedule;

  // Car State
  unsigned int mCarFlags;
  float mOilTempCelsius;
  float mOilPressureKPa;
  float mWaterTempCelsius;
  float mWaterPressureKPa;
  float mFuelPressureKPa;
  float mFuelLevel;
  float mFuelCapacity;
  float mSpeed;
  float mRpm;
  float mMaxRPM;
  float mBrake;
  float mThrottle;
  float mClutch;
  float mSteering;
  int mGear;
  int mNumGears;
  float mOdometerKM;
  bool mAntiLockActive;
  int mLastOpponentCollisionIndex;
  float mLastOpponentCollisionMagnitude;
  bool mBoostActive;
  float mBoostAmount;

  // Motion & Device Related
  float mOrientation[VEC_MAX];
  float mLocalVelocity[VEC_MAX];
  float mWorldVelocity[VEC_MAX];
  float mAngularVelocity[VEC_MAX];
  float mLocalAcceleration[VEC_MAX];
  float mWorldAcceleration[VEC_MAX];
  float mExtentsCentre[VEC_MAX];

  // Wheels / Tyres
  unsigned int mTyreFlags[TYRE_MAX];
  unsigned int mTerrain[TYRE_MAX];
  float mTyreY[TYRE_MAX];
  float mTyreRPS[TYRE_MAX];
  float mTyreSlipSpeed[TYRE_MAX];
  float mTyreTemp[TYRE_MAX];
  float mTyreGrip[TYRE_MAX];
  float mTyreHeightAboveGround[TYRE_MAX];
  float mTyreLateralStiffness[TYRE_MAX];
  float mTyreWear[TYRE_MAX];
  float mBrakeDamage[TYRE_MAX];
  float mSuspensionDamage[TYRE_MAX];
  float mBrakeTempCelsius[TYRE_MAX];
  float mTyreTreadTemp[TYRE_MAX];
  float mTyreLayerTemp[TYRE_MAX];
  float mTyreCarcassTemp[TYRE_MAX];
  float mTyreRimTemp[TYRE_MAX];
  float mTyreInternalAirTemp[TYRE_MAX];

  // Car Damage
  unsigned int mCrashState;
  float mAeroDamage;
  float mEngineDamage;

  // Weather
  float mAmbientTemperature;
  float mTrackTemperature;
  float mRainDensity;
  float mWindSpeed;
  float mWindDirectionX;
  float mWindDirectionY;
  float mCloudBrightness;

  // PCars2 additions start, version 8
  volatile unsigned int mSequenceNumber;

  // Additional car variables
  float mWheelLocalPositionY[TYRE_MAX];
  float mSuspensionTravel[TYRE_MAX];
  float mSuspensionVelocity[TYRE_MAX];
  float mAirPressure[TYRE_MAX];
  float mEngineSpeed;
  float mEngineTorque;
  float mWings[2];
  float mHandBrake;

  // Additional race variables
  float mCurrentSector1Times[STORED_PARTICIPANTS_MAX];
  float mCurrentSector2Times[STORED_PARTICIPANTS_MAX];
  float mCurrentSector3Times[STORED_PARTICIPANTS_MAX];
  float mFastestSector1Times[STORED_PARTICIPANTS_MAX];
  float mFastestSector2Times[STORED_PARTICIPANTS_MAX];
  float mFastestSector3Times[STORED_PARTICIPANTS_MAX];
  float mFastestLapTimes[STORED_PARTICIPANTS_MAX];
  float mLastLapTimes[STORED_PARTICIPANTS_MAX];
  bool mLapsInvalidated[STORED_PARTICIPANTS_MAX];
  unsigned int mRaceStates[STORED_PARTICIPANTS_MAX];
  unsigned int mPitModes[STORED_PARTICIPANTS_MAX];
  float mOrientations[STORED_PARTICIPANTS_MAX][VEC_MAX];
  float mSpeeds[STORED_PARTICIPANTS_MAX];
  char mCarNames[STORED_PARTICIPANTS_MAX][STRING_LENGTH_MAX];
  char mCarClassNames[STORED_PARTICIPANTS_MAX][STRING_LENGTH_MAX];

  int mEnforcedPitStopLap;
  char mTranslatedTrackLocation[STRING_LENGTH_MAX];
  char mTranslatedTrackVariation[STRING_LENGTH_MAX];
  float mBrakeBias;
  float mTurboBoostPressure;
  char mTyreCompound[TYRE_MAX][TYRE_COMPOUND_NAME_LENGTH_MAX];
  unsigned int mPitSchedules[STORED_PARTICIPANTS_MAX];
  unsigned int mHighestFlagColours[STORED_PARTICIPANTS_MAX];
  unsigned int mHighestFlagReasons[STORED_PARTICIPANTS_MAX];
  unsigned int mNationalities[STORED_PARTICIPANTS_MAX];
  float mSnowDensity;

  // AMS2 Additions (v10...)

  // Session info
  float mSessionDuration;
  int mSessionAdditionalLaps;

  // Tyres
  float mTyreTempLeft[TYRE_MAX];
  float mTyreTempCenter[TYRE_MAX];
  float mTyreTempRight[TYRE_MAX];

  // DRS
  unsigned int mDrsState;

  // Suspension
  float mRideHeight[TYRE_MAX];

  // Input
  unsigned int mJoyPad0;
  unsigned int mDPad;

  int mAntiLockSetting;
  int mTractionControlSetting;

  // ERS
  int mErsDeploymentMode;
  bool mErsAutoModeEnabled;

  // Clutch State & Damage
  float mClutchTemp;
  float mClutchWear;
  bool mClutchOverheated;
  bool mClutchSlipping;

  int mYellowFlagState;

  bool mSessionIsPrivate;
  int mLaunchStage;

} Ams2SharedMemory;

#endif // _AMS2_SHARED_MEMORY_HPP_
