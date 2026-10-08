# Telemetry-Based Setup Checks and Warnings

This guide turns the aliases in `MotecDataAnalyser.mapping.json` into practical setup checks. It is a decision aid, not a universal setup sheet: a channel being mapped does not mean it is present in every profile, source, car, or session, and telemetry alone rarely proves a single cause.

AMS2-specific adjustment directions marked as memo suggestions are adapted from the user-provided *AMS2 Tuning Aide Memoire V1.0* (Steelcast27 video notes). They are presented as hypotheses to test, not as validated rules for every car.

## How to use this guide

1. Confirm the profile, source, units, and channel coverage. Ignore checks whose required channels are missing or invalid.
2. Compare repeatable laps or events from the same session. Use representative laps, not out-laps, traffic, safety-car running, or laps with damage or invalidation.
3. Change one setup item by one available increment, then repeat the same test. Keep fuel, tyres, weather, driving, and track conditions as similar as practical.
4. Keep the original setup and record the change, observed evidence, and result. Revert changes that do not improve the target symptom without creating a new one.

Prefer the car/simulator's setup limits, increments, tyre guidance, and engineering documentation over generic values. This mapping contains telemetry channels, not setup min/max values, ideal temperatures, or manufacturer targets.

## Basic: first checks

These checks are suitable for an initial pass. Require repeated, stable evidence before changing the setup.

### Tyre pressure and temperature profile

Compare `Tyre_Pressure_FL/FR/RL/RR` and the corresponding `Tyre_Temp_<corner>_Inner`, `Tyre_Temp_<corner>_Middle`, and `Tyre_Temp_<corner>_Outer` channels (for example, `Tyre_Temp_FL_Inner`) after the tyres have stabilized. Use the simulator's recommended **hot** pressure window when it provides one; do not assume cold pressure is the target. Pressure data is normalized to kPa in this mapping, including Evo Motec sources converted from psi.

| Repeated observation | First hypothesis | Small test |
| --- | --- | --- |
| Middle temperature is consistently hotter than both shoulders | Pressure may be too high for the current tyre/load/conditions | Reduce the starting pressure slightly, run comparable laps, and check both hot pressure and the temperature profile. |
| Both shoulders are consistently hotter than the middle | Pressure may be too low | Raise starting pressure slightly, then repeat the same check. |
| One shoulder is hotter than the other | Camber, corner loading, toe, driving line, or track direction may be contributing | Compare inner-to-outer temperatures on the same tyre, left/right counterparts, and cornering direction before changing pressure. |
| Pressures differ greatly across axle or side | Starting pressures, load, ambient conditions, or setup may differ | Compare hot pressures after similar running; adjust only the outlying tyre(s) toward the car's target window. |

The middle-versus-shoulders rule is only a screening heuristic. A hot middle or shoulders can also result from carcass construction, load, surface, measurement timing, or driving conditions. Do not chase one transient sample.

The supplied AMS2 tuning memo uses roughly 75-85 C as a soft-tyre working window, treats below about 65 C as cold, and suggests checking pressure after roughly three laps. Use these only as that memo's starting references: the correct window and warm-up time depend on the car, compound, weather, and what the selected temperature channel measures. Prefer the car's own guidance and compare stabilized readings from comparable laps.

On a front-wheel-drive car, the rear tyres may warm more slowly because they receive less drive energy. A cooler rear axle early in a run is not, by itself, evidence of incorrect rear pressure, camber, or suspension. Compare front/rear warm-up over several representative laps and assess handling only after the rear tyres have stabilized; the effect varies with car, tyre, weather, braking, and track layout.

### Camber and shoulder temperatures

Compare **inner versus outer** temperatures on each tyre; use middle temperature as context. Assuming the usual convention that more negative camber tilts the top of the wheel inward:

- Inner consistently hotter than outer, especially on loaded tyres: excessive negative camber is one possibility. Test a small reduction in negative camber.
- Outer consistently hotter than inner in corners: insufficient negative camber is one possibility. Test a small increase in negative camber.
- A large shoulder difference only on one side, or a similar difference on straight sections, is not enough to diagnose camber. Check toe, tyre pressure, wheel alignment, track direction, corner loading, and temperature channel orientation first.

The mapping includes per-corner camber angles for Evo Live (`Camber_FL/FR/RL/RR`) but does not expose a universal camber setup target. AMS2 live temperature aliases for inner/outer are not all guaranteed to be directly equivalent to MoTeC naming; verify channel meaning for the selected source.

The memo associates front-tyre overheating in fast corners with excessive front aero. Treat this as an intermediate hypothesis only: confirm that the rise is repeatable at higher speed, compare front/rear balance and tyre load/temperature patterns, and then test a small front-aero change if the car exposes one. Temperature color labels such as “yellow” are display conventions and are not telemetry values in this mapping.

### Wear, damage, and simple condition warnings

- Compare `Tyre_Wear_*`, `Disc_Life_*`, `Pad_Life_*`, `Brake_Damage_*`, `Suspension_Damage_*`, `Engine_Damage`, and `Car_Damage_*` across corners and laps. A sudden change or a single corner outlier merits inspection before setup tuning.
- Use `Tyre_Flags_*`, `Terrain_*`, `Number_Of_Tyres_Out`, `Lap_Invalidated`, `Timing_Is_Invalid`, `Crash_State`, and `Car_Flags` to identify laps/events that should not be mixed into a clean setup comparison.
- Treat wear values as source-native unless that profile documents their scale. The mapping does not define a universal remaining-life threshold.

### Temperatures and operating limits

- Check `Engine_Water_Temp`, `Engine_Oil_Temp`, `Engine_Oil_Pressure`, `Engine_Water_Pressure`, `Fuel_Pressure`, `Clutch_Temp`, and `Exhaust_Temperature` for sustained abnormal trends or warnings from the simulator/car. These are primarily **reliability warnings**, not proof that a setup change is needed.
- Check `Brake_Temp_FL/FR/RL/RR` after comparable braking. A repeatable left/right or front/rear imbalance can justify checking brake bias, ducting, dragging brakes, compound, or usage. Do not infer brake bias from one temperature sample.
- If temperature/pressure rises with a change in weather, compare `Ambient_Temp`, `Track_Temp`, `Rain_Density`, `Snow_Density`, `Air_Density`, and `Wind_Speed` before attributing it to setup.

### Basic driver-input and session checks

- Review `Throttle_Pos`, `Brake_Pos`, `Clutch_Pos`, `Steering_Input`, `Vehicle_Speed`, `Gear`, `Engine_RPM`, and `Lap_Time` together. Inconsistent inputs or shifts can explain lap-to-lap differences better than setup.
- Use `Fuel_Level_Ratio`, `Fuel_Litres_Current`, `Fuel_Per_Lap`, `Fuel_Litres_Per_Lap`, `Fuel_Estimated_Laps`, and `Laps_Possible_With_Fuel` for stint planning. Compare equivalent laps; fuel burn changes vehicle mass and therefore handling, braking, and ride height.
- Compare `Lap_Time`, `Best_Lap_Time`, `Last_Lap_Time`, sector times, and `Race_Cut_Current_Delta` only when traffic, flags, track state, and lap validity are comparable.

## Intermediate: correlate signals and driving phases

These checks require multiple channels and should be evaluated separately for corner entry, mid-corner, and exit. A symptom is evidence of a behavior; it is not itself proof of the setup cause.

### Understeer or oversteer

Use `Steering_Angle` or `Steering_Input`, `Yaw_Rate`, `G_Lat`, `Vehicle_Speed`, and `Throttle_Pos` to locate repeatable handling imbalance. Separate entry/combined braking, steady-state cornering, and power-on exit.

For FWD cars, check rear-tyre temperature and warm-up before attributing early-run rear instability to a setup fault: cooler rear tyres may have less grip until they come up to temperature. Confirm the symptom on stabilized, comparable laps before changing rear settings.

- **Understeer:** steering demand rises without a matching yaw response or lateral acceleration. Check front tyre temperatures/pressure, front camber, entry speed, brake release, front/rear roll stiffness, and aero balance. Test one relevant adjustment at a time; do not automatically add front grip by changing several parameters together.
- **Entry oversteer:** yaw response grows during braking/turn-in. Check brake release, `Brake_Bias_Setting` where available, rear tyre condition, rear stability, and deceleration. A brake-bias change can improve entry stability but may increase front lock risk.
- **Exit oversteer / traction loss:** yaw grows or rear wheel slip rises as throttle is applied. Compare `Throttle_Pos`, `Wheel_Slip_Ratio_*`, `Wheel_Speed_*`, `Wheel_Load_*` (where available), `TCS_In_Action`/`TCS_Active`, and `Diff_Power_Raw`/`Diff_Coast_Raw` (Evo Live). Possible tests include a more conservative differential power setting, a small rear grip/load adjustment, or a less aggressive throttle application. Verify the setup control exists and its direction for that car.
- **Power understeer:** steering demand remains high as throttle is added. Check front tyre condition, differential behavior, throttle timing, and front/rear balance; avoid assuming a front-camber change is the only remedy.

The steering/yaw relationship depends on speed, corner radius, steering ratio, and sign conventions. Compare like-for-like corners and laps rather than using one global threshold.

### AMS2 phase-based setup hypotheses

The supplied AMS2 memo suggests the following starting directions. Use them only when the car exposes that setup control and its effect is confirmed for the selected car. Telemetry can identify the phase and symptom, but the mapping does not contain spring, anti-roll-bar, or low-speed damper settings. Change one control at a time; the memo's paired suggestions are not a reason to change several controls simultaneously.

| Phase and observed balance | Memo's candidate test | Validate with |
| --- | --- | --- |
| Entry understeer | Small reduction in front spring stiffness or low-speed damping | Steering/yaw response during turn-in, brake release, front tyre behavior |
| Entry oversteer | Small increase in front spring stiffness or low-speed damping | Rear stability, brake release, front lock/ABS activity |
| Mid-corner understeer | Soften front anti-roll bar or stiffen rear anti-roll bar | Steady-corner steering/yaw response and inside/outside tyre behavior |
| Mid-corner oversteer | Stiffen front anti-roll bar or soften rear anti-roll bar | Steady-corner yaw and rear slip |
| Exit understeer | Test a small increase in rear spring stiffness or low-speed damping | Throttle application, rear slip, yaw, and whether the symptom is actually power understeer |
| Exit oversteer | Test a small reduction in rear spring stiffness or low-speed damping | Rear slip/yaw during throttle pickup and traction-control activity |

These directions are source-specific heuristics, not universal suspension laws. In particular, springs and dampers are different controls with different effects; test them separately. If a change worsens another phase or reduces tyre contact/traction, revert it.

### Wheel slip, traction control, and wheel speeds

Compare `Wheel_Slip_Ratio_FL/FR/RL/RR`, `Wheel_Slip_*`, `Tyre_Slip_Speed_*`, `Wheel_Speed_*`, `Vehicle_Speed`, `Wheel_Angular_Speed_*`, `TCS_In_Action`, `ABS_In_Action`, `TCS_Active`, and `ABS_Active` where available.

- A repeatable driven-wheel slip increase under throttle supports a traction-loss hypothesis; check throttle ramp, tyre condition/pressure, load transfer, differential behavior, and surface before changing setup.
- Repeated ABS/lock activity under braking supports checking brake input, bias, tyre condition, and brake temperatures. Wheel-speed comparison is only meaningful with correct units and comparable vehicle speed.
- A single wheel-speed or slip outlier can be caused by kerbs, wheel lift, low grip, a sensor/channel mismatch, or a transient. Corroborate it with G, steering, pedal input, and corner location.

### Suspension, damping, ride height, and bumpstops

- Compare `Damper_Velocity_*` distributions by corner and direction, `Suspension_Travel_*`, `Ride_Height_*`, and `Bumpstop_Travel_*` where present. Repeated bumpstop engagement or travel near a known limit can justify checking ride height, spring/bumpstop settings, and platform control.
- If one damper's velocity distribution is persistently asymmetric or unlike its axle/corner counterpart, inspect the damper, sensor sign/zero, kerb exposure, and track direction. The mapping does not provide damper force, so high damper velocity alone does **not** prove damping is too soft or too hard.
- Compare suspension behavior at similar speed, braking, lateral G, and track location. A change in spring/damper setting affects more than one phase; validate comfort, contact, and handling after each test.
- The memo also suggests reducing low-speed compression at an overheating wheel and increasing low-speed rebound at the opposite wheels, with the reverse for a cold wheel. This cross-wheel damping rule is highly setup- and load-dependent: tyre temperature alone cannot establish damper force or direction. If tested, confirm the simulator's compression/rebound convention and change one control at a time while checking wheel load, travel, damper velocity, and the same corner on repeat laps.
- The `Rake` virtual channel is front axle average ride height minus rear axle average ride height in mm. Track it against speed, suspension travel, and aero/handling behavior. The sign is a defined arithmetic convention, not a universal “good” direction. Ride-height channels differ by profile; check availability before using the comparison.

### Braking and brake balance

Compare `Brake_Pos`, `Brake_Bias_Setting`, `Brake_Temp_*`, `Brake_Torque_*` (Evo), `ABS_In_Action`, `Wheel_Slip_Ratio_*`, `G_Long`, and `Vehicle_Speed` during similar braking events.

- Repeated front lock/ABS with stable rear behavior may support a small rearward bias test; repeated rear instability/lock may support a small forward bias test. Use the car's supported increments and re-check both ends of the car.
- The supplied memo suggests 83% brake pressure as a personal baseline. Do not treat that as a general target: this mapping's `Brake_Pos` is pedal input, not maximum brake-pressure setup, so it cannot confirm or prescribe that setting.
- Brake temperature/torque imbalance can also come from corner workload, ducting, dragging brakes, compounds, or driver inputs. Temperature alone is not a direct measurement of brake bias.
- Lower braking deceleration at the same pedal input can have several causes. Check speed, surface, ABS activity, tyre state, and event comparability before changing bias.

### Aero and high-speed platform

Use `Aero_Balance` (MoTeC), `Wing_Front`/`Wing_Rear` where available, `Ride_Height_*`, `Rake`, `Suspension_Travel_*`, `Vehicle_Speed`, and `G_Lat`/`G_Vert`.

- Compare fast-corner balance and platform behavior across speed bands. If balance changes substantially with speed, inspect aero balance, ride heights/rake, and suspension control as competing causes.
- A wing change trades straight-line speed for cornering stability/grip. Change one end by an available increment and compare similar corners and straight-line speed.
- The supplied memo's practical test is to reduce rear wing in small steps after establishing a stable baseline, stopping and reverting when repeatable high-speed rear instability appears. Compare lap time, straight-line speed, and fast-corner behavior together; do not trade away more stability than the lap-time gain justifies.
- The mapping does not contain a universal aero map, ride-height target, or validated aero-stall threshold. Do not diagnose stall from low travel or rake alone.

## Pro: evidence-weighted investigation

These analyses need clean synchronization, adequate samples, profile-specific knowledge, and usually a known setup snapshot. They are not safe as single-channel automatic prescriptions.

### Multi-signal event analysis

Segment data into braking, turn-in, steady corner, and exit using time/distance, `Brake_Pos`, `Throttle_Pos`, `Steering_Angle`, `G_Lat`, `G_Long`, `Yaw_Rate`, `Vehicle_Speed`, and lap/sector context. Compare the same event over several valid laps. A credible finding should state:

- the observed symptom and phase;
- the measured evidence and sample/lap count;
- at least two plausible causes when the signals do not isolate one;
- the smallest setup change to test;
- channels/events that should improve, and checks that would disprove the hypothesis.

Use confidence/data quality as a measure of evidence coverage and repeatability, not as a probability that the setup change will work.

### Tyre/load and balance investigation

Correlate inner/middle/outer and core/carcass/tread/layer temperatures with pressure, camber, wear, wheel load, slip, lateral G, speed, and corner direction. Look for consistent corner-specific patterns and separate warm-up from stabilized running. The mapping includes `Tyre_Core_Temp_*`, `Tyre_Carcass_Temp_*`, `Tyre_Tread_Temp_*`, `Tyre_Layer_Temp_*`, `Tyre_Grip_*`, `Tyre_Wear_*`, and `Wheel_Load_*` for selected profile/source combinations; coverage is not uniform.

Use cross-corner patterns to distinguish a whole-axle issue from one wheel, but account for track direction, asymmetric circuits, temperature-channel orientation, and different tyre load histories. This telemetry does not supply a universal optimum temperature spread or tyre load curve.

### Differential and power delivery

Where present, combine `Diff_Power_Raw`, `Diff_Coast_Raw`, `LSD_Locking_Torque`, rear wheel slip/speeds, throttle, yaw, and corner phase. The supplied AMS2 memo proposes testing less coast/deceleration locking for entry understeer and more for entry oversteer; for exit understeer it proposes less power locking, and for exit oversteer the reverse. Treat these only as hypotheses: differential terminology and control direction differ by car, and the mapping's raw telemetry is not necessarily the setup value. Confirm the control definition, permitted range, and expected direction in the car's setup screen before testing. Do not apply a blanket “maximum preload” rule; use the symptom, supported increments, and a repeatable before/after comparison.

### Brake, thermal, and component-health warnings

Trend `Brake_Temp_*`, `Brake_Torque_*`, `Disc_Life_*`, `Pad_Life_*`, `Engine_Oil_Temp`, `Engine_Water_Temp`, pressure channels, `Clutch_Temp`, `Clutch_Wear`, and damage channels over a session. Flag sustained or rapidly worsening values for inspection against the simulator/car's own limits. The mapping contains no common safe limits, so do not invent numeric redlines or convert a thermal warning into a setup recommendation without validated limits.

### Aero, weather, and stint effects

For high-speed comparisons, correlate `Aero_Balance`, ride height/rake, suspension movement, speed, G, wind, air density, ambient/track temperature, rain, fuel load, and lap time. Separate weather/track evolution and fuel-mass changes from setup effects. `Air_Density` and wind can change aero behavior; fuel channels and lap progression can change vehicle mass. Without a controlled comparison, report competing explanations rather than a definitive wing or ride-height change.

### Powertrain, gearing, and energy strategy

- Compare `Engine_RPM`, `Max_RPM`, `Gear`, `Gear_Count`, `Vehicle_Speed`, `Change_Up_RPM`, `Change_Down_RPM`, and `Gear_RPM_Window` to identify apparent shift/gear-use issues. The mapping does not contain gear ratios, final-drive setup ranges, or a universal shift target; verify car-specific limits before proposing gearing changes.
- Review `Engine_Torque`, `Current_BHP`, turbo/boost channels, `ERS_*`, and `KERS_*` alongside throttle, gear, and lap phase. Treat these primarily as delivery/strategy diagnostics; source scales and available controls vary.
- The memo suggests reducing radiator and brake-duct openings for speed, using about 110 C as an engine-temperature reference, and dry-weather ABS/TC levels around 2-3. These are not universal targets and the corresponding setup controls/limits are not established by this mapping. Reduce cooling only within validated car-specific limits, and tune ABS/TC to the car, conditions, and observed lock/slip rather than copying those numbers.
- Use fuel-per-lap and remaining-fuel aliases for stint planning, not as a substitute for race rules or a validated fuel model.
- Treat `RPM_Limiter_On`, engine running/ignition states, oil/water pressure and temperature, and damage channels as operational warnings. Follow the car's documented limits.

## What the mapping cannot tell us

The mapping is an inventory and unit-conversion layer. By itself it does not provide:

- the actual setup values, allowed ranges, or click increments for each car;
- spring, anti-roll-bar, brake-pressure, brake-duct, radiator-opening, and damper-setting values (the telemetry aliases do not represent those setup controls);
- universal tyre pressure/temperature targets, suspension targets, or component safety limits;
- guaranteed availability of every alias in every profile/source;
- complete definitions or scales for every `native`, enum, bitmask, slip, wear, and raw differential value;
- tyre construction, alignment/toe settings, damper force curves, gear ratios, or an aero map;
- proof that a correlation caused the observed behavior.

Before turning this guide into automatic prescriptions, validate channel presence and units, add car-specific setup limits/steps and threshold sources, and test each rule against representative clean logs. Missing inputs should result in “insufficient data,” not a guessed recommendation.

## Alias coverage at a glance

The mapping has 453 unique aliases and one virtual value (`Rake`). The available data falls into these practical analysis families:

| Family | Example aliases |
| --- | --- |
| Tyres, pressures, temperatures, wear, grip, slip, and wheel load | `Tyre_Pressure_*`, `Tyre_Temp_*`, `Tyre_Core_Temp_*`, `Tyre_Wear_*`, `Tyre_Grip_*`, `Wheel_Slip_Ratio_*`, `Wheel_Load_*` |
| Suspension, geometry, and platform | `Camber_*`, `Damper_Velocity_*`, `Suspension_Travel_*`, `Ride_Height_*`, `Rake`, `Bumpstop_Travel_*`, `CG_Height` |
| Driver inputs, motion, and handling | `Throttle_Pos`, `Brake_Pos`, `Steering_Input`, `Steering_Angle`, `G_Lat`, `G_Long`, `Yaw_Rate`, `Velocity_*`, `Orientation_*` |
| Brakes, controls, and differential | `Brake_Bias_Setting`, `Brake_Temp_*`, `Brake_Torque_*`, `ABS_*`, `TCS_*`, `Diff_*`, `LSD_Locking_Torque` |
| Aero, environment, and track | `Aero_Balance`, `Wing_Front`, `Wing_Rear`, `Air_Density`, `Wind_Speed`, `Track_Temp`, `Rain_Density`, `Track_Length`, GPS aliases |
| Engine, transmission, and energy | `Engine_RPM`, `Engine_Torque`, oil/water channels, boost aliases, `Gear`, `ERS_*`, `KERS_*`, `Clutch_*` |
| Fuel, session, timing, and race context | `Fuel_*`, `Lap_*`, sector times, `Race_Status`, `Session_*`, flags, participant/gap aliases |
| Health, damage, and runtime state | `*_Damage_*`, `Disc_Life_*`, `Pad_Life_*`, `Car_Damage_*`, `Packet_ID_*`, `Player_*`, ignition/engine states |

Use the mapping file as the authority for exact profile/source/channel/unit coverage. A channel listed in this table may still be unavailable in the currently loaded log.
