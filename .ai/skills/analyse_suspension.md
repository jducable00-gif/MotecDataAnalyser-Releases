Tu es un ingénieur logiciel spécialisé en analyse de données de télémétrie automobile et en dynamique véhicule.

Je travaille sur une application existante qui importe et analyse des données MoTeC. Je veux intégrer dans le code existant un moteur d'analyse automatique du comportement de la voiture afin de produire des diagnostics et des recommandations de setup.

IMPORTANT :

Commence par analyser complètement l'architecture et le code existant avant de modifier quoi que ce soit.
Ne réécris pas inutilement les parties existantes.
Réutilise les structures, classes, fonctions et conventions déjà présentes lorsque c'est pertinent.
Ne casse aucune fonctionnalité existante.
Si certaines informations nécessaires sont absentes, identifie-les clairement avant d'inventer une solution.
L'analyse doit être déterministe et explicable dans un premier temps. Ne pas utiliser de machine learning pour cette première version.
Chaque recommandation doit être accompagnée des données et des règles qui ont conduit au diagnostic.
Ne jamais présenter une recommandation comme certaine lorsqu'il existe plusieurs causes possibles.
OBJECTIF PRINCIPAL

Transformer les données MoTeC en :

événements de conduite détectés automatiquement ;
symptômes détectés ;
hypothèses de causes ;
score de confiance ;
recommandations de réglage ;
données à surveiller pour confirmer ou infirmer la recommandation.

Architecture souhaitée :

Données MoTeC
→ nettoyage / validation
→ synchronisation
→ détection des événements
→ extraction des métriques
→ détection des symptômes
→ génération d'hypothèses
→ scoring
→ recommandation
→ rapport explicable

1. DONNÉES

Le système doit pouvoir exploiter, lorsqu'elles sont disponibles :

vitesse véhicule
accélération longitudinale
accélération latérale
yaw rate
steering angle
throttle
brake pressure
wheel speeds
damper position
damper velocity
ride height
suspension position
suspension velocity
engine RPM
gear
lap distance
lap number
GPS / position
température pneus
pression pneus
température freins
autres canaux disponibles

Le code doit être capable de fonctionner même si certains canaux sont absents.

Ne jamais supposer qu'un canal existe.

Créer un système de validation des canaux disponibles avant l'analyse.

Exemple :

AVAILABLE_CHANNELS = {
"speed": true,
"brake_pressure": true,
"damper_velocity_fl": true,
...
}

Les analyses nécessitant un canal absent doivent être ignorées ou marquées "insufficient data", et non provoquer une erreur.

2. NORMALISATION DES UNITÉS

Avant toute analyse, normaliser les unités.

Exemples :

damper velocity → mm/s
vitesse → km/h ou m/s, mais utiliser une unité cohérente
accélérations → g ou m/s²
pression → unité cohérente
angles → degrés

Conserver les unités originales dans les métadonnées si nécessaire.

IMPORTANT :
Les seuils doivent être centralisés dans une configuration et ne doivent pas être codés en dur dans différentes fonctions.

3. ANALYSE DES AMORTISSEURS

Créer une analyse spécifique de la vitesse d'amortisseur.

Utiliser initialement les catégories suivantes :

| Vitesse absolue | Zone |
| 0–25 mm/s | ultra low speed |
| 25–50 mm/s | low speed |
| 50–100 mm/s | low speed |
| 100–300 mm/s | transition |
| >300 mm/s | high speed |

Le système doit considérer séparément les deux directions :

compression
rebound / détente

La convention de signe doit être configurable car elle dépend du système MoTeC / capteur.

Calculer notamment :

temps passé dans chaque zone ;
distance parcourue dans chaque zone si pertinent ;
nombre d'événements ;
vitesse maximale ;
vitesse moyenne ;
vitesse RMS ;
distribution des vitesses ;
force d'amortissement si disponible ;
asymétrie compression/rebound ;
comparaison gauche/droite ;
comparaison avant/arrière.

IMPORTANT :
Ne jamais conclure "damping trop dur" uniquement à partir d'une vitesse d'amortisseur élevée.

La vitesse élevée est une observation, pas nécessairement la cause.

4. DÉTECTION DES ÉVÉNEMENTS

Créer un Event Detector capable d'identifier au minimum :

freinage ;
freinage fort ;
trail braking ;
turn-in ;
apex ;
accélération ;
remise des gaz ;
sortie de virage ;
changement de direction ;
ligne droite ;
passage sur vibreur si détectable ;
événement de suspension rapide.

Chaque événement doit contenir :

{
event_type,
start_time,
end_time,
lap,
distance_start,
distance_end,
corner_id,
confidence
}

Les seuils doivent être configurables.

Ne pas utiliser uniquement des seuils absolus lorsque des valeurs normalisées ou relatives peuvent être utilisées.

5. ANALYSE PAR VIRAGE

Si les données GPS ou la distance de piste permettent d'identifier les virages, analyser chaque virage séparément.

Créer idéalement :

CornerAnalysis

avec :

corner entry speed
minimum speed
exit speed
braking point
brake release
throttle application
lateral G
longitudinal G
steering angle
yaw rate
ride height
damper velocity
suspension travel
wheel slip
etc.

L'objectif est d'éviter de mélanger les comportements de différents virages.

6. DÉTECTION DES SYMPTÔMES

Créer un système extensible de règles.

Exemples de symptômes à implémenter progressivement :

SUSPENSION :

front platform too low under braking
rear squat under acceleration
excessive body roll
excessive pitch
frequent bumpstop contact
excessive suspension travel
suspension packing
excessive high-speed damper activity
excessive low-speed damper activity
left/right asymmetry
front/rear asymmetry

COMPORTEMENT :

entry understeer
mid-corner understeer
exit understeer
entry oversteer
mid-corner oversteer
exit oversteer
poor traction
braking instability
poor kerb compliance
slow direction change

Chaque symptôme doit être représenté sous forme d'un objet structuré.

Exemple :

{
"symptom": "front_platform_too_low_under_braking",
"score": 0.82,
"confidence": "high",
"evidence": [
"...",
"...",
"..."
]
}

7. SYSTÈME DE SCORING

Ne pas utiliser une règle binaire unique.

Utiliser un système de score pondéré.

Exemple :

Symptôme :
front_platform_too_low_under_braking

Indices :

front_ride_height_low +3
bumpstop_event +3
high_brake_pressure +2
high_longitudinal_g +2
damper_velocity_low_speed +1

Indices contradictoires :

adequate_bumpstop_margin -3
problem_only_one_corner -1

Calculer un score normalisé entre 0 et 1.

Le système doit conserver la liste complète des facteurs ayant augmenté ou diminué le score.

8. DIAGNOSTIC : SÉPARER OBSERVATION ET CAUSE

C'est une règle fondamentale du système.

Exemple :

OBSERVATION :
"Front ride height decreases significantly under braking."

CAUSES POSSIBLES :

spring stiffness
low-speed compression damping
ride height
bumpstop
aerodynamic load
brake balance
geometry

Ne jamais transformer directement l'observation en recommandation.

Créer une structure :

Observation
→ Symptom
→ Possible Causes
→ Evidence
→ Confidence
→ Recommended Test

9. MATRICE CAUSE → RÉGLAGE

Créer une base de connaissances configurable.

Exemple :

{
"symptom": "rear_traction_loss_on_exit",

"possible_causes": [
    {
        "cause": "rear_rebound_too_high",
        "weight": 0.45,
        "evidence": [
            "rear_rebound_velocity_high",
            "rear_load_variation_high",
            "wheel_slip_high"
        ],
        "possible_adjustment": "reduce_rear_rebound"
    },

    {
        "cause": "rear_spring_too_stiff",
        "weight": 0.25,
        ...
    },

    {
        "cause": "differential",
        "weight": 0.20,
        ...
    }
]


}

Les coefficients doivent être configurables.

10. RECOMMANDATIONS

Une recommandation doit toujours contenir :

problème détecté ;
niveau de confiance ;
preuves ;
causes possibles ;
réglage proposé ;
direction du changement ;
amplitude suggérée si suffisamment fiable ;
paramètres à surveiller après le changement.

Exemple :

{
"recommendation": "reduce_rear_rebound",
"direction": "softer",
"suggested_change": "-1 click",
"confidence": 0.76,
"reason": [
"rear rebound velocity elevated",
"traction loss during throttle application",
"repeatable in T3, T7 and T11"
],
"validation_channels": [
"rear_wheel_slip",
"rear_ride_height",
"yaw_rate",
"exit_speed"
]
}

IMPORTANT :
Si les données ne permettent pas de déterminer une amplitude fiable, dire simplement :

"Test : réduire légèrement le rear rebound"

plutôt que d'inventer "-2 clicks".

11. COMPARAISON DES TOURS

Implémenter une fonction permettant de comparer :

meilleurs tours ;
tours moyens ;
tours lents ;
tours consécutifs ;
tours avant modification de setup ;
tours après modification de setup.

Le système doit rechercher les différences reproductibles.

Exemple :

FAST LAPS
T3 exit speed = 142 km/h
T7 exit speed = 128 km/h

SLOW LAPS
T3 exit speed = 137 km/h
T7 exit speed = 124 km/h

Le moteur doit identifier les paramètres qui différencient systématiquement les deux groupes.

12. VALIDATION D'UNE RECOMMANDATION

Créer une logique de "before / after".

Lorsqu'un changement de setup est enregistré :

{
"setup_change": {
"parameter": "rear_rebound",
"old_value": 12,
"new_value": 11,
"timestamp": ...
}
}

Comparer les tours avant et après.

Déterminer si :

le symptôme s'est amélioré ;
le symptôme s'est aggravé ;
aucun changement significatif ;
les données sont insuffisantes.

Le système doit apprendre de ces résultats dans une future version, mais pour la V1 il suffit de les enregistrer.

13. EXPLICABILITÉ

Chaque diagnostic doit pouvoir répondre à :

"Pourquoi le système pense-t-il cela ?"

Exemple de sortie :

DIAGNOSTIC
Rear traction issue on corner exit

CONFIDENCE
78%

EVIDENCE

rear wheel slip increased by 14%
rear rebound velocity above threshold
rear ride height changes rapidly
issue reproduced in 4 corners
issue occurs during throttle application

POSSIBLE CAUSES

Rear rebound damping — 45%
Rear spring — 25%
Differential — 20%
Other — 10%

RECOMMENDED TEST
Reduce rear rebound damping slightly.

VALIDATION
Monitor:

rear wheel slip
exit speed
yaw rate
rear damper velocity
14. ARCHITECTURE LOGICIELLE

Avant d'implémenter, inspecte le projet et détermine où intégrer les modules suivants :

ChannelManager
UnitNormalizer
EventDetector
CornerDetector
DamperAnalyzer
SuspensionAnalyzer
SymptomDetector
DiagnosticEngine
SetupRecommendationEngine
LapComparator
ReportGenerator
Configuration / ThresholdManager

N'impose pas cette architecture si le projet possède déjà une architecture meilleure.

Adapte-la au code existant.

15. CONFIGURATION

Tous les seuils doivent être configurables.

Par exemple :

damper:
ultra_low_speed: 25
low_speed: 100
transition_speed: 300
high_speed: 300

braking:
heavy_braking_g: ...

suspension:
bumpstop_margin: ...

Ne pas disperser ces valeurs dans le code.

16. ROBUSTESSE

Le système doit gérer :

canaux absents ;
données NaN ;
trous dans les données ;
fréquence d'échantillonnage différente ;
valeurs aberrantes ;
capteurs défectueux ;
changement de signe des damper channels ;
unités différentes ;
tours incomplets.

Chaque résultat doit avoir un niveau de qualité des données.

Exemple :

data_quality = HIGH / MEDIUM / LOW / INSUFFICIENT

17. PRIORITÉ DE DÉVELOPPEMENT

Ne développe pas tout en même temps.

Implémente dans cet ordre :

PHASE 1

architecture
validation des canaux
normalisation
analyse damper velocity

PHASE 2

event detection
braking / acceleration
corner segmentation

PHASE 3

suspension analysis
ride height
damper
bumpstop

PHASE 4

symptom detection

PHASE 5

diagnostic engine
scoring

PHASE 6

setup recommendations

PHASE 7

lap comparison
before / after

PHASE 8

reporting / UI

Après chaque phase :

exécuter les tests existants ;
créer des tests unitaires ;
vérifier qu'aucune fonctionnalité existante n'est cassée ;
montrer les fichiers modifiés ;
expliquer les changements.
18. RÈGLE IMPORTANTE SUR LES RECOMMANDATIONS

Le moteur doit privilégier :

"Voici ce que les données montrent"

avant :

"Voici ce que tu dois changer."

Il doit pouvoir conclure :

"Insufficient evidence to recommend a setup change."

Cette conclusion est préférable à une mauvaise recommandation.

19. OBJECTIF FINAL

Le résultat final doit ressembler à un ingénieur data + ingénieur de piste qui analyse la télémétrie.

Exemple :

SETUP ANALYSIS

MAIN ISSUE
Rear traction loss on corner exit

CONFIDENCE
78%

WHERE
T3 / T7 / T11

WHEN
Throttle > 70%

OBSERVED DATA

wheel slip +14%
rear rebound velocity elevated
rear ride height rising rapidly
repeatable across multiple laps

POSSIBLE CAUSES

Rear rebound damping — 45%
Rear spring — 25%
Differential — 20%
Other — 10%

RECOMMENDATION
Test slightly softer rear rebound.

CHANGE
Start with the smallest available adjustment.

VALIDATION
Compare:

exit speed
wheel slip
yaw rate
rear damper velocity

EXPECTED RESULT
Reduced wheel slip and improved exit speed without excessive rear movement.

DATA QUALITY
HIGH

Commence maintenant par analyser le code existant.

NE MODIFIE PAS LE CODE immédiatement.

Première réponse attendue :

architecture actuelle du projet ;
flux actuel des données ;
endroits où intégrer le nouveau moteur ;
canaux déjà disponibles ;
canaux manquants ;
risques de régression ;
proposition d'architecture adaptée au projet ;
plan d'implémentation par étapes.

Attends ensuite ma validation avant de faire les modifications importantes.