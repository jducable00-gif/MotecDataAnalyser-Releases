//---------------------------------------------------------------------------
// VehicleDb.h -- Base de vehicules AMS2 integree dans le binaire (Layer 1).
// ATTENTION : cette base ne couvre QUE les vehicules d'Assetto Corsa Mod 2.
// AC EVO et PMR n'y sont pas representes ; pour ces sims, les donnees theoriques
// doivent provenir d'une autre source (ou rester absentes -> has* == false).
//
// Le fichier data/reference/ams2_vehicles_db.json est embarque sous forme de
// tableau d'octets dans VehicleDb.cpp (genere par scripts/generate_vehicle_db.py)
// et parse au premier acces avec le JsonParser de RuleConfig.h. L'application ne
// lit donc plus aucun fichier externe pour les donnees theoriques des vehicules :
// plages min/max, pas, valeurs par defaut, masses/CG, temperatures optimales...
// (voir .ai/skills/setup_analysis.md -- "The Three Layers of Truth", Layer 1).
//
// Usage:
//     const Ams2VehicleDb& db = Ams2VehicleDb::instance();
//     if (!db.isValid()) { ... }                       // JSON corrompu/absent
//     VehicleInfo car;
//     if (db.find("Alpine_A110_GT4_Evo", car))         // exact, puis insensible a la casse
//         use(car.chassis.massKg);                     // 1165.0
//---------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

class JsonValue;   // RuleConfig.h -- forward decl (parse interne au .cpp)

// ===============================
// Structures typrees de la base vehicules AMS2.
// Chaque champ porte un "present" : le schema JSON evolue, on ne veut pas
// d'undefined behavior sur les champs manquants des anciennes versions du DB.
// ===============================

struct WheelBrakeData {
    bool present = false;          // section "brakes.<FL|FR|RL|RR>" presente ?
    double optTempMinC = 0.0, optTempMaxC = 0.0;   // OptimumTempRange [min,max] (deg C)
    double fadeTorqueLossBelow = 0.0, fadeTorqueLossAbove = 0.0;
    bool hasFadeTemps = false;     // FadeRangeBelow/Above presents ?
    double fadeRangeBelowC = 0.0, fadeRangeAboveC = 0.0;

    WheelBrakeData() {}            // C++Builder: constructeur explicite vide requis
};

struct ChassisPhysics {
    bool present = false;          // section "physics.chassis" presente ?
    double massKg = 0.0, hasMass = true;      // Mass (kg) -- quasi toujours present
    double cgHeightM = 0.0, hasCgHeight = false;   // CGHeight (m)
    double cgRightRangeM = 0.0, hasCgRightRange = false;  // CGRightRange (m)
    double cgRearRangeM = 0.0, hasCgRearRange = false;     // CGRearRange (m)

    WheelBrakeData brakes[4];      // [FL, FR, RL, RR] -- index: kWheelFl=0..kWheelRr=3
};

struct EnginePhysics {
    bool present = false;          // section "physics.engine" presente ?
    double optOilTempC = 0.0, hasOptOilTemp = false;      // OptimumOilTemp (deg C)
    double oilWarningTempC = 0.0, hasOilWarningTemp = false;   // OilWarningTemp
    double waterWarningTempC = 0.0, hasWaterWarningTemp = false;// WaterWarningTemp

    EnginePhysics() {}             // C++Builder: constructeur explicite vide requis
};

struct VehicleInfo {
    std::string name;              // cle du DB (ex "Alpine_A110_GT4_Evo")
    bool valid = false;            // true si l'entree a ete trouvee + parsee
    ChassisPhysics chassis;
    EnginePhysics engine;

    VehicleInfo() {}               // C++Builder: constructeur explicite vide requis
};

// ===============================
// Ams2VehicleDb -- point d'accès unique à la base embarquée (singleton).
// Le JSON est parsé une seule fois, au premier appel. Si le parse échoue
// (fichier corrompu), isValid() == false et find()/vehicleCount() sont sûrs.
// ===============================

class Ams2VehicleDb {
public:
    static const int kWheelFl = 0;   // index dans ChassisPhysics::brakes[]
    static const int kWheelFr = 1;
    static const int kWheelRl = 2;
    static const int kWheelRr = 3;

    static Ams2VehicleDb& instance();

    bool isValid() const { return m_valid; }        // JSON embarqué parse avec succès ?
    std::size_t vehicleCount() const;               // nombre de véhicules (0 si invalide)

    // Noms des véhicules, triés par ordre lexicographique (déterministe).
    void allNames(std::vector<std::string>& out) const;

    // Recherche : d'abord correspondance exacte sur la clé du DB, puis insensible à
    // la casse. Retourne false si aucun véhicule ne correspond ; car est alors vide.
    bool find(const std::string& name, VehicleInfo& car) const;

private:
    Ams2VehicleDb();                 // parse le JSON embarqué une seule fois
    ~Ams2VehicleDb() {}              // non copiable -- singleton (constructeur privé)

    Ams2VehicleDb(const Ams2VehicleDb&);            // interdits : pas de copie du DB
    Ams2VehicleDb& operator=(const Ams2VehicleDb&);

    bool m_valid;                    // true si le parse initial a réussi
    std::vector<VehicleInfo> m_vehicles;   // entrees valides, triees par nom (insensible casse)
    std::vector<std::string> m_lowerIndex;  // noms en minuscules, meme ordre que m_vehicles
};
