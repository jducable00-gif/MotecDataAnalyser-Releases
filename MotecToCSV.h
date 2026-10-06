//---------------------------------------------------------------------------
// MotecToCSV.h — Décodage binaire des fichiers LD/LDX MoTeC.
//
// Déclare la structure MotecColumnInfo (une colonne décodée) et la fonction
// DecodeMotecLd (lecture des blocs de données).
// L'analyse elle-même (parseMotecLd) réutilise DecodeMotecLd.
//---------------------------------------------------------------------------

#ifndef MotecToCSVH
#define MotecToCSVH
//---------------------------------------------------------------------------
#include <string>
#include <vector>

typedef void (*ExportProgressCallback)(int percent);

struct MotecColumnInfo
{
    std::string name;
    std::string unit;
    int frequency = 0;
    int decimalShift = 0;
    unsigned short type = 0;
    std::vector<double> values;
    // Session metadata (filled once from the file header by DecodeMotecLd,
    // so callers no longer need to re-read the whole LD a second time).
    std::string driverName;   // header offset 158, max 64 chars
    std::string vehicleName;  // header offset 222, max 128 chars
    std::string trackName;    // header offset 350, max 128 chars
};

bool DecodeMotecLd(const std::string& inputFile,
                   std::vector<MotecColumnInfo>& columns,
                   std::vector<double>& beaconMarkers,
                   std::string& errorMessage,
                   ExportProgressCallback progressCallback = NULL);

#endif
