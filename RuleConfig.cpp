//---------------------------------------------------------------------------
// RuleConfig.cpp — Parseur JSON minimaliste + règles/alias externalisés.
//
// Les valeurs par défaut intégrées ci-dessous sont les anciennes tables
// statiques de analyze_motec_setup.cpp (symptomRules[], genericAliases,
// evoAliases), déplacées ici pour que le moteur de diagnostic soit piloté
// par la configuration (DEVELOPMENT_PLAN.md — étape 1).
//---------------------------------------------------------------------------

#pragma hdrstop

#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "RuleConfig.h"

// ===============================
// JsonParser — descente récursive, C++11 strict (pas de lambdas dans le parseur)
// ===============================
namespace {

struct JsonParseContext {
    const std::string& text;
    std::size_t pos;
    bool failed = false;
    std::string error;

    explicit JsonParseContext(const std::string& t) : text(t), pos(0) {}

    void fail(const char* message, std::size_t atPos) {
        if (!failed) {
            failed = true;
            error = std::string(message);
            error += " (position " + std::to_string(atPos) + ")";
        }
    }

    void skipWhitespace() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                ++pos;
            else
                break;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (pos < text.size() && text[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    char peek() const {
        return pos < text.size() ? text[pos] : '\0';
    }

    bool parseValue(JsonValue& out);

    void parseString(std::string& out) {
        // Assumes the opening quote has already been consumed.
        out.clear();
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"')
                return;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos >= text.size()) break;
            char esc = text[pos++];
            switch (esc) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > text.size()) { fail("invalid \\u escape", pos); return; }
                    unsigned int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = text[pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9')      code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else { fail("invalid \\u escape", pos); return; }
                    }
                    // Surrogate pair handling.
                    unsigned int cp = code;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos + 6 > text.size() || text[pos] != '\\' || text[pos+1] != 'u') {
                            fail("unpaired high surrogate", pos); return;
                        }
                        pos += 2;
                        unsigned int low = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = text[pos++];
                            low <<= 4;
                            if (h >= '0' && h <= '9')      low |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') low |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') low |= static_cast<unsigned>(h - 'A' + 10);
                            else { fail("invalid \\u escape", pos); return; }
                        }
                        cp = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    // Encode as UTF-8.
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: fail("invalid escape", pos); return;
            }
        }
        if (!failed) fail("unterminated string", text.size());
    }

    void parseNumber(JsonValue& out) {
        std::size_t start = pos;
        while (pos < text.size()) {
            char c = text[pos];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                ++pos;
            else
                break;
        }
        std::string numStr = text.substr(start, pos - start);
        if (numStr.empty()) { fail("invalid number", start); return; }
        char* endPtr = NULL;
        double value = std::strtod(numStr.c_str(), &endPtr);
        if (endPtr == numStr.c_str() || *endPtr != '\0') { fail("invalid number", start); return; }
        out.type = JsonValue::Number;
        out.number = value;
    }

    void parseLiteral(const char* literal, std::size_t length) {
        if (text.compare(pos, length, literal) == 0)
            pos += length;
        else
            fail("invalid token", pos);
    }
};

bool JsonParseContext::parseValue(JsonValue& out) {
    skipWhitespace();
    if (pos >= text.size()) { fail("unexpected end of input", pos); return false; }
    char c = text[pos];

    if (c == '{') {
        ++pos; // consume '{'
        out.type = JsonValue::Object;
        skipWhitespace();
        if (consume('}'))
            return true;
        while (!failed) {
            skipWhitespace();
            if (peek() != '"') { fail("expected object key string", pos); return false; }
            ++pos; // consume '"'
            std::string key;
            parseString(key);
            if (failed) return false;
            if (!consume(':')) { fail("expected ':' after object key", pos); return false; }
            JsonValue value;
            if (!parseValue(value)) return false;
            out.object.push_back(std::make_pair(key, value));
            skipWhitespace();
            if (consume(','))
                continue;
            if (consume('}'))
                return true;
            fail("expected ',' or '}' in object", pos);
            return false;
        }
        return false;
    }

    if (c == '[') {
        ++pos; // consume '['
        out.type = JsonValue::Array;
        skipWhitespace();
        if (consume(']'))
            return true;
        while (!failed) {
            JsonValue value;
            if (!parseValue(value)) return false;
            out.array.push_back(value);
            skipWhitespace();
            if (consume(','))
                continue;
            if (consume(']'))
                return true;
            fail("expected ',' or ']' in array", pos);
            return false;
        }
        return false;
    }

    if (c == '"') {
        ++pos; // consume '"'
        out.type = JsonValue::String;
        parseString(out.str);
        return !failed;
    }

    if (c == 't' || c == 'f') {
        bool value = false;
        if (text.compare(pos, 4, "true") == 0)      { pos += 4; value = true; }
        else if (text.compare(pos, 5, "false") == 0){ pos += 5; value = false; }
        else { fail("invalid literal", pos); return false; }
        out.type = JsonValue::Bool;
        out.boolean = value;
        return true;
    }

    if (c == 'n') {
        parseLiteral("null", 4);
        out.type = JsonValue::Null;
        return !failed;
    }

    // Number: must start with a digit or '-'.
    if ((c >= '0' && c <= '9') || c == '-') {
        parseNumber(out);
        return !failed;
    }

    fail("unexpected character", pos);
    return false;
}

// Helper accessors used by RuleConfig::load.
double jsonGetDouble(const JsonValue* v, double fallback) {
    if (v && v->type == JsonValue::Number)
        return v->number;
    return fallback;
}

std::string jsonString(const JsonValue* v) {
    if (v && v->type == JsonValue::String)
        return v->str;
    return std::string();
}

bool jsonGetBool(const JsonValue* v, bool fallback) {
    if (v && v->type == JsonValue::Bool)
        return v->boolean;
    return fallback;
}

const JsonValue* jsonObjectField(const JsonValue& obj, const char* key) {
    for (std::size_t i = 0; i < obj.object.size(); ++i)
        if (obj.object[i].first == key)
            return &obj.object[i].second;
    return NULL;
}

const std::vector<JsonValue>* jsonArrayField(const JsonValue& obj, const char* key) {
    const JsonValue* v = jsonObjectField(obj, key);
    if (v && v->type == JsonValue::Array)
        return &v->array;
    return NULL;
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::string toUpperCopy(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    return s;
}

// Bornage des poids de cause à [0, 1].
double clampWeightValue(double w) {
    if (w < 0.0) return 0.0;
    if (w > 1.0) return 1.0;
    return w;
}

// Ajoute une cause pondérée à un symptôme.
// CauseEntry est un agrégat sans constructeur : on remplit champ par champ.
void addCause(RuleSymptom& r, const char* cause, double weight,
              const char* adjustment, const char* direction) {
    CauseEntry entry;
    entry.cause = cause ? cause : "";
    entry.weight = clampWeightValue(weight);
    entry.adjustment = adjustment ? adjustment : "";
    entry.direction = direction ? direction : "";
    r.causes.push_back(entry);
}

// Applique un symptôme JSON sur la liste de règles : fusion par identifiant.
void mergeJsonSymptom(std::vector<RuleSymptom>& rules, const JsonValue& sym) {
    std::string id = trimCopy(jsonString(jsonObjectField(sym, "id")));
    if (id.empty())
        return;

    RuleSymptom* target = NULL;
    for (std::size_t i = 0; i < rules.size(); ++i) {
        if (rules[i].id == id) { target = &rules[i]; break; }
    }
    if (!target) {
        RuleSymptom fresh;
        fresh.id = id;
        rules.push_back(fresh);
        target = &rules[rules.size() - 1];
    }

    const std::vector<JsonValue>* causesArr = jsonArrayField(sym, "causes");
    if (causesArr) {
        for (std::size_t i = 0; i < causesArr->size(); ++i) {
            const JsonValue& c = (*causesArr)[i];
            std::string causeName = trimCopy(jsonString(jsonObjectField(c, "cause")));
            if (causeName.empty())
                continue;

            CauseEntry entry;
            entry.cause = causeName;
            entry.weight = clampWeightValue(jsonGetDouble(jsonObjectField(c, "weight"), 0.0));
            std::string adjustment = jsonString(jsonObjectField(c, "adjustment"));
            if (!adjustment.empty())
                entry.adjustment = adjustment;
            std::string direction = jsonString(jsonObjectField(c, "direction"));
            if (!direction.empty())
                entry.direction = direction;

            // Fusion par nom de cause : mise à jour si existante, sinon ajout.
            bool found = false;
            for (std::size_t k = 0; k < target->causes.size(); ++k) {
                if (target->causes[k].cause == entry.cause) {
                    target->causes[k] = entry;
                    found = true;
                    break;
                }
            }
            if (!found)
                target->causes.push_back(entry);
        }
    }

    const std::vector<JsonValue>* valArr = jsonArrayField(sym, "validationChannels");
    if (valArr && !valArr->empty()) {
        target->validationChannels.clear();
        for (std::size_t i = 0; i < valArr->size(); ++i) {
            std::string ch = trimCopy(jsonString((*valArr)[i].type == JsonValue::String ? &(*valArr)[i] : NULL));
            if (!ch.empty())
                target->validationChannels.push_back(ch);
        }
    }
}

// Applique une entrée d'alias JSON sur les tables.
void mergeJsonAlias(std::unordered_map<std::string, std::string>& genericMap,
                    std::unordered_map<std::string, std::string>& evoMap,
                    const JsonValue& alias) {
    std::string from = toUpperCopy(trimCopy(jsonString(jsonObjectField(alias, "from"))));
    std::string to   = toUpperCopy(trimCopy(jsonString(jsonObjectField(alias, "to"))));
    if (from.empty() || to.empty())
        return;

    std::string profileRaw = trimCopy(toUpperCopy(jsonString(jsonObjectField(alias, "profile"))));
    // "" / absent → generic ; "EVOM1" → table EvoM1 ; tout le reste ("AMS2",
    // "GENERIC") → generic (ChannelProfile::Generic == AMS2).
    if (profileRaw == "EVOM1" || profileRaw == "EVO") {
        evoMap[from] = to;
    } else {
        genericMap[from] = to;
    }
}

bool parseVirtualAlias(const std::string& name, AnalysisAlias& alias);

void mergeJsonChannelMapping(std::vector<ChannelMapping>& mappings,
                             const JsonValue& value) {
    ChannelMapping mapping;
    mapping.alias = trimCopy(jsonString(jsonObjectField(value, "alias")));
    const std::string type = toUpperCopy(trimCopy(jsonString(jsonObjectField(value, "type"))));
    if (type == "VIRTUAL") {
        mapping.type = ChannelMappingType::Virtual;
        mapping.source = MappingSource::Virtual;
        mapping.standardUnit = trimCopy(jsonString(jsonObjectField(value, "unit")));
        if (mapping.alias.empty() || !parseVirtualAlias(mapping.alias, mapping.virtualAlias))
            return;

        const std::string operation = toUpperCopy(trimCopy(jsonString(jsonObjectField(value, "operation"))));
        if (operation != "DIFFERENCE_OF_AVERAGES")
            return;
        mapping.operation = VirtualChannelOperation::DifferenceOfAverages;

        const std::vector<JsonValue>* positive = jsonArrayField(value, "positiveInputs");
        const std::vector<JsonValue>* negative = jsonArrayField(value, "negativeInputs");
        if (!positive || !negative || positive->empty() || negative->empty())
            return;

        for (std::size_t i = 0; i < positive->size(); ++i) {
            AnalysisAlias input;
            if ((*positive)[i].type != JsonValue::String || !parseVirtualAlias((*positive)[i].str, input))
                return;
            mapping.positiveInputs.push_back(input);
        }
        for (std::size_t i = 0; i < negative->size(); ++i) {
            AnalysisAlias input;
            if ((*negative)[i].type != JsonValue::String || !parseVirtualAlias((*negative)[i].str, input))
                return;
            mapping.negativeInputs.push_back(input);
        }
    } else if (type.empty() || type == "RAW") {
        mapping.type = ChannelMappingType::Raw;
        mapping.channel = trimCopy(jsonString(jsonObjectField(value, "channel")));
        mapping.standardUnit = trimCopy(jsonString(jsonObjectField(value, "unit")));
        mapping.sourceUnit = trimCopy(jsonString(jsonObjectField(value, "sourceUnit")));
        mapping.factor = jsonGetDouble(jsonObjectField(value, "factor"), 1.0);
        mapping.offset = jsonGetDouble(jsonObjectField(value, "offset"), 0.0);

        const std::string profile = toUpperCopy(trimCopy(jsonString(jsonObjectField(value, "profile"))));
        if (profile == "EVOM1" || profile == "EVO") {
            mapping.profile = ChannelProfile::EvoM1;
        } else if (profile == "AMS2") {
            mapping.profile = ChannelProfile::AMS2;
        } else {
            return;
        }

        const std::string source = toUpperCopy(trimCopy(jsonString(jsonObjectField(value, "source"))));
        if (source == "MOTEC" || source == "FILE") {
            mapping.source = MappingSource::MotecFile;
        } else if (source == "LIVE" || source == "SHARED_MEMORY") {
            mapping.source = MappingSource::LiveSharedMemory;
        } else {
            return;
        }

        if (mapping.channel.empty() || !std::isfinite(mapping.factor) ||
            mapping.factor == 0.0 || !std::isfinite(mapping.offset))
            return;
    } else {
        return;
    }

    if (mapping.alias.empty())
        return;

    const std::string normalizedAlias = toUpperCopy(mapping.alias);
    for (std::size_t i = 0; i < mappings.size(); ++i) {
        bool sameKey = toUpperCopy(mappings[i].alias) == normalizedAlias &&
                       mappings[i].type == mapping.type;
        if (mapping.type == ChannelMappingType::Raw)
            sameKey = sameKey && mappings[i].profile == mapping.profile && mappings[i].source == mapping.source;
        if (sameKey) {
            mappings[i] = mapping;
            return;
        }
    }
    mappings.push_back(mapping);
}

bool parseVirtualAlias(const std::string& name, AnalysisAlias& alias) {
    const std::string key = toUpperCopy(trimCopy(name));
    if (key == "RIDE_HEIGHT_FL") alias = AnalysisAlias::RideHeightFL;
    else if (key == "RIDE_HEIGHT_FR") alias = AnalysisAlias::RideHeightFR;
    else if (key == "RIDE_HEIGHT_RL") alias = AnalysisAlias::RideHeightRL;
    else if (key == "RIDE_HEIGHT_RR") alias = AnalysisAlias::RideHeightRR;
    else if (key == "RAKE") alias = AnalysisAlias::Rake;
    else return false;
    return true;
}

} // namespace

bool JsonParser::parse(const std::string& text, JsonValue& out, std::string& error) {
    JsonParseContext ctx(text);
    if (!ctx.parseValue(out)) {
        error = ctx.error;
        return false;
    }
    ctx.skipWhitespace();
    if (ctx.pos < ctx.text.size()) {
        error = "trailing characters after JSON document";
        return false;
    }
    return true;
}

// ===============================
// RuleConfig — valeurs par défaut intégrées
// ===============================
RuleConfig::RuleConfig() {
    buildDefaults();
}

double RuleConfig::clampWeight(double w) {
    return clampWeightValue(w);
}

void RuleConfig::resetToDefaults() {
    m_rules.clear();
    m_channelMappings.clear();
    m_genericAliases.clear();
    m_evoAliases.clear();
    buildDefaults();
}

RuleLoadStatus RuleConfig::load(const std::string& jsonPath) {
    // Repart toujours des valeurs par défaut intégrées, puis fusionne le JSON.
    resetToDefaults();

    std::ifstream in(jsonPath.c_str(), std::ios::binary);
    if (!in.is_open())
        return RuleLoadStatus::FileNotFound;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    // BOM UTF-8 éventuel.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);

    JsonValue root;
    std::string error;
    if (!JsonParser::parse(text, root, error)) {
        // Fichier corrompu : on conserve les valeurs par défaut intégrées.
        resetToDefaults();
        return RuleLoadStatus::ParseError;
    }

    const std::vector<JsonValue>* symptoms = jsonArrayField(root, "symptoms");
    if (symptoms) {
        for (std::size_t i = 0; i < symptoms->size(); ++i)
            mergeJsonSymptom(m_rules, (*symptoms)[i]);
    }

    const std::vector<JsonValue>* aliases = jsonArrayField(root, "channelAliases");
    if (aliases) {
        for (std::size_t i = 0; i < aliases->size(); ++i)
            mergeJsonAlias(m_genericAliases, m_evoAliases, (*aliases)[i]);
    }

    return RuleLoadStatus::Ok;
}

RuleLoadStatus RuleConfig::loadChannelMappings(const std::string& jsonPath) {
    m_channelMappings.clear();

    std::ifstream in(jsonPath.c_str(), std::ios::binary);
    if (!in.is_open())
        return RuleLoadStatus::FileNotFound;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);

    JsonValue root;
    std::string error;
    if (!JsonParser::parse(text, root, error))
        return RuleLoadStatus::ParseError;

    const std::vector<JsonValue>* mappings = jsonArrayField(root, "channelMappings");
    if (!mappings)
        return RuleLoadStatus::ParseError;

    for (std::size_t i = 0; i < mappings->size(); ++i)
        mergeJsonChannelMapping(m_channelMappings, (*mappings)[i]);

    return RuleLoadStatus::Ok;
}

const RuleSymptom* RuleConfig::findRule(const std::string& symptom) const {
    for (std::size_t i = 0; i < m_rules.size(); ++i)
        if (m_rules[i].id == symptom)
            return &m_rules[i];
    return NULL;
}

bool RuleConfig::resolveAlias(ChannelProfile profile, const std::string& key, std::string& outKey) const {
    // Ordre identique à l'ancien resolveChannelKey : generic d'abord (tous profils),
    // puis la table spécifique au profil EvoM1.
    std::unordered_map<std::string, std::string>::const_iterator it = m_genericAliases.find(key);
    if (it != m_genericAliases.end()) {
        outKey = it->second;
        return true;
    }
    if (profile == ChannelProfile::EvoM1) {
        std::unordered_map<std::string, std::string>::const_iterator evoIt = m_evoAliases.find(key);
        if (evoIt != m_evoAliases.end()) {
            outKey = evoIt->second;
            return true;
        }
    }
    return false;
}

bool RuleConfig::resolveChannelMapping(ChannelProfile profile, MappingSource source,
                                       const std::string& alias, ChannelMapping& outMapping) const {
    const std::string normalizedAlias = toUpperCopy(trimCopy(alias));
    for (std::size_t i = 0; i < m_channelMappings.size(); ++i) {
        const ChannelMapping& mapping = m_channelMappings[i];
        if (mapping.type == ChannelMappingType::Raw &&
            mapping.profile == profile && mapping.source == source &&
            toUpperCopy(mapping.alias) == normalizedAlias) {
            outMapping = mapping;
            return true;
        }
    }
    for (std::size_t i = 0; i < m_channelMappings.size(); ++i) {
        const ChannelMapping& mapping = m_channelMappings[i];
        if (mapping.type == ChannelMappingType::Virtual &&
            toUpperCopy(mapping.alias) == normalizedAlias) {
            outMapping = mapping;
            return true;
        }
    }
    return false;
}

bool RuleConfig::resolveRawToAlias(ChannelProfile profile, MappingSource source,
                                   const std::string& rawName, std::string& outAlias) const {
    const std::string normalizedRaw = toUpperCopy(trimCopy(rawName));
    for (std::size_t i = 0; i < m_channelMappings.size(); ++i) {
        const ChannelMapping& mapping = m_channelMappings[i];
        if (mapping.type == ChannelMappingType::Raw &&
            mapping.profile == profile && mapping.source == source &&
            toUpperCopy(mapping.channel) == normalizedRaw) {
            outAlias = mapping.alias;
            return true;
        }
    }
    return false;
}

RuleConfig& RuleConfig::instance() {
    static RuleConfig config;
    return config;
}

// ===============================
// buildDefaults — anciennes tables statiques de analyze_motec_setup.cpp,
// déplacées ici (DEVELOPMENT_PLAN.md étape 1).
// ===============================
void RuleConfig::buildDefaults() {
    m_rules.clear();
    m_genericAliases.clear();
    m_evoAliases.clear();

    // ---------- Symptômes : causes pondérées + canaux de validation ----------
    {
        RuleSymptom r;
        r.id = "rear_traction_loss_on_exit";
        addCause(r, "rear_rebound_too_high", 0.45, "reduce_rear_rebound", "softer");
        addCause(r, "rear_spring_too_stiff", 0.25, "soften_rear_spring", "softer");
        addCause(r, "differential_too_locking", 0.20, "reduce_diff_lock", "reduce");
        addCause(r, "rear_pressure_low", 0.10, "increase_rear_pressure", "increase");
        r.validationChannels.push_back("rear_wheel_slip");
        r.validationChannels.push_back("exit_speed");
        r.validationChannels.push_back("yaw_rate");
        r.validationChannels.push_back("rear_damper_velocity");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "front_understeer_entry";
        addCause(r, "front_camber_insufficient", 0.40, "increase_front_camber", "increase");
        addCause(r, "front_roll_stiffness_low", 0.30, "stiffen_front_anti_roll", "stiffer");
        addCause(r, "front_pressure_low", 0.20, "increase_front_pressure", "increase");
        addCause(r, "front_rebound_too_soft", 0.10, "increase_front_rebound", "stiffer");
        r.validationChannels.push_back("front_slip_angle");
        r.validationChannels.push_back("yaw_rate");
        r.validationChannels.push_back("front_damper_velocity");
        r.validationChannels.push_back("lateral_g");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "bumpstop_engagement";
        addCause(r, "spring_too_soft", 0.35, "stiffen_spring", "stiffer");
        addCause(r, "ride_height_too_low", 0.30, "raise_ride_height", "increase");
        addCause(r, "low_speed_damping_too_soft", 0.25, "increase_low_speed_damping", "stiffer");
        addCause(r, "bumpstop_too_hard", 0.10, "softer_bumpstop", "softer");
        r.validationChannels.push_back("suspension_travel");
        r.validationChannels.push_back("damper_velocity");
        r.validationChannels.push_back("ride_height");
        r.validationChannels.push_back("body_accel");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "brake_instability";
        addCause(r, "brake_bias_too_rear", 0.40, "shift_bias_forward", "increase");
        addCause(r, "front_pressure_low", 0.25, "increase_front_pressure", "increase");
        addCause(r, "front_damping_too_soft", 0.20, "increase_front_damping", "stiffer");
        addCause(r, "front_spring_too_soft", 0.15, "stiffen_front_spring", "stiffer");
        r.validationChannels.push_back("front_brake_temp");
        r.validationChannels.push_back("front_dive");
        r.validationChannels.push_back("brake_balance");
        r.validationChannels.push_back("front_wheel_speed");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "damper_high_speed_share";
        addCause(r, "high_speed_damping_too_low", 0.50, "increase_high_speed_damping", "stiffer");
        addCause(r, "spring_rate_too_soft", 0.30, "stiffen_spring", "stiffer");
        addCause(r, "bumpstop_not_engaging", 0.10, "check_bumpstop", "check");
        addCause(r, "other", 0.10, "review_setup", "review");
        r.validationChannels.push_back("damper_velocity");
        r.validationChannels.push_back("suspension_travel");
        r.validationChannels.push_back("body_accel");
        r.validationChannels.push_back("tyre_load");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "damper_low_speed_stiff";
        addCause(r, "low_speed_damping_too_high", 0.50, "reduce_low_speed_damping", "softer");
        addCause(r, "spring_rate_too_stiff", 0.30, "soften_spring", "softer");
        addCause(r, "ride_height_too_high", 0.10, "lower_ride_height", "reduce");
        addCause(r, "other", 0.10, "review_setup", "review");
        r.validationChannels.push_back("damper_velocity");
        r.validationChannels.push_back("suspension_travel");
        r.validationChannels.push_back("tyre_contact_patch");
        r.validationChannels.push_back("body_height");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "damper_asymmetry_bump";
        addCause(r, "rebound_too_soft", 0.45, "increase_rebound_damping", "stiffer");
        addCause(r, "bump_too_stiff", 0.30, "reduce_bump_damping", "softer");
        addCause(r, "spring_preload_imbalanced", 0.15, "adjust_preload", "adjust");
        addCause(r, "other", 0.10, "review_setup", "review");
        r.validationChannels.push_back("damper_velocity");
        r.validationChannels.push_back("suspension_travel");
        r.validationChannels.push_back("body_height");
        r.validationChannels.push_back("tyre_load");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "damper_asymmetry_rebound";
        addCause(r, "bump_too_soft", 0.45, "increase_bump_damping", "stiffer");
        addCause(r, "rebound_too_stiff", 0.30, "reduce_rebound_damping", "softer");
        addCause(r, "spring_preload_imbalanced", 0.15, "adjust_preload", "adjust");
        addCause(r, "other", 0.10, "review_setup", "review");
        r.validationChannels.push_back("damper_velocity");
        r.validationChannels.push_back("suspension_travel");
        r.validationChannels.push_back("body_height");
        r.validationChannels.push_back("tyre_load");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "front_understeer_exit";
        addCause(r, "front_camber_insufficient", 0.35, "increase_front_camber", "increase");
        addCause(r, "front_roll_stiffness_low", 0.25, "stiffen_front_anti_roll", "stiffer");
        addCause(r, "front_pressure_low", 0.25, "increase_front_pressure", "increase");
        addCause(r, "aero_balance_front_heavy", 0.15, "reduce_front_downforce", "reduce");
        r.validationChannels.push_back("front_slip_angle");
        r.validationChannels.push_back("yaw_rate");
        r.validationChannels.push_back("lateral_g");
        r.validationChannels.push_back("exit_speed");
        m_rules.push_back(r);
    }
    {
        RuleSymptom r;
        r.id = "rear_oversteer_entry";
        addCause(r, "rear_spring_too_soft", 0.35, "stiffen_rear_spring", "stiffer");
        addCause(r, "rear_roll_stiffness_low", 0.30, "stiffen_rear_anti_roll", "stiffer");
        addCause(r, "rear_pressure_high", 0.20, "reduce_rear_pressure", "reduce");
        addCause(r, "differential_too_open", 0.15, "increase_diff_lock", "increase");
        r.validationChannels.push_back("rear_slip_angle");
        r.validationChannels.push_back("yaw_rate");
        r.validationChannels.push_back("lateral_g");
        r.validationChannels.push_back("entry_speed");
        m_rules.push_back(r);
    }

    // ---------- Alias de canaux : generic (tous profils) ----------
    const std::pair<const char*, const char*> genericTable[] = {
        {"ENGINE_SPEED", "SPEED"},
        {"GROUND_SPEED", "SPEED"},
        {"SPEED", "SPEED"},
        {"ENGINE_RPM", "RPMS"},
        {"RPM", "RPMS"},
        {"RPMS", "RPMS"},
        {"THROTTLE_POS", "THROTTLE"},
        {"THROTTLE", "THROTTLE"},
        {"BRAKE_POS", "BRAKE"},
        {"BRAKE", "BRAKE"},
        {"STEERING_POS", "STEERANGLE"},
        {"STEER_ANGLE", "STEERANGLE"},
        {"STEERANGLE", "STEERANGLE"},
        {"YAW_RATE", "ROTY"},
        {"ROTY", "ROTY"},
        {"G_FORCE_LAT", "LAT_G"},
        {"G_FORCE_LONG", "LONG_G"},
        {"LAT_G", "LAT_G"},
        {"LONG_G", "LONG_G"},
        {"LATERAL_G", "LAT_G"},
        {"LONGITUDINAL_G", "LONG_G"},
        {"SUSP_POS_FL", "SUS_TRAVEL_LF"},
        {"SUSP_POS_FR", "SUS_TRAVEL_RF"},
        {"SUSP_POS_RL", "SUS_TRAVEL_LR"},
        {"SUSP_POS_RR", "SUS_TRAVEL_RR"},
        {"SUSP_TRAVEL_LF", "SUS_TRAVEL_LF"},
        {"SUS_TRAVEL_RF", "SUS_TRAVEL_RF"},
        {"SUS_TRAVEL_LR", "SUS_TRAVEL_LR"},
        {"SUS_TRAVEL_RR", "SUS_TRAVEL_RR"},
        {"RIDE_HEIGHT_FL", "AERO_HEIGHT0"},
        {"RIDE_HEIGHT_FR", "AERO_HEIGHT1"},
        {"RIDE_HEIGHT_RL", "AERO_HEIGHT2"},
        {"RIDE_HEIGHT_RR", "AERO_HEIGHT3"},
        {"WHEEL_SPEED_FL", "WHEEL_SPEED_LF"},
        {"WHEEL_SPEED_FR", "WHEEL_SPEED_RF"},
        {"WHEEL_SPEED_RL", "WHEEL_SPEED_LR"},
        {"WHEEL_SPEED_RR", "WHEEL_SPEED_RR"},
        {"WHEEL_SPEED_FRONT_LEFT", "WHEEL_SPEED_LF"},
        {"WHEEL_SPEED_LEFT_FRONT", "WHEEL_SPEED_LF"},
        {"WHEEL_SPEED_LF", "WHEEL_SPEED_LF"},
        {"WHEEL_SPEED_RF", "WHEEL_SPEED_RF"},
        {"WHEEL_SPEED_LR", "WHEEL_SPEED_LR"},
        {"WHEEL_SPEED_RR", "WHEEL_SPEED_RR"},
        {"BRAKE_TEMP_FL", "BRAKE_TEMP_LF"},
        {"BRAKE_TEMP_FR", "BRAKE_TEMP_RF"},
        {"BRAKE_TEMP_RL", "BRAKE_TEMP_LR"},
        {"BRAKE_TEMP_RR", "BRAKE_TEMP_RR"},
        {"BRAKE_TEMP_FRONT_LEFT", "BRAKE_TEMP_LF"},
        {"BRAKE_TEMP_LF", "BRAKE_TEMP_LF"},
        {"BRAKE_TEMP_RF", "BRAKE_TEMP_RF"},
        {"BRAKE_TEMP_LR", "BRAKE_TEMP_LR"},
        {"BRAKE_TEMP_RR", "BRAKE_TEMP_RR"},
        {"TYRE_TEMP_FL", "C_SURF_TEMP_LF"},
        {"TYRE_TEMP_FR", "C_SURF_TEMP_RF"},
        {"TYRE_TEMP_RL", "C_SURF_TEMP_LR"},
        {"TYRE_TEMP_RR", "C_SURF_TEMP_RR"},
        {"TYRE_TEMP_FL_INNER", "TYRE_INNER_TEMP_LF"},
        {"TYRE_TEMP_FL_CENTRE", "TYRE_CENTRE_TEMP_LF"},
        {"TYRE_TEMP_FL_CENTER", "TYRE_CENTRE_TEMP_LF"},
        {"TYRE_TEMP_FL_OUTER", "TYRE_OUTER_TEMP_LF"},
        {"TYRE_TEMP_FR_INNER", "TYRE_INNER_TEMP_RF"},
        {"TYRE_TEMP_FR_CENTRE", "TYRE_CENTRE_TEMP_RF"},
        {"TYRE_TEMP_FR_CENTER", "TYRE_CENTRE_TEMP_RF"},
        {"TYRE_TEMP_FR_OUTER", "TYRE_OUTER_TEMP_RF"},
        {"TYRE_TEMP_RL_INNER", "TYRE_INNER_TEMP_LR"},
        {"TYRE_TEMP_RL_CENTRE", "TYRE_CENTRE_TEMP_LR"},
        {"TYRE_TEMP_RL_CENTER", "TYRE_CENTRE_TEMP_LR"},
        {"TYRE_TEMP_RL_OUTER", "TYRE_OUTER_TEMP_LR"},
        {"TYRE_TEMP_RR_INNER", "TYRE_INNER_TEMP_RR"},
        {"TYRE_TEMP_RR_CENTRE", "TYRE_CENTRE_TEMP_RR"},
        {"TYRE_TEMP_RR_CENTER", "TYRE_CENTRE_TEMP_RR"},
        {"TYRE_TEMP_RR_OUTER", "TYRE_OUTER_TEMP_RR"},
        {"TYRE_PRESS_FL", "TYRE_PRESS_LF"},
        {"TYRE_PRESS_FR", "TYRE_PRESS_RF"},
        {"TYRE_PRESS_RL", "TYRE_PRESS_LR"},
        {"TYRE_PRESS_RR", "TYRE_PRESS_RR"},
        {"TYRE_PRESSURE_FL", "TYRE_PRESS_LF"},
        {"TYRE_PRESSURE_FR", "TYRE_PRESS_RF"},
        {"TYRE_PRESSURE_RL", "TYRE_PRESS_LR"},
        {"TYRE_PRESSURE_RR", "TYRE_PRESS_RR"},
        {"TYRE_PRESS_LF", "TYRE_PRESS_LF"},
        {"TYRE_PRESS_RF", "TYRE_PRESS_RF"},
        {"TYRE_PRESS_LR", "TYRE_PRESS_LR"},
        {"TYRE_PRESS_RR", "TYRE_PRESS_RR"},
        {"AERO_HEIGHT_0", "AERO_HEIGHT0"},
        {"AERO_HEIGHT0", "AERO_HEIGHT0"},
        {"AERO_HEIGHT_1", "AERO_HEIGHT1"},
        {"AERO_HEIGHT1", "AERO_HEIGHT1"},
        {"AERO_HEIGHT_2", "AERO_HEIGHT2"},
        {"AERO_HEIGHT2", "AERO_HEIGHT2"},
        {"AERO_HEIGHT_3", "AERO_HEIGHT3"},
        {"AERO_HEIGHT3", "AERO_HEIGHT3"},
        {"AERO_BALANCE", "AERO_BALANCE"},
        {"LSD_LOCKING_TORQUE", "LSD_LOCKING_TORQUE"},
        // L/R SURF TEMP mappings - LEFT tyres: L=Outer, R=Inner | RIGHT tyres: L=Inner, R=Outer
        {"L_SURF_TEMP_LF", "TYRE_OUTER_TEMP_LF"},   // Left surface of left front = Outer
        {"R_SURF_TEMP_LF", "TYRE_INNER_TEMP_LF"},   // Right surface of left front = Inner
        {"C_SURF_TEMP_LF", "TYRE_CENTRE_TEMP_LF"},  // Center surface = Centre
        {"L_SURF_TEMP_RF", "TYRE_INNER_TEMP_RF"},   // Left surface of right front = Inner
        {"R_SURF_TEMP_RF", "TYRE_OUTER_TEMP_RF"},   // Right surface of right front = Outer
        {"C_SURF_TEMP_RF", "TYRE_CENTRE_TEMP_RF"},  // Center surface = Centre
        {"L_SURF_TEMP_LR", "TYRE_OUTER_TEMP_LR"},   // Left surface of left rear = Outer
        {"R_SURF_TEMP_LR", "TYRE_INNER_TEMP_LR"},   // Right surface of left rear = Inner
        {"C_SURF_TEMP_LR", "TYRE_CENTRE_TEMP_LR"},  // Center surface = Centre
        {"L_SURF_TEMP_RR", "TYRE_INNER_TEMP_RR"},   // Left surface of right rear = Inner
        {"R_SURF_TEMP_RR", "TYRE_OUTER_TEMP_RR"},   // Right surface of right rear = Outer
        {"C_SURF_TEMP_RR", "TYRE_CENTRE_TEMP_RR"},  // Center surface = Centre
        {"LAP_NUMBER", "LAP_NUMBER"},
        {"LAP_TIME", "LAP_TIME"},
        {"BEACON", "LAP_BEACON"},
        {"LAP_BEACON", "LAP_BEACON"},
        {"FUEL_LEVEL", "FUEL_LEVEL"},
        {"FUEL_PRES", "FUEL_PRES"},
        {"ENGINE_OIL_TEMP", "ENGINE_OIL_TEMP"},
        {"ENG_OIL_TEMP", "ENGINE_OIL_TEMP"},
        {"ENGINE_WATER_TEMP", "ENGINE_WATER_TEMP"},
        {"ENG_WATER_TEMP", "ENGINE_WATER_TEMP"},
        {"OIL_TEMP", "ENGINE_OIL_TEMP"},
        {"OIL_TEMPERATURE", "ENGINE_OIL_TEMP"},
        {"WATER_TEMP", "ENGINE_WATER_TEMP"},
        {"WATER_TEMPERATURE", "ENGINE_WATER_TEMP"},
        {"COOLANT_TEMP", "ENGINE_WATER_TEMP"},
        {"COOLANT_TEMPERATURE", "ENGINE_WATER_TEMP"},
        {"TRACK_TEMP", "TRACK_TEMP"},
        {"AMBIENT_TEMP", "AMBIENT_TEMP"},
        {"TRACKTEMPERATURE", "TRACK_TEMP"},
        {"AMBIENTTEMPERATURE", "AMBIENT_TEMP"},
        {"FUELLEVEL", "FUEL_LEVEL"},
        {"FUELPRESSURE", "FUEL_PRES"},
        // AMS2 suspension velocity (FL/FR/RL/RR naming -> LF/RF/LR/RR)
        {"SUSP_VEL_FL", "SUSP_VEL_LF"},
        {"SUSP_VEL_FR", "SUSP_VEL_RF"},
        {"SUSP_VEL_RL", "SUSP_VEL_LR"},
        {"SUSP_VEL_RR", "SUSP_VEL_RR"},
        // AMS2 brake bias setting
        {"BRAKE_BIAS_SETTING", "BRAKE_BIAS_SETTING"},
        // TCS / ABS activation (AMS2 "TCS Active" / "ABS Active")
        {"TCS_ACTIVE", "TCS_ACTIVE"},
        {"ABS_ACTIVE", "ABS_ACTIVE"}
    };
    for (std::size_t i = 0; i < sizeof(genericTable) / sizeof(genericTable[0]); ++i)
        m_genericAliases[genericTable[i].first] = genericTable[i].second;

    // ---------- Alias de canaux : profil EvoM1 uniquement ----------
    const std::pair<const char*, const char*> evoTable[] = {
        {"ENGINE_SPEED", "SPEED"},
        {"SPEED", "SPEED"},
        {"ROTY", "ROTY"},
        {"YAW_RATE", "ROTY"},
        {"G_LAT", "LAT_G"},
        {"LAT_G", "LAT_G"},
        {"G_LON", "LONG_G"},
        {"LONG_G", "LONG_G"},
        {"THROTTLE", "THROTTLE"},
        {"BRAKE", "BRAKE"},
        {"RPMS", "RPMS"},
        {"STEERANGLE", "STEERANGLE"},
        {"STEER_ANGLE", "STEERANGLE"},
        {"WHEEL_SPEED_LF", "WHEEL_SPEED_LF"},
        {"WHEEL_SPEED_RF", "WHEEL_SPEED_RF"},
        {"WHEEL_SPEED_LR", "WHEEL_SPEED_LR"},
        {"WHEEL_SPEED_RR", "WHEEL_SPEED_RR"},
        {"BRAKE_TEMP_LF", "BRAKE_TEMP_LF"},
        {"BRAKE_TEMP_RF", "BRAKE_TEMP_RF"},
        {"BRAKE_TEMP_LR", "BRAKE_TEMP_LR"},
        {"BRAKE_TEMP_RR", "BRAKE_TEMP_RR"},
        {"TYRE_PRESS_LF", "TYRE_PRESS_LF"},
        {"TYRE_PRESS_RF", "TYRE_PRESS_RF"},
        {"TYRE_PRESS_LR", "TYRE_PRESS_LR"},
        {"TYRE_PRESS_RR", "TYRE_PRESS_RR"},
        {"SUS_TRAVEL_LF", "SUS_TRAVEL_LF"},
        {"SUS_TRAVEL_RF", "SUS_TRAVEL_RF"},
        {"SUS_TRAVEL_LR", "SUS_TRAVEL_LR"},
        {"SUS_TRAVEL_RR", "SUS_TRAVEL_RR"},
        {"BUMPSTOPUP_TRAVEL_LF", "BUMPSTOPUP_TRAVEL_LF"},
        {"BUMPSTOPUP_TRAVEL_RF", "BUMPSTOPUP_TRAVEL_RF"},
        {"BUMPSTOPUP_TRAVEL_LR", "BUMPSTOPUP_TRAVEL_LR"},
        {"BUMPSTOPUP_TRAVEL_RR", "BUMPSTOPUP_TRAVEL_RR"},
        {"AERO_HEIGHT0", "AERO_HEIGHT0"},
        {"AERO_HEIGHT1", "AERO_HEIGHT1"},
        {"AERO_HEIGHT2", "AERO_HEIGHT2"},
        {"AERO_HEIGHT3", "AERO_HEIGHT3"},
        {"AERO_BALANCE", "AERO_BALANCE"},
        {"LSD_LOCKING_TORQUE", "LSD_LOCKING_TORQUE"},
        {"C_SURF_TEMP_LF", "TYRE_CENTRE_TEMP_LF"},
        {"C_SURF_TEMP_RF", "TYRE_CENTRE_TEMP_RF"},
        {"C_SURF_TEMP_LR", "TYRE_CENTRE_TEMP_LR"},
        {"C_SURF_TEMP_RR", "TYRE_CENTRE_TEMP_RR"},
        // L/R SURF TEMP mappings - LEFT tyres: L=Outer, R=Inner | RIGHT tyres: L=Inner, R=Outer
        {"L_SURF_TEMP_LF", "TYRE_OUTER_TEMP_LF"},
        {"R_SURF_TEMP_LF", "TYRE_INNER_TEMP_LF"},
        {"L_SURF_TEMP_RF", "TYRE_INNER_TEMP_RF"},
        {"R_SURF_TEMP_RF", "TYRE_OUTER_TEMP_RF"},
        {"L_SURF_TEMP_LR", "TYRE_OUTER_TEMP_LR"},
        {"R_SURF_TEMP_LR", "TYRE_INNER_TEMP_LR"},
        {"L_SURF_TEMP_RR", "TYRE_INNER_TEMP_RR"},
        {"R_SURF_TEMP_RR", "TYRE_OUTER_TEMP_RR"},
        {"TYRE_INNER_TEMP_LF", "TYRE_INNER_TEMP_LF"},
        {"TYRE_CENTRE_TEMP_LF", "TYRE_CENTRE_TEMP_LF"},
        {"TYRE_OUTER_TEMP_LF", "TYRE_OUTER_TEMP_LF"},
        {"TYRE_INNER_TEMP_RF", "TYRE_INNER_TEMP_RF"},
        {"TYRE_CENTRE_TEMP_RF", "TYRE_CENTRE_TEMP_RF"},
        {"TYRE_OUTER_TEMP_RF", "TYRE_OUTER_TEMP_RF"},
        {"TYRE_INNER_TEMP_LR", "TYRE_INNER_TEMP_LR"},
        {"TYRE_CENTRE_TEMP_LR", "TYRE_CENTRE_TEMP_LR"},
        {"TYRE_OUTER_TEMP_LR", "TYRE_OUTER_TEMP_LR"},
        {"TYRE_INNER_TEMP_RR", "TYRE_INNER_TEMP_RR"},
        {"TYRE_CENTRE_TEMP_RR", "TYRE_CENTRE_TEMP_RR"},
        {"TYRE_OUTER_TEMP_RR", "TYRE_OUTER_TEMP_RR"},
        // EVO brake torque
        {"BRAKE_TORQUE_LF", "BRAKE_TORQUE_LF"},
        {"BRAKE_TORQUE_RF", "BRAKE_TORQUE_RF"},
        {"BRAKE_TORQUE_LR", "BRAKE_TORQUE_LR"},
        {"BRAKE_TORQUE_RR", "BRAKE_TORQUE_RR"},
        // EVO wheel slip ratio
        {"WHEEL_SR_LF", "WHEEL_SR_LF"},
        {"WHEEL_SR_RF", "WHEEL_SR_RF"},
        {"WHEEL_SR_LR", "WHEEL_SR_LR"},
        {"WHEEL_SR_RR", "WHEEL_SR_RR"},
        // EVO tyre air temperature
        {"TYRE_TAIR_LF", "TYRE_TAIR_LF"},
        {"TYRE_TAIR_RF", "TYRE_TAIR_RF"},
        {"TYRE_TAIR_LR", "TYRE_TAIR_LR"},
        {"TYRE_TAIR_RR", "TYRE_TAIR_RR"},
        // EVO TCS / ABS activation (TC / ABS)
        {"TC", "TCS_ACTIVE"},
        {"ABS", "ABS_ACTIVE"}
    };
    for (std::size_t i = 0; i < sizeof(evoTable) / sizeof(evoTable[0]); ++i)
        m_evoAliases[evoTable[i].first] = evoTable[i].second;
}
