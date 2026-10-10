//---------------------------------------------------------------------------
// RuleConfig.h — Règles de diagnostic et alias de canaux, externalisés.
//
// Ce module sépare l'intelligence (les règles) du code d'analyse :
//   - Les symptômes (causes pondérées + canaux de validation) proviennent des
//     valeurs par défaut intégrées ET/OU du fichier JSON "symptoms.json"
//     (cherché à côté de l'exécutable, puis dans <exedir>config\).
//   - Les alias de canaux (generic / EvoM1 / AMS2) suivent le même principe.
//
// Sémantique de fusion : les entrées du fichier JSON remplacent ou étendent
// les valeurs par défaut intégrées — on peut donc ajouter un nouveau véhicule
// ou une nouvelle règle sans recompiler l'application (voir DEVELOPMENT_PLAN.md,
// étape 1). Si le fichier est absent, l'app fonctionne avec les valeurs
// par défaut ; s'il est corrompu, elle retombe aussi sur les valeurs par
// défaut et signale l'erreur au chargeur.
//---------------------------------------------------------------------------

#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "MotecSetupAnalyzer.h"   // CauseEntry, ChannelProfile

// ===============================
// RuleSymptom: une règle de diagnostic chargée en mémoire.
// Remplace l'ancien tableau statique SymptomRule (taille fixe) par des
// vecteurs dynamiques reconstruits depuis le JSON au démarrage.
// ===============================
struct RuleSymptom {
    std::string id;                          // ex: "rear_traction_loss_on_exit"
    std::vector<CauseEntry> causes;          // causes pondérées (0..1) + ajustement suggéré
    std::vector<std::string> validationChannels;  // canaux à surveiller après le changement
};

// ===============================
// JsonValue / JsonParser — mini-parseur JSON autonome (C++11, sans dépendance).
// Gère : null, booléens, nombres, chaînes (échappements + \uXXXX), tableaux et
// objets (ordre des clés préservé). Suffisant pour le schéma de symptoms.json.
// ===============================
struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type = Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> array;                                   // si type == Array
    std::vector<std::pair<std::string, JsonValue> > object;         // si type == Object

    const JsonValue* find(const std::string& key) const {
        if (type != Object) return NULL;
        for (std::size_t i = 0; i < object.size(); ++i)
            if (object[i].first == key)
                return &object[i].second;
        return NULL;
    }

    bool isNull() const { return type == Null; }
};

class JsonParser {
public:
    // Retourne true si le texte est un JSON valide ; sinon error contient la raison.
    static bool parse(const std::string& text, JsonValue& out, std::string& error);
};

// ===============================
// RuleConfig — point d'accès unique aux règles, alias historiques et mappings.
// Les règles/alias et mappings sont chargés depuis des fichiers distincts.
// ===============================
enum class RuleLoadStatus {
    Ok = 0,           // fichier lu et appliqué avec succès
    FileNotFound = -1,// pas de symptoms.json — valeurs par défaut conservées (normal)
    ParseError = -2   // JSON illisible/invalides — valeurs par défaut conservées
};

class RuleConfig {
public:
    static RuleConfig& instance();

    // Charge les règles + alias depuis un fichier JSON.
    // Fusion : symptômes remplacés/ajoutés par identifiant ; alias ajoutés ou
    // écrasés selon la clé "from". Les valeurs par défaut intégrées servent de base.
    RuleLoadStatus load(const std::string& jsonPath);

    // Charge les mappings applicatifs depuis leur fichier JSON dédié.
    RuleLoadStatus loadChannelMappings(const std::string& jsonPath);

    // Restaure les valeurs par défaut intégrées (règles + alias).
    void resetToDefaults();

    const std::vector<RuleSymptom>& rules() const { return m_rules; }
    const std::vector<ChannelMapping>& channelMappings() const { return m_channelMappings; }

    // Accès mutable : utilisé par loadSymptomWeights pour réécrire les poids de
    // cause depuis la section [SymptomWeights] du .ini (format "symptôme|cause").
    std::vector<RuleSymptom>& rulesRef() { return m_rules; }

    // Résout un alias de canal : renvoie true et remplit outKey si une entrée
    // d'alias mappe "key" vers un canal canonique. Ordre de vérification identique
    // à l'ancien code : generic d'abord, puis le profil spécifique (EvoM1).
    bool resolveAlias(ChannelProfile profile, const std::string& key, std::string& outKey) const;

private:
    RuleConfig();
    void buildDefaults();   // valeurs par défaut intégrées (ex symptomRules[] / genericAliases / evoAliases)

    static double clampWeight(double w);

    std::vector<RuleSymptom> m_rules;
    std::vector<ChannelMapping> m_channelMappings;
    std::unordered_map<std::string, std::string> m_genericAliases;  // tous profils
    std::unordered_map<std::string, std::string> m_evoAliases;      // profil EvoM1 uniquement
};
