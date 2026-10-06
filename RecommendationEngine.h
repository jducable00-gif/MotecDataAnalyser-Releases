// ===============================
// RecommendationEngine - damper-velocity issue detection.
// evaluateDamperCorner / hasDamperIssue are the live path used by the UI.
// ===============================

#ifndef RECOMMENDATIONENGINE_H
#define RECOMMENDATIONENGINE_H

#include "MotecSetupAnalyzer.h" // DamperCornerStats, VelocityHistogram, g_thresholds

// True when a corner's damper-velocity distribution indicates an issue
// (asymmetry, excessive high-speed share, or a suspension that barely moves).
bool hasDamperIssue(const DamperCornerStats& stats);

#endif // RECOMMENDATIONENGINE_H
