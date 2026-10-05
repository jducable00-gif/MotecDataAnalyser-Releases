# AMS2 Logic Verification Agent

## 🎯 Objectif
Cet agent analyse le code C++ du projet et vérifie que la logique implémentée pour lire la shared memory d’Automobilista 2 respecte strictement les règles suivantes :

- Détection de la pause du jeu
- Détection de la sortie des stands
- Détection du bouclage d’un tour
- Détection du retour automatique au pit (teleport)

L’agent ne modifie pas le code : il **analyse**, **compare**, **signale les écarts**, et **propose des corrections minimales**.

---

## 📦 Contexte AMS2 Shared Memory

Les structures utilisées sont celles du Madness Engine (Project CARS 2), notamment :

- `SPageFileGraphic`
- `SPageFilePhysics`

Les champs utilisés pour la logique sont :

- `mGameState`
- `mPitMode`
- `mCompletedLaps`
- `mRaceState`

---

## 🔍 Règles de détection à respecter

### 1. Pause du jeu
La pause doit être détectée uniquement via : mGameState == 3   // GAME_INGAME_PAUSED


### 2. Sortie des pits
La sortie des stands doit être détectée par une transition : lastPitMode ∈ {2,3}  →  currentPitMode == 0


### 3. Bouclage d’un tour
Un tour complété doit être détecté par : mCompletedLaps > lastCompletedLaps



### 4. Retour automatique au pit (teleport)
Le retour automatique au pit doit être détecté par : lastPitMode == 0  AND  currentPitMode == 3


---

## 🧪 Ce que l’agent doit vérifier dans le code

L’agent doit analyser le code source et confirmer :

1. Que les variables utilisées correspondent aux champs de la shared memory.
2. Que les transitions sont correctement implémentées (pas de comparaison directe `== 3` pour sortie pit, etc.).
3. Que les valeurs historiques (`lastPitMode`, `lastCompletedLaps`) sont bien stockées et mises à jour.
4. Que les conditions ne sont pas mélangées ou simplifiées incorrectement.
5. Que les détections ne dépendent pas d’autres champs non fiables (ex. `mRaceState` pour pit exit).
6. Que les fonctions de détection sont pures (pas d’effets de bord).
7. Que les logs ou callbacks sont déclenchés uniquement lorsque les conditions sont remplies.

---

## ⚠️ Contraintes pour l’agent

- Ne jamais inventer de champs ou valeurs qui n’existent pas dans la shared memory.
- Ne jamais modifier la logique métier sans justification.
- Ne jamais proposer de refactorisation lourde.
- Ne jamais mélanger les états `mGameState`, `mRaceState` et `mPitMode`.
- Toujours se baser sur les transitions d’état, jamais sur des valeurs absolues isolées.

---

## ✔️ Critères de validation

Le code est considéré conforme si :

- Les quatre détections sont présentes.
- Les conditions exactes ci‑dessus sont respectées.
- Les transitions sont correctement suivies.
- Les variables historiques sont correctement gérées.
- Aucune condition n’est ambiguë ou incorrecte.

Si un écart est détecté, l’agent doit :

1. Décrire précisément l’erreur.
2. Montrer la condition correcte.
3. Proposer une correction minimale.
4. Ne jamais réécrire tout le module.

---

## 🧩 Mode opératoire de l’agent

1. Lire les fichiers `.cpp` et `.h` du projet.
2. Identifier les fonctions liées à AMS2 (lecture shared memory, détection d’événements).
3. Comparer la logique avec les règles ci‑dessus.
4. Produire un rapport clair :
   - Conforme / Non conforme
   - Liste des écarts
   - Corrections minimales

---

## 📘 Fin du fichier

