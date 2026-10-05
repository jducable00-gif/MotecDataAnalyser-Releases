# Plan de Refonte du Système d'Analyse et de Lecture des Données

Ce document détaille la stratégie pour découpler la lecture des données de leur analyse afin de rendre l'application agnostique vis-à-vis des simulateurs et des modes d'acquisition.

## Suivi de Progression
Pour assurer la traçabilité et faciliter le débogage, ce fichier sert de journal de bord :
- **Tagging** : Chaque étape réalisée est marquée par un `[x]`.
- **Validation** : Une étape n'est cochée qu'une fois le code implémenté et testé.
- **Audit** : En cas de problème, le dernier point coché permet d'identifier rapidement la zone d'intervention.

## 1. Phase de Nettoyage (Cleanup)
L'objectif est de supprimer toute la logique d'analyse "hardcodée", les écritures directes dans l'interface utilisateur et de rationaliser la gestion des paramètres.

### Actions Code & UI :
- [x] **Analyse du code existant** : Identifier toutes les fonctions dans `MotecSetupForm.cpp` et `analyze_motec_setup.cpp` qui génèrent des messages d'avertissement ou de conseil.
- [x] **Purge de la logique d'écriture** : Les parseurs restent indépendants de la VCL ; les erreurs CSV/LD sont affichées par les gestionnaires UI. Le rapport actif n'émet plus les recommandations heuristiques historiques.
- [x] **Préservation de la structure** : Les `TTab` et leur structure visuelle sont conservés ; `updateReportTabs()` reçoit désormais uniquement les résultats du moteur de règles actif.

### Refonte des Settings (Paramètres) :
- [x] **Audit des variables de settings** : Inventorier toutes les variables de seuils présentes dans `MotecSettingsForm` et identifier celles qui sont obsolètes ou redondantes.
- [x] **Centralisation (`AnalysisSettings`)** : Le moteur de règles utilise des seuils groupés par catégorie (`TyreThresholds`, `EngineThresholds`, `HandlingThresholds`), construits depuis le modèle persistant.
- [x] **Nettoyage du Formulaire** : Le formulaire lit et écrit le modèle central `g_thresholds` directement ; aucun état de seuil parallèle n'est conservé dans le formulaire.
- [x] **Liaison Moteur $\rightarrow$ Settings** : Les règles du registre lisent leurs seuils depuis `AnalysisSettings`, y compris les limites de pression configurables.

## 2. Implémentation de la Couche d'Abstraction (Mapping)
Mise en œuvre des spécifications définies dans `DATA_MAPPING_SPEC.md`.

### Architecture :
- [x] **`DataProvider`** : Création d'une classe singleton ou injectée qui sert d'unique point d'accès aux données.
- [x] **Système d'Alias** : Les règles interrogent des alias canoniques typés via le `DataProvider`. Le mapping des canaux source Motec/Live, par profil et avec conversions, reste couvert par la tâche de configuration externe ci-dessous.
- [ ] **Découplage des canaux source** : Remplacer le dispatch direct des canaux dans `analyze_motec_setup.cpp` par l'ingestion via le mapping du `DataProvider`. Cette étape dépend de la définition du mapping source/profil ci-dessous.
- [x] **Configuration Externe** : `MotecDataAnalyser.mapping.json` stocke alias, profil, source Motec/Live, nom de canal, unités, facteur et offset ; il est chargé séparément de la configuration des règles.
- [x] **Canaux Virtuels** : Une entrée `type: "virtual"` dans `channelMappings` définit `Rake`; `DataProvider` résout l'alias avec le même accès que les mappings `raw` et calcule la différence des moyennes avant/arrière.

## 3. Nouveau Moteur d'Analyse et de Recommandation
L'analyse devient une couche logique pure, indépendante de la source technique et de l'interface utilisateur.

### Architecture Modulaire (Pattern "Rule-Based")
Pour simplifier l'ajout ou la suppression de points d'analyse sans risquer de casser le système, on utilise une approche par **Règles Indépendantes**.

#### A. La Structure d'une Règle (`AnalysisRule`)
Chaque point à analyser est encapsulé dans sa propre classe héritant d'une interface commune :
- [x] **`Input`** : Chaque règle déclare explicitement les alias nécessaires via `requiredInputs()`.
- [x] **`Logic`** : Une méthode `evaluate(DataProvider& dp)` qui contient la condition mathématique ou logique.
- [x] **`Output`** : Retourne un objet `AnalysisResult` standardisé comprenant :
  - **Sévérité** : `INFO`, `WARNING`, `CRITICAL`.
  - **Catégorie/Onglet** : ID de l'onglet cible (ex: `TAB_TYRES`).
  - **Message** : Titre et description du problème.
  - **Conseil** : Action corrective suggérée.

#### B. Le Registre d'Analyse (`AnalysisRegistry`)
Au lieu d'une suite de `if/else` géante, on utilise un registre :
- [x] Un conteneur (ex: `std::vector<std::unique_ptr<BaseRule>>`) qui stocke toutes les règles actives.
- [x] **Ajout simple** : Pour ajouter un nouveau point d'analyse, il suffit de créer la classe de la règle et de l'ajouter au registre.
- [x] **Suppression simple** : Il suffit de retirer la règle du registre sans toucher au reste du code.

#### C. Le Cycle d'Exécution
1. Le moteur parcourt toutes les règles du `AnalysisRegistry`.
2. Chaque règle interroge le `DataProvider` via les Alias.
3. Si la condition est remplie, un `AnalysisResult` est généré et envoyé au **Dispatcher UI**.

### Avantages de cette structure :
- **Lisibilité** : Chaque fichier de règle ne contient qu'une seule logique métier.
- **Maintenabilité** : On peut activer/désactiver des analyses dynamiquement.
- **Évolutivité** : L'ajout d'un nouveau simulateur n'impacte pas les règles, car elles utilisent des Alias.

## 4. Catalogue Initial d'Analyses (Cas de Test)
Ces règles serviront à valider l'architecture du `DataProvider` et du moteur de règles.

### A. Analyse des Pneus (Onglet Tyres)
Pour chaque pneu (FL, FR, RL, RR), on utilise les alias : `Tyre_Temp_{Pos}_Inner`, `Tyre_Temp_{Pos}_Middle`, `Tyre_Temp_{Pos}_Outer`.

| Point d'Analyse | Condition | Action / Conseil | Sévérité |
| :--- | :--- | :--- | :--- |
| **Camber (Écart I/O)** | `Inner < Outer` | Diminuer le Camber | WARNING |
| **Camber (Sous-utilisation)** | `(Inner - Outer) < 5°C` | Augmenter le Camber | INFO |
| **Pression (Surchauffe Middle)** | `Middle > max(Inner, Outer)` | Diminuer la Pression | WARNING |
| **Pression (Sous-utilisation Middle)** | `Middle < min(Inner, Outer)` | Augmenter la Pression | INFO |

### B. Analyse Moteur (Onglet Engine)
| Point d'Analyse | Condition | Action / Conseil | Sévérité |
| :--- | :--- | :--- | :--- |
| **Température Eau** | `Water_Temp > 100°C` | Vérifier le refroidissement eau | CRITICAL |
| **Température Huile** | `Oil_Temp > 110°C` | Vérifier le refroidissement huile | CRITICAL |

## 5. Intégration UI et Validation
Rétablir le lien entre les résultats d'analyse et l'affichage utilisateur.

### Étapes :
- [ ] **Dispatcher de Messages** : Créer un composant qui reçoit les alertes du moteur d'analyse et les route vers le bon `TTab`.
- [ ] **Validation Croisée** : Tester avec un fichier Motec AMS2 et une session Live AC EVO pour s'assurer que les mêmes conseils sont générés.
- [ ] **Tests de Régression** : Vérifier qu'aucune donnée critique n'est perdue lors du passage par la couche d'alias.

## 6. Résumé du Flux de Données Cible
`Source (File/Live)` $\rightarrow$ `DataProvider (Mapping/Conversion)` $\rightarrow$ `Analysis Engine (Rules)` $\rightarrow$ `UI Dispatcher` $\rightarrow$ `TTab Display`
