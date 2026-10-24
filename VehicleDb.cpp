//---------------------------------------------------------------------------
// VehicleDb.cpp -- Base de vehicules AMS2 integree dans le binaire (Layer 1).
//
// Les octets du JSON vivent dans VehicleDb.generated.inc, genere par :
//     python scripts/generate_vehicle_db.py
// Ce fichier ne fait que parser ces octets une seule fois (JsonParser de
// RuleConfig.h) et exposer les donnees via l'API typree de VehicleDb.h.
//---------------------------------------------------------------------------

#include "VehicleDb.h"
#include "RuleConfig.h"      // JsonValue, JsonParser

#include <algorithm>         // std::sort
#include <cctype>            // tolower (recherche insensible a la casse)

// Octets du JSON embarque : genere par scripts/generate_vehicle_db.py.
#include "VehicleDb.generated.inc"   // kAms2VehiclesJson[] (taille = sizeof)

namespace {

double vNum(const JsonValue* v) {
    return (v && v->type == JsonValue::Number) ? v->number : 0.0;
}

bool isNum(const JsonValue* v) {
    return (v && v->type == JsonValue::Number);
}

std::string toLowerCopy(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(static_cast<unsigned char>(tolower(s[i])));
    return s;
}
// Forme canonique pour la recherche : minuscules + '_' remplace par ' '.
// Les cles du DB utilisent des espaces ("Alpine A110 GT4 Evo") tandis que les
// noms recus (gfx.car_model en live, colonnes LD) utilisent souvent des
// tirets-bas. Normaliser les deux cotés rend find() insensible aux deux.
std::string normalizeName(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = static_cast<char>(static_cast<unsigned char>(tolower(s[i])));
        if (c == '_') c = ' ';
        s[i] = c;
    }
    return s;
}
// CGRightRange / CGRearRange : deux formes dans le DB.
//  - scalaire            -> valeur fixe (hasFixedValue)
//  - objet {min,step,max}-> plage reglable ; la position courante vient du champ
//    frere <settingKey> (= min + step * setting). La presence de l'objet est en
//    elle-meme une information : le CG peut etre deplace dans les reglages.
void parseCgRange(const JsonValue* ch, const char* rangeKey,
                  const char* settingKey, CgRangeData& out) {
    if (!ch || ch->type != JsonValue::Object) return;
    const JsonValue* v = ch->find(rangeKey);
    if (isNum(v)) {                                   // forme scalaire : CG fixe
        out.present       = true;
        out.hasFixedValue = true;
        out.fixedValueM   = v->number;
        return;
    }
    if (!v || v->type != JsonValue::Object) return;   // cle absente ou forme inconnue
    const JsonValue* mn  = v->find("min");
    if (isNum(mn)) {                                  // forme objet : plage reglable
        out.present       = true;
        out.minM          = mn->number;
        const JsonValue* stp = v->find("step");
        if (isNum(stp))  out.stepM         = stp->number;
        const JsonValue* mx  = v->find("max");
        if (isNum(mx))   out.maxPositions  = mx->number;
    } else {                                          // objet sans min : inutilisable
        return;
    }
    const JsonValue* s = ch->find(settingKey);
    if (isNum(s)) {                                   // position courante selectionnee
        out.hasSetting   = true;
        out.settingIndex = s->number;
    }
}

void parseWheelBrake(const JsonValue* obj, WheelBrakeData& out) {
    if (!obj || obj->type != JsonValue::Object) return;
    const JsonValue* range = obj->find("OptimumTempRange");
    if (range && range->type == JsonValue::Array && range->array.size() >= 2) {
        out.optTempMinC = vNum(&range->array[0]);
        out.optTempMaxC = vNum(&range->array[1]);
    }
    const JsonValue* fB = obj->find("FadeTorqueLossBelow");
    if (isNum(fB)) { out.fadeTorqueLossBelow = fB->number; }
    const JsonValue* fA = obj->find("FadeTorqueLossAbove");
    if (isNum(fA)) { out.fadeTorqueLossAbove = fA->number; }
    const JsonValue* rB = obj->find("FadeRangeBelow");
    const JsonValue* rA = obj->find("FadeRangeAbove");
    if (isNum(rB) && isNum(rA)) {
        out.hasFadeTemps = true;
        out.fadeRangeBelowC = rB->number;
        out.fadeRangeAboveC = rA->number;
    }
    out.present = true;
}

void parseVehicle(const std::string& name, const JsonValue* entry, VehicleInfo& car) {
    if (!entry || entry->type != JsonValue::Object) return;
    car.name = name;

    const JsonValue* physics = entry->find("physics");
    if (physics && physics->type == JsonValue::Object) {
        // --- chassis -------------------------------------------------
        const JsonValue* ch = physics->find("chassis");
        if (ch && ch->type == JsonValue::Object) {
            car.chassis.present = true;
            const JsonValue* m  = ch->find("Mass");         if (isNum(m))  car.chassis.massKg      = m->number;
            const JsonValue* cg = ch->find("CGHeight");     if (isNum(cg)) { car.chassis.cgHeightM    = cg->number; car.chassis.hasCgHeight   = true; }

            parseCgRange(ch, "CGRightRange", "CGRightSetting", car.chassis.cgRight);
            parseCgRange(ch, "CGRearRange",  "CGRearSetting",  car.chassis.cgRear);


            const char* wheelKeys[4] = {"FL", "FR", "RL", "RR"};
            const JsonValue* brakes = ch->find("brakes");
            if (brakes && brakes->type == JsonValue::Object) {
                for (int w = 0; w < 4; ++w)
                    parseWheelBrake(brakes->find(wheelKeys[w]), car.chassis.brakes[w]);
            }
        }

        // --- engine --------------------------------------------------
        const JsonValue* eng = physics->find("engine");
        if (eng && eng->type == JsonValue::Object) {
            car.engine.present = true;
            const JsonValue* oo = eng->find("OptimumOilTemp");   if (isNum(oo)) { car.engine.optOilTempC    = oo->number; car.engine.hasOptOilTemp  =true; }
            const JsonValue* ow = eng->find("OilWarningTemp");   if (isNum(ow)) { car.engine.oilWarningTempC= ow->number; car.engine.hasOilWarningTemp=true;}
            const JsonValue* ww = eng->find("WaterWarningTemp"); if (isNum(ww)) { car.engine.waterWarningTempC=ww->number;car.engine.hasWaterWarningTemp=true;}
        }
    }

    // Une entree est "valide" si la section chassis existe (le schema du DB est
    // uniforme : Mass y figure toujours). Les champs manquants restent signales
    // par leurs drapeaux has* individuels.
    car.valid = car.chassis.present;
}

// Comparateur par nom (minuscules) pour le tri deterministe de m_vehicles.
struct NameLess {
    bool operator()(const VehicleInfo& a, const VehicleInfo& b) const {
        return toLowerCopy(a.name) < toLowerCopy(b.name);
    }
};

} // namespace

// ===============================
// Ams2VehicleDb -- implementation
// ===============================

Ams2VehicleDb& Ams2VehicleDb::instance() {
    static Ams2VehicleDb s_instance;   // C++11 : initialisation thread-safe garantie
    return s_instance;
}

std::size_t Ams2VehicleDb::vehicleCount() const {
    return m_vehicles.size();
}

void Ams2VehicleDb::allNames(std::vector<std::string>& out) const {
    out.clear();
    for (std::size_t i = 0; i < m_lowerIndex.size(); ++i)
        out.push_back(m_vehicles[i].name);   // copie explicite, pas de move (style projet)
}

bool Ams2VehicleDb::find(const std::string& name, VehicleInfo& car) const {
    if (!m_valid || m_lowerIndex.empty()) return false;

    // Recherche binaire sur l'index normalise (minuscules + '_' -> ' ') : la
    // correspondance est insensible a la casse ET au tiret-bas/espace.
    std::string norm = normalizeName(name);
    if (norm.empty()) return false;
    std::size_t lo = 0, hi = m_lowerIndex.size();
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        if (m_lowerIndex[mid] == norm) {
            car = m_vehicles[mid];   // copie explicite, pas de move (style projet)
            return true;
        }
        if (m_lowerIndex[mid] < norm) lo = mid + 1; else hi = mid;
    }

    return false;
}

Ams2VehicleDb::Ams2VehicleDb() : m_valid(false) {
    std::string text(reinterpret_cast<const char*>(kAms2VehiclesJson), sizeof(kAms2VehiclesJson));
    JsonValue root;
    std::string error;
    if (!JsonParser::parse(text, root, error)) return;   // JSON corrompu : DB invalide

    for (std::size_t i = 0; i < root.object.size(); ++i) {
        const std::string& key = root.object[i].first;
        VehicleInfo car;
        parseVehicle(key, &root.object[i].second, car);
        if (!car.valid) continue;                        // entree incomplete : ignoree

        m_vehicles.push_back(car);                       // copie explicite (style projet)
    }

    std::sort(m_vehicles.begin(), m_vehicles.end(), NameLess());

    for (std::size_t i = 0; i < m_vehicles.size(); ++i) {
        m_lowerIndex.push_back(normalizeName(m_vehicles[i].name)); // copie explicite
    }

    m_valid = !m_vehicles.empty();
}
