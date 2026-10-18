# Instructions pour l’agent IA — Analyse des suspensions et amortisseurs (Assetto Corsa EVO)

## 1. Objectif

Analyser la télémétrie disponible dans Assetto Corsa EVO afin de produire des **tendances de réglage prudentes et explicables** pour les amortisseurs et la plateforme de suspension.

L’agent doit :
- analyser les histogrammes de vitesse d’amortisseur par roue ;
- signaler les zones qui semblent surreprésentées ou sous-représentées par rapport à une plage cible configurable ;
- utiliser les autres canaux disponibles pour vérifier si les tendances sont cohérentes ;
- éviter de présenter une tendance statistique comme une preuve certaine qu’un réglage est incorrect.

Les recommandations sont des aides à l’analyse, pas des réglages universels garantis.

---

## 2. Analyse de l’histogramme de vitesse d’amortisseur

### 2.1 Convention de signe et zones

Utiliser la convention suivante **uniquement si elle correspond bien à la convention des données d’entrée** :
- vitesse négative : détente / rebound ;
- vitesse positive : compression / bump.

Si la convention du jeu ou du pipeline de données n’est pas confirmée, ne pas attribuer automatiquement les signes à la compression et à la détente. Demander une vérification ou exposer explicitement cette incertitude.

Avec une limite provisoire de vitesse lente à ±25 mm/s :

| Zone | Intervalle de vitesse | Nom court |
|---|---:|---|
| Détente lente | -25 à 0 mm/s | LSR |
| Détente rapide | inférieur à -25 mm/s | HSR |
| Compression lente | 0 à +25 mm/s | LSC |
| Compression rapide | supérieur à +25 mm/s | HSC |

Les valeurs exactement égales à -25, 0 et +25 mm/s doivent être affectées à une seule zone selon une règle constante, afin d’éviter les doubles comptages. Documenter cette règle dans le code.

**Important :** ±25 mm/s est un seuil de départ pour l’analyse comparative, pas une frontière universelle valable pour toutes les voitures, tous les circuits ou tous les jeux. Garder ces seuils configurables.

### 2.2 Calcul des pourcentages

Pour chaque roue, calculer le pourcentage du temps ou des échantillons dans chaque zone, selon la méthode d’échantillonnage utilisée :

- `LSR_pct`
- `HSR_pct`
- `LSC_pct`
- `HSC_pct`

Les quatre zones doivent être mutuellement exclusives et couvrir l’ensemble des échantillons valides. Par conséquent :

`LSR_pct + HSR_pct + LSC_pct + HSC_pct = 100 %`

Cette égalité est une propriété du calcul si tous les échantillons valides sont classés. **Ce n’est pas un test de qualité du réglage**. Elle peut servir à détecter un bug de classification ou des échantillons omis, avec une tolérance numérique appropriée.

Ignorer ou traiter explicitement les valeurs invalides, les trous de télémétrie et les périodes où la voiture est arrêtée. Utiliser la même politique pour toutes les roues et tous les tours comparés.

### 2.3 Cibles provisoires et prudence

Les plages suivantes peuvent être utilisées comme **valeurs initiales configurables**, uniquement pour signaler des écarts statistiques :

- zones lentes (LSR et LSC) : cible provisoire de 30–40 % chacune ;
- zones rapides (HSR et HSC) : cible provisoire de 10–20 % chacune.

Ces plages sont des heuristiques de travail et **ne sont pas des valeurs idéales universelles validées**. Elles peuvent être inadaptées à un véhicule, à un circuit, à un style de pilotage ou à une méthode d’échantillonnage particuliers.

L’agent doit :
1. permettre de modifier les cibles et les seuils de vitesse ;
2. afficher les valeurs mesurées et les cibles utilisées ;
3. ne pas présenter les cibles comme des données officielles MoTeC ou comme des spécifications du jeu ;
4. éviter de conclure qu’une voiture est mal réglée uniquement parce qu’une zone dépasse cette plage.

Si aucune cible fiable n’a été définie pour la voiture et les conditions analysées, présenter les pourcentages comme des observations descriptives plutôt que comme des défauts confirmés.

### 2.4 Interprétation d’une zone au-dessus ou en dessous de la cible

Pour la logique heuristique demandée :

- **Au-dessus de la cible** : signaler une présence excessive dans cette zone. Cela peut suggérer que l’amortisseur laisse trop facilement se produire les mouvements correspondant à cette zone ; une augmentation de l’amortissement concerné peut être une hypothèse à tester.
- **En dessous de la cible** : signaler une présence insuffisante dans cette zone. Cela peut suggérer que les mouvements correspondants sont trop limités ; une diminution de l’amortissement concerné peut être une hypothèse à tester.

Ne pas transformer ces règles en certitude physique. La distribution de vitesse dépend aussi des bosses, vibreurs, virages, freinages, accélérations, transferts de charge, vitesse du véhicule, pneus, ressorts, géométrie, aérodynamique et style de pilotage. L’histogramme mesure la fréquence des vitesses, **pas directement la force d’amortissement ni la raideur de l’amortisseur**.

L’agent doit employer des formulations comme :
- « tendance compatible avec… »
- « hypothèse à vérifier… »
- « tester un petit changement puis comparer… »

Éviter :
- « preuve que l’amortisseur est trop souple/dur » ;
- « appliquer obligatoirement +X clics » sans modèle fiable reliant les données aux réglages ;
- une recommandation de réglage précise déduite du seul histogramme.

### 2.5 Relier la zone au réglage concerné

Sous réserve de confirmer la nomenclature du véhicule et du jeu :

| Zone | Circuit à examiner en priorité |
|---|---|
| LSC | compression lente |
| HSC | compression rapide |
| LSR | détente lente |
| HSR | détente rapide |

Ne pas supposer que le réglage exposé par le jeu agit uniquement dans une zone parfaite ou indépendante des autres zones. Les réglages réels peuvent interagir.

Comparer les roues séparément (FL, FR, RL, RR), puis rechercher une tendance cohérente entre les roues d’un même essieu. Ne pas imposer une symétrie gauche/droite : le circuit, les virages et les conditions peuvent produire des différences réelles.

---

## 3. Qualité des données et méthode de comparaison

Avant de proposer une tendance :

1. Vérifier les unités et la convention de signe de la vitesse d’amortisseur.
2. Vérifier que les données sont synchronisées et que les échantillons sont valides.
3. Comparer des tours représentatifs et comparables, idéalement plusieurs tours propres.
4. Identifier les périodes pertinentes : tour lancé, tour complet, secteurs, virages, freinages ou vibreurs.
5. Ne pas mélanger sans distinction des tours de conditions très différentes.
6. Si possible, utiliser une durée réelle pondérée par le temps plutôt qu’un simple compte d’échantillons lorsque la fréquence d’échantillonnage varie.
7. Signaler le nombre de tours, la durée analysée, la fréquence d’échantillonnage et les données manquantes.
8. Ne recommander aucun changement lorsque la qualité ou la quantité des données est insuffisante.

Pour comparer avant/après un réglage, garder autant que possible constants le circuit, les conditions, le carburant, les pneus, les aides, le pilote, les secteurs et la méthode de collecte. Comparer les mêmes zones et la même durée utile.

---

## 4. Utiliser les canaux de suspension disponibles dans SharedFileOut

La documentation SharedFileOut fournie expose notamment les champs suivants. Leur présence dans la structure ne garantit pas qu’ils soient tous valides ou utiles dans chaque session : contrôler les valeurs à l’exécution.

### 4.1 Canaux utiles

| Champ | Interprétation documentée | Utilisation possible |
|---|---|---|
| `suspensionTravel[4]` | Déplacement/compression de suspension en mètres par roue | Étudier la plage utilisée, les pics, la répétition en butée possible et les différences entre roues |
| `rideHeight[2]` | Hauteur de caisse avant et arrière en mètres | Suivre l’évolution de la plateforme avant/arrière |
| `wheelLoad[4]` | Charge verticale des pneus en N par roue | Repérer les décharges de roue et les transferts de charge |
| `accG[3]` | Accélérations en G : latérale X, longitudinale Y, verticale Z | Contextualiser les événements et les réactions de caisse |
| `pitch`, `roll` | Tangage et roulis | Examiner les mouvements de plateforme |
| `speedKmh` | Vitesse du véhicule | Comparer des événements à vitesse similaire |
| `brake`, `gas`, `steerAngle` | Frein, accélérateur, angle de direction | Séparer freinage, accélération et virage |
| `localAngularVel[3]` | Vitesse angulaire locale | Compléter l’analyse des mouvements de caisse |
| `roadVibrations`, `kerbVibration`, `slipVibrations` | Indicateurs de vibration | Aider à identifier les événements de route, vibreur ou glissement, si les signaux sont pertinents |

Les indices des tableaux doivent être associés aux roues selon la documentation et le code source utilisés. Ne pas supposer un ordre différent de celui documenté.

### 4.2 Limites d’interprétation

- `suspensionTravel` est documenté comme déplacement/compression en mètres, mais ne pas supposer sans vérification s’il s’agit d’une position absolue, d’un déplacement relatif à une référence, ou d’une mesure remise à zéro périodiquement.
- Ne pas détecter une butée uniquement à partir d’un grand déplacement sans connaître la plage mécanique ou les limites de suspension.
- `rideHeight` est la hauteur de caisse avant/arrière ; ce champ n’est pas nécessairement une mesure de hauteur à chaque roue.
- `wheelLoad` peut indiquer une roue fortement déchargée, mais il faut tenir compte de la phase du virage, du freinage et de l’accélération.
- Les accélérations verticales peuvent provenir de bosses, vibreurs, mouvements de caisse ou d’autres événements ; elles ne prouvent pas à elles seules que le ressort ou l’amortisseur est incorrect.
- Ne pas déduire un taux de ressort en N/mm si ce taux n’est pas fourni par une source fiable.

### 4.3 Détection de tendances de plateforme

L’agent peut signaler des **indices à vérifier**, par exemple :
- déplacement de suspension qui atteint de façon répétée une limite connue ;
- variations importantes et répétées de hauteur de caisse ;
- décharge fréquente d’une roue dans une phase identifiable ;
- oscillations répétées de tangage ou de roulis après un événement ;
- corrélation entre une zone de vitesse d’amortisseur, un événement de route et une réaction de plateforme.

Pour chaque signalement, afficher les mesures qui le justifient et le contexte (roue, secteur, vitesse, frein/accélérateur, accélération). Ne pas recommander automatiquement un changement de ressort à partir d’un seul indicateur.

---

## 5. Distinguer amortisseur, ressort et autres causes

L’agent doit distinguer :

- **Histogramme d’amortisseur** : décrit la répartition des vitesses de déplacement de suspension.
- **Réglage d’amortissement** : agit sur la résistance aux mouvements de suspension ; l’histogramme seul ne mesure pas directement cette force.
- **Ressort** : sa raideur ne peut pas être déterminée à partir du seul histogramme de vitesse.
- **Plateforme / débattement** : peut fournir des indices sur l’utilisation de la suspension, mais demande des références et des limites connues.
- **Pneu, aérodynamique, géométrie, piste et pilotage** : peuvent influencer les mêmes signaux.

Pour suggérer d’examiner le ressort, exiger plusieurs indices cohérents, par exemple une utilisation répétée de presque tout le débattement, une hauteur de caisse qui atteint une limite connue, et une réaction de plateforme cohérente dans des conditions comparables. Même dans ce cas, formuler une hypothèse à vérifier, pas une certitude.

Si le taux de ressort ou les limites mécaniques ne sont pas disponibles, dire explicitement que la télémétrie actuelle ne permet pas de calculer directement la raideur du ressort.

---

## 6. Réglages disponibles dans la structure SharedFileOut

La structure `SMEvoElectronics` documente notamment :
- `front_bump_damper_level`
- `front_rebound_damper_level`
- `rear_bump_damper_level`
- `rear_rebound_damper_level`

Ces champs indiquent les niveaux de réglage d’amortisseur exposés par la structure. Ne pas supposer sans vérification la plage, le sens d’augmentation, le nombre de clics ou la correspondance exacte entre la valeur et l’effort d’amortissement. Vérifier ces éléments dans le jeu ou dans une documentation fiable avant de convertir une recommandation en commande concrète.

La documentation SharedFileOut utilisée ici ne décrit pas de champ explicite donnant directement le taux de ressort en N/mm. Cela signifie seulement que ce taux n’est pas identifié dans les structures documentées ; cela ne prouve pas qu’aucun réglage de ressort n’existe dans l’interface du jeu.

---

## 7. Format de sortie recommandé pour l’agent

Pour chaque roue, produire un résultat structuré avec :

1. **Qualité des données** : valide / insuffisante, nombre de tours, durée et éventuelles limites.
2. **Histogramme mesuré** : LSR, HSR, LSC, HSC en pourcentage.
3. **Cibles utilisées** : valeurs et indication explicite si elles sont provisoires.
4. **Écarts** : différence en points de pourcentage entre mesure et cible.
5. **Interprétation prudente** : tendance possible, jamais une certitude issue du seul histogramme.
6. **Canaux de corroboration** : débattement, hauteur de caisse, charge de roue, accélérations, tangage/roulis, vitesse et commandes pilote.
7. **Hypothèse à tester** : un changement limité à la fois, si les données sont suffisamment cohérentes.
8. **Méthode de validation** : répéter des tours comparables et comparer les mêmes indicateurs.
9. **Niveau de confiance** : faible, moyen ou élevé, avec une justification explicite. Le niveau élevé exige plusieurs sources d’indice cohérentes et des données de bonne qualité ; il ne signifie pas une certitude absolue.

Exemple de formulation :

> **Roue avant gauche — HSC au-dessus de la cible provisoire.** Cette roue passe une part relativement importante du temps au-dessus du seuil de compression rapide choisi. Cela peut être lié aux événements de piste et à la vitesse d’entrée dans cette zone, pas uniquement au réglage d’amortisseur. Vérifier les vibreurs, les pics de débattement et les accélérations verticales dans les mêmes secteurs. Si la tendance persiste sur plusieurs tours comparables, tester un petit ajustement de compression rapide, puis comparer les mêmes indicateurs avant/après.

---

## 8. Règles de sécurité analytique

- Ne jamais présenter une cible heuristique comme une norme officielle.
- Ne jamais affirmer qu’un histogramme seul prouve qu’un amortisseur est trop dur ou trop souple.
- Ne jamais inférer directement le taux de ressort à partir des seuls pourcentages de vitesse d’amortisseur.
- Ne jamais recommander un nombre exact de clics sans connaître la plage et le sens du réglage.
- Ne pas interpréter les zones rapides comme un défaut automatique : elles peuvent correspondre à des bosses, vibreurs, compressions de piste ou à une conduite agressive.
- Ne pas exiger une distribution symétrique entre détente et compression ni entre gauche et droite.
- Signaler les hypothèses, seuils, limites et données manquantes.
- Privilégier une modification à la fois et une validation par comparaison avant/après.
