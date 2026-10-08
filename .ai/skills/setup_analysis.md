# Skill: Setup & Performance Analysis (AMS2)

This skill provides expertise in analyzing vehicle setup and performance for Automobilista 2, using a three-layer data correlation model.

## 🧠 Core Concept: The Three Layers of Truth

To perform accurate diagnostics, always correlate data across these three layers:

1.  **Layer 1: Theoretical Setup (The CDF Layer)**
    *   **Source**: `.cdf` files parsed into `data/reference/ams2_vehicles_db.json`.
    *   **Purpose**: Defines the "Rules of the Game". Provides physical limits (`min`, `max`), resolution (`step`), and default values.
    *   **Usage**: Use this to validate if a recommendation is physically possible and follows manufacturer increments.

2.  **Layer 2: Session Configuration (The Snapshot Layer)**
    *   **Source**: Shared Memory captured during stable/pit conditions.
    *   **Purpose**: Defines the "Driver's Intent". The actual settings applied for the current session.
    *   **Usage**: This is your baseline. Compare telemetry deviations against this snapshot, not the CDF defaults.

3.  **Layer 3: Dynamic Performance (The Telemetry Layer)**
    *   **Source**: Motec Data (Real-time/Recorded) + **Calculated Channels (Math Channels)**.
    *   **Purpose**: Defines the "Physical Reality". What actually happened on track, including derived metrics like Rake, Slip Angle, and Combined Grip.
    *   **Usage**: The source of truth for detecting anomalies (e.g., unexpected suspension travel, temperature spikes, or grip saturation).

---

## 🛠️ Correlation Rules (Proxy Analysis)

Since many setup parameters are not directly available in telemetry, use these proxy correlations:

### Aerodynamics (Aero)
*   **Target Parameter**: `RearWingSetting` / `FrontWingSetting`.
*   **Telemetry Proxy**: Vertical Load/Compression & **Rake**.
    *   **Correlation**: High Aero Setting $\rightarrow$ Increased compression of `mSuspensionTravel[i]` at high `mWorldVelocity`. Use **Rake** to monitor the longitudinal pitch and its impact on aero balance.
    *   **Anomaly Detection**: If `RWSetting` is high but suspension travel remains low at speed, or if Rake is unstable, suspect aero stall or incorrect spring rates.

### Braking (Brake Bias)
*   **Target Parameter**: `BrakeBias`.
*   **Telemetry Proxy**: Brake Pressure vs Deceleration & **Brake Bias Efficiency**.
    *   **Correlation**: Compare `mBrake` (driver input) with longitudinal G-force and brake temperature distribution. Use **Brake Bias Eff.** to validate the actual deceleration achieved per unit of pressure.

### Tyres & Grip
*   **Target Parameter**: `TyrePressure` / `Camber`.
*   **Telemetry Proxy**: Tyre Temperature Gradients (`mTyreTempLeft/Right/Center`) & **Slip Angle/Ratio**.
    *   **Correlation**: High Camber $\rightarrow$ Higher inner/outer temperature differential. Use **Slip Angle** to detect the point of grip saturation (understeer/oversteer).

---

## 🤖 Recommendation Logic Workflow

When generating a recommendation, follow this strict sequence:

1.  **Identify Anomaly**: Detect an issue in **Layer 3** (e.g., "Rear end instability").
2.  **Check Intent**: Look at **Layer 2** to see the current setting (e.g., "Rear Wing is at Setting 0").
3.  **Validate Constraints**: Consult **Layer 1** for the allowed range and step (e.g., "Range is 0-4, Step is 1").
4.  **Formulate Suggestion**: Propose a change that moves from Layer 2 towards an optimal value within Layer 1's bounds.
    *   *Bad*: "Increase wing by 0.5".
    *   *Good*: "Increase Rear Wing setting from 0 to 1 (next available step)".

---

## ⚠️ Critical Constraints
- **Never** suggest a value outside the `min`/`max` defined in the JSON.
- **Always** respect the `step` increment of the parameter.
---

## 📋 Required Vehicle Constants for Advanced Analysis

To implement and utilize "Math Channels" (Calculated Channels), the following car-specific constants must be provided (either via manual user input or vehicle database):

| Category | Parameter | Symbol | Used In |
| :--- | :--- | :---: | :--- |
| **Mass** | Total Vehicle Mass (with driver) | $m$ | Weight Transfer, Combined Grip |
| **Dimensions** | Wheelbase (Front axle to Rear axle) | $L$ | Longitudinal Weight Transfer |
| **Dimensions** | Track Width (Left to Right) | $W_{f/r}$ | Lateral Weight Transfer |
| **Geometry** | Center of Gravity Height | $H_{CG}$ | All Weight Transfer calculations |
| **Suspension** | Spring Rates (FL, FR, RL, RR) | $k$ | Estimated Tyre Load |
| **Tyres** | Dynamic Rolling Radius (Front/Rear) | $R$ | Slip Ratio, Wheelspin % |
| **Aero** | Air Density (Standard or Local) | $\rho$ | Dynamic Pressure ($q$) |
