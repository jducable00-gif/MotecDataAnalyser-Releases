# Analyseur de Configuration MoTeC

Un outil de bureau qui lit la télémétrie MoTeC et la transforme en un **rapport de configuration explicable** : statistiques par tour, diagnostics pneus/thermiques/suspension, et recommandations avec score de confiance (symptôme → causes pondérées → ajustement suggéré + canaux de validation).

Les rapports peuvent être générés en **Anglais** ou en **Français**. Les noms des canaux sont normalisés automatiquement pour deux profils de véhicule : **Evo** (Lotus Evora M1 / Assetto Corsa) et **AMS2** (Assetto Corsa Motorsport 2, repli générique).

---

## Mise en route

Vous avez trois façons d'exécuter l'application. Elles fonctionnent toutes de la même manière une fois lancées — chaque fichier que l'application lit ou écrit se trouve **à côté de l'exécutable**.

| Option | Ce dont vous avez besoin | Emplacement d'exécution |
|--------|--------------------------|-------------------------|
| **Package portable** (recommandé) | Le dossier `MotecDataAnalyser_Portable\` produit par `package.ps1`. Copiez tout le dossier n'importe où et lancez `MotecDataAnalyser.exe`. Aucune installation, aucun C++ Builder requis. | N'importe où — autonome |
| **Sortie de build** | Un build réussi (`build.ps1`). Lancez `Win32\Debug\Project1.exe` (ou votre config/plateforme choisie). | `Win32\<Config>\` |
| **IDE** | Ouvrez `Project1.cbproj`, ciblez **Win32**, configuration **Debug**, puis Build / Compile. | Dossier de sortie géré par l'IDE |

> Le package portable inclut les cinq packages d'exécution (`rtl`, `vcl`, `vclwinx`, `vclimg`, `bindengine`) sous forme de fichiers `.bpl` à côté de l'exe, ce qui permet de le lancer sur une machine propre sans C++ Builder installé.

Lors du premier lancement, l'application crée son fichier de paramètres à côté d'elle :
- `MotecDataAnalyser.ini` — derniers dossiers + options UI (langue, profil, rapport complet).
- `MotecDataAnalyser.thresholds.ini` — tous les seuils d'analyse (créé avec les valeurs par défaut s'il est absent).

---

## Chargement d'un fichier de données

La fenêtre principale dispose d'un bouton d'importation et de deux cases à cocher optionnelles pour la capture en direct.

### Importer des données MoTeC  →  fichiers `.ld` / `.ldx`
Cliquez sur **Import Motec Data**. Une boîte de dialogue s'ouvre, filtrée sur `*.ld`. Choisissez votre log MoTeC (ou son index) et l'application le décode, normalise les noms de canaux selon le profil sélectionné, lance l'analyse et remplit :

- la zone de rapport à gauche,
- le panneau **température des pneus** (températures intérieur/centre/extérieur par coin + pression),
- le panneau **suspension / vitesse d'amortisseur** (distribution sur 41 bins −400…+400 mm/s par coin, débattement moyen, engagement du bumpstop).

### Fichiers MoTeC CSV  →  `.csv`
Le moteur d'analyse lit également les exports CSV standard de MoTeC. Le bouton dédié **"Load MoTeC CSV"** existe dans l'interface mais est masqué par défaut ; si vous avez besoin de le rendre visible (par exemple pour un build où seules des données CSV sont disponibles), réglez `ButtonLoad->Visible = true` lors de la création du formulaire et cliquez dessus — il ouvre une boîte de dialogue filtrée sur `*.csv`.

### Capture en direct
- **Live (AMS2)** — lit la mémoire partagée d'AMS2 en temps réel ; sélectionne automatiquement le profil *AMS2*.
- **AC EVO Live** — lit les données en direct de l'AC Evora M1 ; sélectionne automatiquement le profil *Evo*.

Les deux sont mutuellement exclusifs (cocher l'un décoche l'autre). La LED rouge devient verte une fois connecté. Pendant qu'une session est active, les boutons d'importation de fichiers sont désactivés jusqu'à ce que vous décochiez la case.

### Options qui persistent automatiquement
Ces contrôles UI s'enregistrent eux-mêmes dans `MotecDataAnalyser.ini` — aucun redémarrage nécessaire :

| Contrôle | Valeurs | Effet |
|---------|--------|--------|
| **Profile** | Evo / AMS2 | Table de normalisation des noms de canaux utilisée pour l'import LD et les rapports. |
| **Language** | English / Français | Langue du rapport (toutes les chaînes de caractères sont localisées). |
| **Full Report** | on/off | Active ou désactive les sections étendues du rapport écrit. |

---

## Lire une recommandation : confiance & qualité des données

Chaque diagnostic dans le rapport est un bloc `Recommendation`, pas un simple verdict. Il contient toujours :

- **MAIN ISSUE / symptôme** — ce qui a été détecté (ex: perte de traction arrière en sortie).
- **OBSERVED DATA** — valeurs concrètes de preuve provenant de votre télémétrie.
- **POSSIBLE CAUSES** — plusieurs causes classées par poids, chacune avec un ajustement et une direction. Une recommandation n'est *jamais* présentée comme certaine lorsque plusieurs causes correspondent aux données.
- **RECOMMENDATION / changement suggéré**.
- **VALIDATION channels** — quels autres canaux vérifier pour confirmer ou exclure une cause.
- **CONFIDENCE + DATA QUALITY** — le niveau de fiabilité (ci-dessous).

### Score de confiance (0 → 1)
`computeConfidence()` combine quatre signaux :

| Signal | Poids | Mesure |
|--------|-------|--------|
| Proximité du seuil | **40 %** | `min(1, (\|value\|/threshold − 1))`. Plus la valeur dépasse la limite, plus ce terme est élevé (plafonné à 1). En dessous du seuil, il contribue à 0. |
| Nombre d'échantillons | **25 %** | `min(1, échantillons / 100)`. Plus il y a d'échantillons de support → plus la confiance est élevée ; sature à 100. |
| Reproductibilité sur les tours | **20 %** | `min(1, toursReproduits / MinLapsForReproducibility)`. Un symptôme observé sur de nombreux tours est plus fiable (par défaut : 3 tours). |
| Couverture des canaux | **15 %** | `canauxDisponibles / canauxNécessaires`. L'absence de canaux requis réduit directement la confiance. |

Les quatre termes sont additionnés et plafonnés à `[0, 1]`. Si aucun canal requis n'est disponible (`canauxDisponibles ≤ 0`), le score est forcé à **0**.

### Niveaux de qualité des données
La confiance numérique est associée à une étiquette via `dataQualityLabel()`, en utilisant les seuils de la section `[Confidence]` du fichier INI (valeurs par défaut affichées) :

| Confiance | Étiquette (EN / FR) | Seuil par défaut |
|------------|-------------------|----------------|
| ≥ 0.85 | HIGH / ÉLEVÉE | `High = 0.85` |
| ≥ 0.60 | MEDIUM / MOYENNE | `Medium = 0.60` |
| ≥ 0.30 | LOW / FAIBLE | `Low = 0.30` |
| < 0.30 | INSUFFICIENT / INSUFFISANTE | — |

Une ligne séparée de **couverture des données** dans le rapport résume également l'exhaustivité de votre ensemble de canaux (ex: "HIGH — most channels available" vs "INSUFFICIENT — very limited data"). Considérez un résultat LOW/INSUFFICIENT comme *indicatif uniquement* et effectuez plus de tours ou corrigez les canaux manquants avant d'agir.

---

## Réglage des seuils

Il existe **deux** fichiers de configuration, tous deux créés à côté de l'exe :

1. `MotecDataAnalyser.thresholds.ini` — limites d'analyse numériques (les valeurs "quand est-ce un problème ?").
2. `MotecDataAnalyser.rules.json` *(optionnel)* — règles de symptômes et poids par cause ; see below.

> **Il n'y a pas d'éditeur de seuils dans l'interface.** Pour modifier une limite, fermez l'application, modifiez le `.ini`, puis redémarrez-la (les changements sont lus une seule fois au démarrage). La section `[SymptomWeights]` du même INI peut écraser les poids individuels des causes provenant du fichier JSON.

### `MotecDataAnalyser.thresholds.ini`
Sections et quelques clés représentatives (toutes les valeurs sont des nombres simples ; unités indiquées) :

| Section | Ce qu'elle contrôle | Exemples de clés |
|---------|----------------------|------------------|
| `[General]` | Échantillons minimum avant qu'une analyse soit significative | `MinDamperSamples`, `MinHandlingSamples`, `MinSteerYawSamples`, `HandlingConfidenceSamples` |
| `[Pressure]` | Limites de pression des pneus (psi) + écart gauche/droite | `LowEvo`, `HighEvo`, `LowAms2`, `HighAms2`, `SideDiffMax` |
| `[TyreTemp]` | Écarts de température (°C), bandes liées au carrossage | `CentreShoulderEvo`, `InnerOuterEvo`, …, `CamberMinSpread`, `CamberMaxSpread` |
| `[LSD]` | Limites de verrouillage / glissement par profil | `HighEvo`, `LowEvo`, `HighAms2`, `LowAms2` |
| `[Damper]` | Vérifications de la distribution de vitesse d'amortisseur (mm/s) | `LowSpeedThreshold`, `AsymmetryThreshold`, `HighSpeedShareMax`, `LowSpeedShareMin`, `LowSpeedStdDevMax` |
| `[Handling]` | Détection de phase + déclencheurs sous/sur-virage | `BrakePedalMin`, `SteerMinCornering`, `LatGMaxUndersteer`, `YawMinOversteer`, … |
| `[Thermal]` | Limites de température des fluides (°C) | `WaterTempMax`, `OilTempMax` |
| `[Suspension]` | Débattement, hauteur de caisse et limites de vitesse | `TravelMax`, `RideHeightDiffMax`, `VelocityMax`, `AvgTravelFrontMax`, `AvgTravelRearMax` |
| `[Brake]` | Vérifications température/bias/torque de freinage | `TempDiffMax`, `BiasMax`, `TorqueMin`, … |
| `[ChannelMismatch]` | Limites de cohérence entre canaux | `PressureMax/Min`, `SuspensionMax/Min` |
| `[TCS]` / `[ABS]` | Caps d'intervention | `ActiveMax` |
| `[WheelSlip]` | Limite de glissement | `Max` |
| `[WheelSpeed]` | Limites d'écart de vitesse des roues | `FrontDiffMax`, `RearDiffMax`, `TempDiffMax` |
| `[Traction]` | Bandes de détection de perte de traction | `RearDiffMax`, `LongGMax`, … |
| `[Bumpstop]` | Seuil d'engagement du bumpstop | `Min` |
| `[Aero]` | Équilibre aéro / sensibilité hauteur de caisse | `BalanceMax`, `HeightDiffMax`, `SpeedMin` |
| `[Steering]` | Corrélation direction/lacet + bandes de vitesse | `CorrUndersteer`, `CorrOversteer`, … |
| `[Stability]` | Score de stabilité et corrélations | `ScoreMin`, `WheelSpeedDiffMax`, … |
| `[Lap]` | Limite d'écart de temps entre tours (s) | `TimeGapMax` |
| `[Confidence]` | Seuils de qualité + tours pour reproductibilité | `Low = 0.30`, `Medium = 0.60`, `High = 0.85`, `MinLapsForReproducibility = 3` |

**Comment régler :** augmentez une limite si vous obtenez trop de faux positifs pour ce symptôme ; baissez-la si de vrais problèmes passent inaperçus. Modifiez une ou deux valeurs à la fois, redémarrez et relancez le même fichier pour comparer. Si vous supprimez `MotecDataAnalyser.thresholds.ini`, l'application le régénère avec les valeurs par défaut au prochain démarrage.

### `[SymptomWeights]` (même INI)
Poids par cause qui écrasent le JSON des règles. Format de clé : `"symptôme|cause"`, valeur un float entre `0..1` :

```ini
[SymptomWeights]
rear_traction_loss_on_exit|excessive_rear_damper_bump = 0.45
front_understeer_entry|low_front_camber = 0.30
```

### Règles personnalisées : `MotecDataAnalyser.rules.json` *(optionnel)*
Pour modifier *quels* symptômes existent, leurs listes de causes ou leurs canaux de validation (plutôt que de simples limites numériques), déposez un fichier nommé **`MotecDataAnalyser.rules.json`** soit à côté de l'exe, soit dans un sous-dossier `config\`. Un modèle prêt à l'emploi est fourni ici :

```
config\MotecDataAnalyser.rules.example.json   (dans le package portable)
MotecDataAnalyser.rules.example.json          (racine du repo, pour les développeurs)
```

Copiez-le dans l'un de ces emplacements et renommez-le sans `.example`. Si aucun fichier de règles n'est trouvé, les valeurs par défaut intégrées sont utilisées. Load order at startup: **rules JSON first**, then `[SymptomWeights]` from the INI overrides individual weights — so the INI always wins on a per-cause basis.

---

## Où tout se trouve (tout est relatif à l'exe)

| Fichier / dossier | Utilité |
|---------------|---------|
| `MotecDataAnalyser.ini` | Derniers dossiers + options UI (auto-sauvegardé). |
| `MotecDataAnalyser.thresholds.ini` | Seuils d'analyse + `[SymptomWeights]`. Modifiez → redémarrez. |
| `config\MotecDataAnalyser.rules.json` *(ou à côté de l'exe)* | Règles de symptômes / poids personnalisés (optionnel). |
| `data\` | Échantillons de télémétrie (CSV, `.ld`, `.ldx`) et docs de référence pour tester l'app. |

---

## Compilation depuis les sources (développeurs)

```powershell
.\scripts\build.ps1         # build incrémental Debug/Win32 -> BUILD OK en cas de succès
.\scripts\build.ps1 -Clean  # efface Win32\Debug d'abord, puis reconstruit
.\scripts\package.ps1       # produit MotecDataAnalyser_Portable\ après un build réussi
```

Consultez `AGENTS.md` pour la chaîne d'outils complète (Embarcadero C++ Builder 12 / Clang VCL), les détails de compilation en ligne de commande et les notes sur le packaging. Ne retargetez pas vers MSVC/MinGW/g++.