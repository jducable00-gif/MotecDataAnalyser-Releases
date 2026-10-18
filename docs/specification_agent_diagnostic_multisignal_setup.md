# Spécification d’extension — Moteur de diagnostic et de recommandations de setup

## But de cette extension

Ajouter au logiciel existant un **moteur général de détection des symptômes, de diagnostic multi-signaux et de recommandations de réglage**.

Le projet possède déjà des fonctions d’analyse de carrossage à partir des températures Inner/Outer, d’analyse des températures de pneu liées à la pression, d’analyse d’histogrammes de suspension et d’affichage des températures de freins, d’eau et d’huile.

**Ne pas recoder ces fonctions dans ce travail.** Les considérer comme des analyses déjà disponibles et exploiter leurs résultats, ainsi que les canaux de télémétrie existants, pour construire une couche de diagnostic supérieure.

L’agent doit commencer par inspecter le code et le modèle de données réel, puis proposer un plan d’intégration qui réutilise l’architecture existante. Les noms de canaux ci-dessous sont des exemples sémantiques : utiliser les alias réellement définis dans le projet.

---

## 1. Architecture à ajouter : trois niveaux

### Niveau A — Détection des symptômes

Repérer des symptômes observables sans leur attribuer immédiatement une cause mécanique.

Exemples :
- perte de stabilité du train arrière en sortie de virage ;
- sous-virage en entrée ou au milieu du virage ;
- survirage en entrée ou en sortie ;
- perte de motricité à l’accélération ;
- blocage ou glissement excessif d’une roue au freinage ;
- comportement différent gauche/droite ;
- contact probable avec les butées, talonnage ou course de suspension fréquemment saturée, uniquement si les canaux permettent de l’étayer ;
- variations inhabituelles de hauteur de caisse, de tangage ou de roulis ;
- activité de suspension très différente entre roues comparables ;
- températures ou comportements qui se dégradent au fil des tours.

Chaque symptôme doit être associé à une fenêtre et une phase de conduite, à des mesures et à une qualité de données. Ne pas produire de symptôme si les canaux nécessaires sont absents ou trop peu fiables.

### Niveau B — Diagnostic multi-signaux

Pour chaque symptôme, évaluer plusieurs causes plausibles à partir de plusieurs signaux indépendants. Produire une liste de causes possibles **classées par plausibilité**, sans transformer une corrélation en certitude.

Exemple : perte de stabilité arrière en sortie de virage.
- Signaux utiles si disponibles : accélérateur, vitesse de roue, glissement longitudinal, angle de volant, taux de lacet, accélération latérale, charge verticale par roue, températures/état des pneus, réglages de différentiel.
- Causes à examiner selon les données disponibles : limite d’adhérence du pneu, transfert de charge, réglage de différentiel, amortissement, géométrie ou différence de conditions entre les tours.
- Le système ne doit pas conclure automatiquement « différentiel trop ouvert » ou « amortisseur trop souple » sur le seul symptôme de survirage.

Autres exemples de combinaisons :
- **Sous-virage en entrée** : freinage, angle de volant, réponse en lacet, accélération latérale, glissement, état des pneus et phase de relâchement du frein.
- **Blocage au freinage** : frein, vitesse de roue, vitesse véhicule, ABS actif si disponible, répartition de freinage si exposée.
- **Perte de motricité en sortie** : accélérateur, vitesses de roues, glissement, lacet, charge des roues arrière et réglages de différentiel si disponibles.
- **Comportement instable sur bosses/vibreurs** : vitesse de suspension, course, charge de roue, vibrations et vitesse véhicule.
- **Perte de performance à haute vitesse** : vitesse, hauteur de caisse, tangage/roulis, charges verticales et équilibre aérodynamique si ces mesures existent.

Les combinaisons ci-dessus sont des pistes de conception, pas des règles universelles. Adapter chaque règle aux canaux effectivement disponibles dans chaque simulateur.

### Niveau C — Recommandations de réglage et validation

Ne recommander un réglage que si :
1. un symptôme est suffisamment étayé ;
2. une ou plusieurs causes plausibles sont identifiées ;
3. un réglage lié à ces causes existe dans le jeu et est identifiable ;
4. les données permettent de distinguer raisonnablement cette cause des alternatives.

Chaque recommandation doit être formulée comme une expérience contrôlée :
- réglage à tester ;
- direction du changement, si elle peut être justifiée ;
- raison et mesures à l’appui ;
- niveau de confiance ;
- risques/effets secondaires possibles ;
- signaux à surveiller lors du prochain essai ;
- critères permettant de confirmer ou rejeter l’hypothèse.

Ne jamais inventer une valeur cible, un nombre de clics, une plage mécanique ou un réglage qui n’existe pas dans le simulateur. Si la direction du changement n’est pas suffisamment justifiée, demander un test comparatif plutôt que donner une consigne.

---

## 2. Segmenter les données par phase de conduite

Créer une segmentation réutilisable pour éviter les diagnostics qui mélangent des phénomènes différents.

Phases souhaitées, dans la mesure où les canaux le permettent :
- ligne droite / accélération stabilisée ;
- approche du freinage ;
- freinage initial ;
- entrée en virage / relâchement du frein ;
- milieu de virage ;
- sortie de virage / remise des gaz ;
- passage sur bosse ou vibreur ;
- virage lent, moyen ou rapide, si la classification est fiable.

Définir les phases avec des règles configurables utilisant les signaux disponibles (frein, accélérateur, vitesse, angle de volant, accélération latérale, taux de lacet, etc.). Ne pas coder des seuils universels sans les exposer en configuration.

Chaque segment doit contenir :
- temps ou échantillons inclus ;
- phase identifiée ;
- critères ayant conduit à cette classification ;
- indicateur de confiance ou de qualité de segmentation.

Si les données ne suffisent pas à distinguer les phases, utiliser une catégorie « phase inconnue » plutôt que de deviner.

---

## 3. Analyse des canaux et familles de signaux

Auditer le mapping actuel et établir un inventaire des canaux disponibles par simulateur. Utiliser les alias canoniques existants au lieu de dupliquer leur mapping.

Familles utiles à rechercher, uniquement si elles existent :
- direction : angle de volant, taux de lacet, accélération latérale ;
- commande conducteur : frein, accélérateur ;
- roues : vitesses de roues, glissement, charge verticale ;
- aides : ABS/traction control actif ;
- pneus : températures, pressions, carrossage et autres données déjà calculées par le logiciel ;
- suspension : vitesse d’amortisseur, course de suspension, hauteur de caisse, tangage, roulis ;
- véhicule : vitesse, accélérations ;
- réglages : biais de freinage, différentiel, barres antiroulis, ressorts, amortisseurs, hauteur de caisse et aérodynamique, seulement si réellement accessibles.

Pour chaque règle de diagnostic, déclarer les canaux requis, les canaux optionnels et les conditions de désactivation. Si une donnée critique manque, réduire la confiance ou s’abstenir de conclure.

---

## 4. Détection de l’asymétrie et comparaison gauche/droite ou avant/arrière

Ajouter des comparaisons entre roues lorsque les mesures sont comparables :
- gauche contre droite sur un même essieu ;
- avant contre arrière ;
- comportement d’un même coin du véhicule entre plusieurs tours ou sessions.

Avant d’interpréter une différence, tenir compte du sens du virage, du sens de la piste, de la charge latérale, des conditions de piste et du nombre de virages à gauche/droite. Ne pas conclure à un problème mécanique à partir de moyennes globales si elles mélangent des charges très différentes.

Distinguer :
- asymétrie répétée dans des conditions comparables ;
- asymétrie probablement due au tracé ou à la phase de conduite ;
- différence non concluante faute de données.

---

## 5. Comparaison avant/après un changement de setup

Ajouter un mode de comparaison entre deux sessions ou deux groupes de tours.

Conditions :
- aligner autant que possible le même circuit, la même voiture, les mêmes pneus et des conditions comparables ;
- comparer les mêmes phases de conduite et, si possible, les mêmes virages ;
- afficher la modification du setup connue entre les deux essais si elle est fournie ;
- comparer les temps au tour et les mesures disponibles, sans confondre amélioration d’un indicateur et amélioration globale de performance.

Pour chaque symptôme, indiquer :
- évolution de la mesure ;
- évolution de sa fréquence ou de sa sévérité ;
- si le changement va dans le sens attendu ;
- facteurs de confusion (conditions, trafic, pneus, carburant, style de pilotage, météo) ;
- verdict : amélioration probable, dégradation probable ou résultat inconclusif.

Ne pas attribuer automatiquement une amélioration à un réglage si plusieurs paramètres ont changé entre les essais.

---

## 6. Niveaux de confiance et traçabilité des diagnostics

Chaque diagnostic doit inclure :
- symptômes détectés ;
- mesures concrètes qui les soutiennent ;
- phase de conduite et fenêtre analysées ;
- causes candidates classées ;
- canaux manquants ou facteurs de confusion ;
- niveau de confiance ;
- prochaine vérification utile.

Échelle indicative :
- **Faible** : un seul indicateur indirect, ou données partielles ;
- **Moyen** : plusieurs indices concordants, mais plusieurs causes restent plausibles ;
- **Élevé** : plusieurs mesures indépendantes concordent dans des conditions répétées et comparables.

Le niveau de confiance ne doit pas être une probabilité numérique à moins que le système soit calibré sur des données validées. Ne pas afficher « 90 % sûr » sans calibration statistique.

Ajouter un mécanisme permettant de désactiver une règle et de tracer pourquoi elle n’a pas été exécutée (canal absent, données insuffisantes, phase non reconnue, etc.).

---

## 7. Catalogue de règles extensible

Créer, ou étendre si elle existe, une définition déclarative des règles de diagnostic. Éviter de disperser les seuils et les règles dans de multiples blocs de code.

Chaque règle devrait pouvoir définir :
- identifiant et version ;
- symptôme ciblé ;
- canaux requis et optionnels ;
- phases de conduite applicables ;
- conditions d’activation configurables ;
- mesures calculées ;
- causes candidates ;
- réglages pouvant être testés ;
- niveau de confiance maximal possible ;
- limites connues ;
- tests unitaires associés.

La structure exacte doit respecter les conventions et le langage du projet. Ne pas introduire un moteur de règles complexe ou une nouvelle dépendance si une architecture plus simple suffit.

---

## 8. Priorité des recommandations

Classer les recommandations selon :
1. sécurité ou anomalie clairement mesurée, si une limite spécifique et fiable est disponible ;
2. problème soutenu par plusieurs mesures indépendantes ;
3. hypothèse nécessitant un essai comparatif ;
4. information uniquement, lorsque les preuves ne permettent pas de recommander un réglage.

Dans ce projet, ne pas inventer de limites de température de frein, d’eau ou d’huile. Ces données peuvent servir de contexte ou à des comparaisons futures, mais elles ne doivent pas générer d’alerte mécanique sans seuils fiables propres au véhicule/moteur/simulateur.

Ne pas multiplier les recommandations contradictoires. Si deux hypothèses impliquent des changements opposés, expliquer le conflit et proposer d’abord une mesure ou un essai permettant de les départager.

---

## 9. Interface et lisibilité

Intégrer les résultats à l’interface actuelle, sans créer un écran parallèle inutile.

Pour chaque problème, afficher une fiche ou une section contenant :
- **Symptôme** ;
- **Quand il apparaît** ;
- **Éléments observés** ;
- **Causes possibles**, classées ;
- **Test conseillé** ;
- **Confiance** ;
- **Limites / données manquantes**.

Permettre à l’utilisateur de développer les preuves chiffrées. Les recommandations doivent être compréhensibles sans masquer les mesures qui les justifient.

Si l’interface a déjà un modèle de diagnostic, l’étendre de façon rétrocompatible.

---

## 10. Tests et critères d’acceptation

Ajouter des tests unitaires pour :
- les règles de détection de symptômes ;
- la segmentation des phases, y compris les cas ambigus ;
- les cas de canaux absents ou d’échantillons insuffisants ;
- les diagnostics avec plusieurs causes candidates ;
- le classement des causes et les niveaux de confiance ;
- les asymétries gauche/droite dans des conditions comparables et non comparables ;
- les comparaisons avant/après avec facteurs de confusion ;
- l’absence de recommandation catégorique lorsque les preuves sont insuffisantes ;
- la non-régression des analyses déjà présentes.

Utiliser des données synthétiques clairement documentées ou des extraits anonymisés de télémétrie du projet. Ne pas créer des tests qui valident une hypothèse mécanique arbitraire comme une vérité physique.

Critères de fin :
1. L’extension réutilise les diagnostics et alias existants.
2. Les symptômes sont séparés des causes et des recommandations.
3. Les analyses peuvent être segmentées par phase de conduite quand les données le permettent.
4. Les causes sont classées et justifiées par des mesures observables.
5. Chaque recommandation donne une expérience de validation et un niveau de confiance explicable.
6. La comparaison avant/après distingue les changements observés des causes supposées.
7. Les données manquantes et les facteurs de confusion sont explicitement gérés.
8. Les tests existants passent et les nouvelles règles sont testées.
9. L’agent fournit un rapport des fichiers modifiés, règles ajoutées, tests exécutés et limitations.

## 11. Ordre d’implémentation

1. Auditer l’architecture, les canaux, le mapping d’alias et les diagnostics existants.
2. Présenter un plan d’intégration fondé sur le dépôt réel.
3. Ajouter le moteur de symptômes sans modifier les diagnostics déjà en place.
4. Ajouter la segmentation des phases et la traçabilité des preuves.
5. Ajouter les diagnostics multi-signaux et le classement des causes.
6. Ajouter les recommandations conditionnelles et leur protocole de validation.
7. Ajouter la comparaison avant/après.
8. Intégrer les résultats à l’interface existante.
9. Ajouter et exécuter les tests de non-régression.
10. Fournir un rapport final.

**Instruction à l’agent :** ce document décrit des fonctionnalités nouvelles de diagnostic général. Les analyses déjà présentes dans le projet doivent être réutilisées comme sources de données, pas recodées. Inspecte le code avant de choisir les classes, les noms et les points d’intégration.
