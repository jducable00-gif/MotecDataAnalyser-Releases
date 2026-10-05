# Table de correspondance des canaux — AC EVO (Evora M1) ↔ AMS2

Comparaison des deux listes de canaux :

| Fichier | Véhicule / Sim | Canaux uniques |
|---------|----------------|----------------|
| `AC_EVO_channels.md` | Lotus Evora M1 (Assetto Corsa, profil `EvoM1`) | 66 |
| `AMS2_channels.md` | Assetto Corsa Motorsport 2 | 99 |

Légende des remarques :

- **Unités différentes** : même grandeur physique, unités différentes (voir conversions en bas de page).
- **Probable** : correspondance supposée, à vérifier sur les données réelles.
- **EVO seul** : canal absent d'AMS2.
- **AMS2 seul** : canal absent d'AC EVO.

## Correspondance

| Catégorie | Grandeur | AC EVO (Evora M1) | Unité EVO | AMS2 | Unité AMS2 | Remarques |
|-----------|----------|-------------------|-----------|------|------------|-----------|
| Lap | Beacon | LAP_BEACON | .. | Beacon | — | |
| Lap | Beacon BR2 | — | — | BR2 Beacon Number | — | AMS2 seul |
| Lap | Numéro de tour | — | — | Lap Number | — | AMS2 seul |
| Lap | Temps du tour | — | — | Lap Time | s | AMS2 seul |
| Lap | Temps intermédiaire | — | — | Split Time | s | AMS2 seul |
| Lap | Statut de course | — | — | Race Status | — | AMS2 seul |
| Moteur | Taux de régime | RPMS | 1/min | Engine RPM | rpm | |
| Moteur | Rapport | GEAR | no | Gear | — | |
| Moteur | Couple moteur | current_torque | Nm | Engine Torque | Nm | |
| Moteur | Puissance moteur | current_bhp | bhp | Engine Power | bhp | EVO seul |
| Moteur | Pression turbo | turbo_boost | bar | Turbo Boost Pressure | kPa | Unités différentes (×100) |
| Moteur | Temp. huile | oil_temperature_c | C | Eng Oil Temp | C | |
| Moteur | Pression huile | oil_pressure_bar | bar | Eng Oil Pres | kPa | Unités différentes (×100) |
| Moteur | Temp. eau | water_temperature_c | C | Eng Water Temp | C | |
| Moteur | Pression eau | water_pressure_bar | bar | Eng Water Pres | kPa | Unités différentes (×100) |
| Moteur | Pression carburant | fuel_pressure_bar | bar | Fuel Pres | kPa | Unités différentes (×100) |
| Moteur | Niveau carburant | FUEL | L | Fuel Level | l | |
| Moteur | Capacité carburant | max_fuel | L | Fuel Capacity | l | |
| Commandes | Accélérateur | THROTTLE | % | Throttle Pos | % | |
| Commandes | Frein | BRAKE | % | Brake Pos | % | |
| Commandes | Embrayage | CLUTCH | % | Clutch Pos | % | |
| Commandes | Volant | STEERANGLE | deg | Steering Pos | % | Unités différentes (degrés vs %) |
| Vitesse | Vitesse au sol | SPEED | km/h | Ground Speed | kph | |
| Vitesse | Latitude GPS | — | — | GPS Latitude | deg | AMS2 seul |
| Vitesse | Longitude GPS | — | — | GPS Longitude | deg | AMS2 seul |
| Vitesse | Vitesse X | velocity[0] | m/s | Velocity X | m/s | |
| Vitesse | Vitesse Y | velocity[1] | m/s | Velocity Y | m/s | |
| Vitesse | Vitesse Z | velocity[2] | m/s | Velocity Z | m/s | |
| Vitesse | Position X | — | — | Position X | m | AMS2 seul |
| Vitesse | Position Y | — | — | Position Y | m | AMS2 seul |
| Vitesse | Position Z | — | — | Position Z | m | AMS2 seul |
| Vitesse | Orientation X | pitch | deg | Orientation X | deg | Probable |
| Vitesse | Orientation Y | heading | deg | Orientation Y | deg | Probable |
| Vitesse | Orientation Z | roll | deg | Orientation Z | deg | Probable |
| Vitesse | Altitude | — | — | Altitude | m | AMS2 seul |
| Accélérations | G latéral | accG[1] | G | G Force Lat | G | |
| Accélérations | G longitudinal | accG[0] | G | G Force Long | G | |
| Accélérations | G vertical | accG[2] | G | G Force Vert | G | |
| Accélérations | Taux de lacet (yaw) | localAngularVel[1] | rad/s | Angular Velocity Y | rad/s | |
| Accélérations | Vitesse angulaire X | localAngularVel[0] | rad/s | Angular Velocity X | rad/s | |
| Accélérations | Vitesse angulaire Z | localAngularVel[2] | rad/s | Angular Velocity Z | rad/s | |
| Suspension | Course suspension FL | SUS_TRAVEL_LF | mm | Susp Pos FL | cm | Unités différentes (÷10) |
| Suspension | Course suspension FR | SUS_TRAVEL_RF | mm | Susp Pos FR | cm | Unités différentes (÷10) |
| Suspension | Course suspension RL | SUS_TRAVEL_LR | mm | Susp Pos RL | cm | Unités différentes (÷10) |
| Suspension | Course suspension RR | SUS_TRAVEL_RR | mm | Susp Pos RR | cm | Unités différentes (÷10) |
| Suspension | Vitesse suspension FL | — | — | Susp Vel FL | m/s | |
| Suspension | Vitesse suspension FR | — | — | Susp Vel FR | m/s | |
| Suspension | Vitesse suspension RL | — | — | Susp Vel RL | m/s | |
| Suspension | Vitesse suspension RR | — | — | Susp Vel RR | m/s | |
| Suspension | Bumpstop FL | BUMPSTOPUP_TRAVEL_LF | mm | — | — | EVO seul |
| Suspension | Bumpstop FR | BUMPSTOPUP_TRAVEL_RF | mm | — | — | EVO seul |
| Suspension | Bumpstop RL | BUMPSTOPUP_TRAVEL_LR | mm | — | — | EVO seul |
| Suspension | Bumpstop RR | BUMPSTOPUP_TRAVEL_RR | mm | — | — | EVO seul |
| Pneus | Temp. pneu FL | TYRE_TAIR_LF | C | Tyre Temp FL | C | |
| Pneus | Temp. pneu FR | TYRE_TAIR_RF | C | Tyre Temp FR | C | |
| Pneus | Temp. pneu RL | TYRE_TAIR_LR | C | Tyre Temp RL | C | |
| Pneus | Temp. pneu RR | TYRE_TAIR_RR | C | Tyre Temp RR | C | |
| Pneus | Temp. pneu FL externe | L_SURF_TEMP_LF | C | Tyre Temp FL Outer | C | |
| Pneus | Temp. pneu FL centre | C_SURF_TEMP_LF | C | Tyre Temp FL Centre | C | |
| Pneus | Temp. pneu FL interne | R_SURF_TEMP_LF | C | Tyre Temp FL Inner | C | |
| Pneus | Temp. pneu FR externe | L_SURF_TEMP_RF | C | Tyre Temp FR Outer | C | |
| Pneus | Temp. pneu FR centre | C_SURF_TEMP_RF | C | Tyre Temp FR Centre | C | |
| Pneus | Temp. pneu FR interne | R_SURF_TEMP_RF | C | Tyre Temp FR Inner | C | |
| Pneus | Temp. pneu RL externe | L_SURF_TEMP_LR | C | Tyre Temp RL Outer | C | |
| Pneus | Temp. pneu RL centre | C_SURF_TEMP_LR | C | Tyre Temp RL Centre | C | |
| Pneus | Temp. pneu RL interne | R_SURF_TEMP_LR | C | Tyre Temp RL Inner | C | |
| Pneus | Temp. pneu RR externe | L_SURF_TEMP_RR | C | Tyre Temp RR Outer | C | |
| Pneus | Temp. pneu RR centre | C_SURF_TEMP_RR | C | Tyre Temp RR Centre | C | |
| Pneus | Temp. pneu RR interne | R_SURF_TEMP_RR | C | Tyre Temp RR Inner | C | |
| Pneus | Pression pneu FL | TYRE_PRESS_LF | psi | Tyre Pressure FL | kPa | Unités différentes (×6,895) |
| Pneus | Pression pneu FR | TYRE_PRESS_RF | psi | Tyre Pressure FR | kPa | Unités différentes (×6,895) |
| Pneus | Pression pneu RL | TYRE_PRESS_LR | psi | Tyre Pressure RL | kPa | Unités différentes (×6,895) |
| Pneus | Pression pneu RR | TYRE_PRESS_RR | psi | Tyre Pressure RR | kPa | Unités différentes (×6,895) |
| Pneus | Vitesse roue FL | WHEEL_SPEED_LF | m/s | Tyre Speed FL | rad/s | Unités différentes (v = ω·r) |
| Pneus | Vitesse roue FR | WHEEL_SPEED_RF | m/s | Tyre Speed FR | rad/s | Unités différentes (v = ω·r) |
| Pneus | Vitesse roue RL | WHEEL_SPEED_LR | m/s | Tyre Speed RL | rad/s | Unités différentes (v = ω·r) |
| Pneus | Vitesse roue RR | WHEEL_SPEED_RR | m/s | Tyre Speed RR | rad/s | Unités différentes (v = ω·r) |
| Pneus | Glissement roue FL | WHEEL_SR_LF | .. | — | — | EVO seul |
| Pneus | Glissement roue FR | WHEEL_SR_RF | .. | — | — | EVO seul |
| Pneus | Glissement roue RL | WHEEL_SR_LR | .. | — | — | EVO seul |
| Pneus | Glissement roue RR | WHEEL_SR_RR | .. | — | — | EVO seul |
| Pneus | Charge pneu FL | wheelLoad[0] | N | Tyre Load FL | N | EVO seul |
| Pneus | Charge pneu FR | wheelLoad[1] | N | Tyre Load FR | N | EVO seul |
| Pneus | Charge pneu RL | wheelLoad[2] | N | Tyre Load RL | N | EVO seul |
| Pneus | Charge pneu RR | wheelLoad[3] | N | Tyre Load RR | N | EVO seul |
| Pneus | Usure pneu FL | tyreWear[0] | % | Tyre Wear FL | % | EVO seul |
| Pneus | Usure pneu FR | tyreWear[1] | % | Tyre Wear FR | % | EVO seul |
| Pneus | Usure pneu RL | tyreWear[2] | % | Tyre Wear RL | % | EVO seul |
| Pneus | Usure pneu RR | tyreWear[3] | % | Tyre Wear RR | % | EVO seul |
| Pneus | Carrossage FL | camberRAD[0] | rad | Camber FL | deg | Unités différentes (×57,3) |
| Pneus | Carrossage FR | camberRAD[1] | rad | Camber FR | deg | Unités différentes (×57,3) |
| Pneus | Carrossage RL | camberRAD[2] | rad | Camber RL | deg | Unités différentes (×57,3) |
| Pneus | Carrossage RR | camberRAD[3] | rad | Camber RR | deg | Unités différentes (×57,3) |
| Pneus | Déplacement Y pneu FL | — | — | Tyre Y FL | m | |
| Pneus | Déplacement Y pneu FR | — | — | Tyre Y FR | m | |
| Pneus | Déplacement Y pneu RL | — | — | Tyre Y RL | m | |
| Pneus | Déplacement Y pneu RR | — | — | Tyre Y RR | m | |
| Pneus | Hauteur pneu au sol FL | — | — | Tyre Height Above Ground FL | m | |
| Pneus | Hauteur pneu au sol FR | — | — | Tyre Height Above Ground FR | m | |
| Pneus | Hauteur pneu au sol RL | — | — | Tyre Height Above Ground RL | m | |
| Pneus | Hauteur pneu au sol RR | — | — | Tyre Height Above Ground RR | m | |
| Freins | Temp. frein FL | brakeTemp[0] | C | Brake Temp FL | C | |
| Freins | Temp. frein FR | brakeTemp[1] | C | Brake Temp FR | C | |
| Freins | Temp. frein RL | brakeTemp[2] | C | Brake Temp RL | C | |
| Freins | Temp. frein RR | brakeTemp[3] | C | Brake Temp RR | C | |
| Freins | Couple frein FL | brakeTorque[0] | Nm | — | — | EVO seul |
| Freins | Couple frein FR | brakeTorque[1] | Nm | — | — | EVO seul |
| Freins | Couple frein RL | brakeTorque[2] | Nm | — | — | EVO seul |
| Freins | Couple frein RR | brakeTorque[3] | Nm | — | — | EVO seul |
| Freins | Répartition freinage | brakeBias | % | Brake Bias Setting | % | |
| Freins | Usure plaquettes FL | padLife[0] | % | — | — | EVO seul |
| Freins | Usure plaquettes FR | padLife[1] | % | — | — | EVO seul |
| Freins | Usure plaquettes RL | padLife[2] | % | — | — | EVO seul |
| Freins | Usure plaquettes RR | padLife[3] | % | — | — | EVO seul |
| Freins | Usure disques FL | discLife[0] | % | — | — | EVO seul |
| Freins | Usure disques FR | discLife[1] | % | — | — | EVO seul |
| Freins | Usure disques RL | discLife[2] | % | — | — | EVO seul |
| Freins | Usure disques RR | discLife[3] | % | — | — | EVO seul |
| Aérodynamique | Hauteur de caisse FL | AERO_HEIGHT0 | mm | Ride Height FL | cm | Probable (AERO_HEIGHT0..3 ↔ FL/FR/RL/RR), unités différentes |
| Aérodynamique | Hauteur de caisse FR | AERO_HEIGHT1 | mm | Ride Height FR | cm | Probable, unités différentes |
| Aérodynamique | Hauteur de caisse RL | AERO_HEIGHT2 | mm | Ride Height RL | cm | Probable, unités différentes |
| Aérodynamique | Hauteur de caisse RR | AERO_HEIGHT3 | mm | Ride Height RR | cm | Probable, unités différentes |
| Aérodynamique | Répartition aéro | AERO_BALANCE | % | — | — | EVO seul (pas d'équivalent AMS2) |
| Transmission | Couple différentiel | LSD_LOCKING_TORQUE | N | — | — | EVO seul |
| Environnement | Temp. air | AIR_TEMP | C | Ambient Temp | C | |
| Environnement | Temp. route | ROAD_TEMP | C | Track Temp | C | |
| Systèmes | ABS actif | absInAction | bool | ABS Active | bool | |
| Systèmes | Antipatinage actif | tcinAction | bool | TCS Active | bool | |
| Systèmes | ASM actif | — | — | ASM Active | — | AMS2 seul |
| Systèmes | DRS disponible | drsAvailable | bool | DRS Available | bool | |
| Systèmes | DRS actif | is_drs_open | bool | DRS Active | bool | |
| Systèmes | ERS actif | ersIsCharging | bool | ERS Active | int | Probable (Mode) |
| Systèmes | Boost actif | — | — | Boost Active | — | AMS2 seul |
| Systèmes | Boost disponible | — | — | Boost Available | % | AMS2 seul |
| Systèmes | Niveau ABS | abs_level | int | ABS Setting | int | |
| Systèmes | Niveau TC | tc_level | int | TC Setting | int | |
| Systèmes | Niveau ESC | esc_level | int | — | — | EVO seul |
| Systèmes | Niveau EBB | ebb_level | int | — | — | EVO seul |
| Systèmes | Map moteur | engine_map_level | int | — | — | EVO seul |
| Systèmes | Niveau Turbo | turbo_level | float | — | — | EVO seul |
| Systèmes | Map déploiement ERS | ers_deployment_map | int | ERS Mode | int | |
| Systèmes | Map recharge ERS | ers_recharge_map | float | — | — | EVO seul |
| Systèmes | Mode Overtake ERS | is_ers_overtake_mode_on | bool | — | — | EVO seul |
| Systèmes | Différence puissance | diff_power_level | int | — | — | EVO seul |
| Systèmes | Différence freinage | diff_coast_level | int | — | — | EVO seul |
| Systèmes | Mode performance | active_performance_mode | int | — | — | EVO seul |

## Canaux Calculés (Math Channels)

Cette section définit les canaux qui ne sont pas fournis nativement par le shared memory mais qui peuvent être calculés pour une analyse type MoTeC. Certains nécessitent des données d'entrée manuelles (Masse, Empattement, Raideur ressorts).

### 1. Dynamique & Grip
| Canal Calculé | Formule / Logique | Sources AC EVO | Sources AMS2 | Utilité Analyse |
| :--- | :--- | :--- | :--- | :--- |
| **Combined Grip** | $\sqrt{G_{lat}^2 + G_{long}^2}$ | `accG[0]`, `accG[1]` | `G Force X`, `G Force Y` | Utilisation du cercle de friction (G-G Diagram) |
| **Slip Ratio** | $\frac{(\text{Vitesse Roue} \times \text{Rayon}) - \text{Vitesse Voiture}}{\text{Vitesse Voiture}}$ | `wheelSpeed[0..3]`, `speedKmh` | `Wheel Speed`, `mSpeed` | Analyse de la traction et blocage frein |
| **Slip Angle** | $\arctan\left(\frac{\text{Vitesse Latérale}}{\text{Vitesse Longitudinale}}\right)$ | `accG[1]` (Lat), `speedKmh` | `G Force Y`, `mSpeed` | Détection sous-virage / survirage |
| **Wheelspin %** | $\frac{\text{Vitesse Roue Arrière} - \text{Vitesse Voiture}}{\text{Vitesse Voiture}} \times 100$ | `wheelSpeed[2..3]`, `speedKmh` | `Wheel Speed`, `mSpeed` | Quantification de la perte d'adhérence |

### 2. Châssis & Aérodynamique
| Canal Calculé | Formule / Logique | Sources AC EVO | Sources AMS2 | Utilité Analyse |
| :--- | :--- | :--- | :--- | :--- |
| **Rake Relatif** | $\text{Moyenne(Travel Front)} - \text{Moyenne(Travel Rear)}$ | `suspTravel[0..3]` | `Susp Vel` (Intégration) / `Position Z` | Équilibre aérodynamique et plongée |
| **Transfert de Masse** | $\frac{\text{Masse} \times \text{Accél} \times \text{H}_{CG}}{\text{Empattement/Voie}}$ | `accG[0..2]` | `G Force X/Y/Z` | Analyse du grip et transfert de charge |
| **Tyre Load Est.** | $\text{Position Suspension} \times \text{Raideur Ressort (k)}$ | `suspTravel[0..3]` | `Susp Vel` / `Position Z` | Estimation de la charge verticale par pneu |
| **Dynamic Pressure** | $\frac{1}{2} \rho v^2$ | `speedKmh` | `mSpeed` | Corrélation charge aéro vs vitesse |

### 3. Inputs Pilote & Qualité
| Canal Calculé | Formule / Logique | Sources AC EVO | Sources AMS2 | Utilité Analyse |
| :--- | :--- | :--- | :--- | :--- |
| **Steering Rate** | $\frac{\Delta \text{Angle Volant}}{\Delta t}$ | `steeringWheel` | `Steering Angle` | Détection du "sawing" et instabilité |
| **Brake Gradient** | $\frac{\Delta \text{Pression Frein}}{\Delta t}$ | `brakePressure` | `Brake Pressure` | Analyse du trail-braking et impact entrée |
| **Brake Bias Eff.** | $\frac{\text{Décél. Réelle}}{\text{Pression Frein Totale}}$ | `accG[0]`, `brakePressure` | `G Force X`, `Brake Pressure` | Optimisation du répartiteur de freinage |

## Résumé

| | AC EVO (Evora M1) | AMS2 |
|---|---|---|
| Canaux uniques | 98 | 99 |
| Canaux avec équivalent direct | 65 | 65 |
| Canaux sans équivalent | 33 | 34 |

Canaux **EVO seuls** (33) : `BUMPSTOPUP_TRAVEL_*` (4), `WHEEL_SR_*` (4), `BRAKE_TORQUE_*` (4), `padLife[4]` (4), `discLife[4]` (4), `wheelLoad[4]` (4), `tyreWear[4]` (4), `camberRAD[4]` (4), `AERO_BALANCE`, `LSD_LOCKING_TORQUE`, et certains réglages électroniques spécifiques.

Canaux **AMS2 seuls** (34) : `BR2 Beacon Number`, `Lap Number`, `Lap Time`, `Split Time`, `Race Status`, `GPS Latitude`, `GPS Longitude`, `Position X/Y/Z` (3), `Altitude`, `G Force Vert`, `Angular Velocity X/Z` (2), `ASM Active`, `Boost Active`, `Boost Available`.

## Conversions d'unités utiles

| Grandeur | EVO → AMS2 |
|----------|------------|
| Pression pneu | psi × 6,895 = kPa |
| Vitesse roue | m/s ÷ rayon de roue = rad/s |
| Carrossage | rad × 57,3 = deg |
| Volant | deg ↔ % (selon démultiplication direction) |
