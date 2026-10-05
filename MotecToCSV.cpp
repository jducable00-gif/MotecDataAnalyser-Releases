//---------------------------------------------------------------------------
// MotecToCSV.cpp — Décodage du format binaire LD MoTeC.
//
// Lit le fichier .ld (enchaînement de blocs pointés par l'en-tête), extrait
// chaque canal (nom, unité, fréquence, valeurs) et les marqueurs de balises
// depuis le fichier .ldx. ConvertMotecLdToCsv ré-écrit le tout au format CSV.
//
// NB : le décodage LD est indépendant de l'analyse CSV (voir AGENTS.md).
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

#pragma hdrstop

#include "MotecToCSV.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// Classe RAII pour gérer les handles Windows
class RegistryKeyHandle {
public:
    explicit RegistryKeyHandle(HKEY key = NULL) : m_key(key) {}
    ~RegistryKeyHandle() { if (m_key != NULL) RegCloseKey(m_key); }
    operator HKEY() const { return m_key; }

private:
    HKEY m_key;
};

// Classe RAII pour gérer les handles de fichiers Windows
class FindFileHandle {
public:
    explicit FindFileHandle(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {}
    ~FindFileHandle() { if (m_handle != INVALID_HANDLE_VALUE) FindClose(m_handle); }
    operator HANDLE() const { return m_handle; }

private:
    HANDLE m_handle;
};

std::string MotecExePath;
std::string MotecExportError;

const std::string& GetMotecExportError()
{
    return MotecExportError;
}

unsigned short ReadU16(const std::vector<unsigned char>& data, size_t offset)
{
    return (unsigned short)(data[offset] | (data[offset + 1] << 8));
}

short ReadI16(const std::vector<unsigned char>& data, size_t offset)
{
    return (short)ReadU16(data, offset);
}

unsigned long ReadU32(const std::vector<unsigned char>& data, size_t offset)
{
    return (unsigned long)data[offset] |
        ((unsigned long)data[offset + 1] << 8) |
        ((unsigned long)data[offset + 2] << 16) |
        ((unsigned long)data[offset + 3] << 24);
}

std::string ReadText(const std::vector<unsigned char>& data, size_t offset, size_t length)
{
    std::string result;
    for (size_t index = 0; index < length && offset + index < data.size(); ++index)
    {
        if (data[offset + index] == 0)
            break;
        result += (char)data[offset + index];
    }
    return result;
}

std::vector<double> ReadBeaconMarkers(const std::string& inputFile)
{
    // Companion XML is <stem>.ldx regardless of whether the input was .ld or .ldx.
    std::string ldxPath;
    const size_t dot = inputFile.find_last_of('.');
    if (dot != std::string::npos && inputFile.compare(dot, 3, ".ld") == 0) {
        // Input is a .ld file → append 'x' to get .ldx
        ldxPath = inputFile + "x";
    } else if (dot != std::string::npos) {
        // Input has some other extension (e.g. .ldx) → replace with .ldx
        ldxPath = inputFile.substr(0, dot) + ".ldx";
    } else {
        // No extension: assume bare stem
        ldxPath = inputFile + ".ldx";
    }

    std::ifstream ldx(ldxPath.c_str());
    std::vector<double> markers;
    if (!ldx)
        return markers;

    std::string xml((std::istreambuf_iterator<char>(ldx)), std::istreambuf_iterator<char>());
    std::string::size_type position = 0;
    while ((position = xml.find("Time=\"", position)) != std::string::npos)
    {
        position += 6;
        char* end = NULL;
        double marker = std::strtod(xml.c_str() + position, &end);
        if (end != xml.c_str() + position)
            markers.push_back(marker / 1000000.0);
    }
    return markers;
}

bool WriteCsvValue(std::ofstream& output, const std::string& value)
{
    output << '"';
    for (size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] == '"')
            output << '"';
        output << value[index];
    }
    output << '"';
    return output.good();
}

void WriteCsvNumber(std::ofstream& output, double value)
{
	output << std::setprecision(6) << std::defaultfloat << value;
}

void WriteI2Value(std::ofstream& output, const MotecColumnInfo& column, double value)
{
    double displayValue = value;
    int precision = 0;

    if (column.name == "SPEED")
    {
        displayValue *= 3.6;
        precision = 1;
    }
    else if (column.name == "ROTY")
    {
        displayValue *= 57.295779513;
        precision = 2;
    }
    else if (column.name == "AERO_BALANCE" || column.unit == "m/s2" || column.unit == "rad/s" ||
        column.unit == "deg/s" || column.unit == "deg")
    {
        precision = 2;
    }

    output << std::fixed << std::setprecision(precision) << displayValue;
}

std::string I2Unit(const MotecColumnInfo& column)
{
    if (column.name == "SPEED")
        return "km/h";
    if (column.name == "ROTY")
        return "deg/s";
    if (column.unit == "Nm")
        return "N.m";
    return column.unit;
}

// Reads a NUL-terminated text field from the LD header (fixed offsets).
static std::string readHeaderField(const std::vector<unsigned char>& data, size_t offset, size_t length) {
    std::string result;
    for (size_t i = 0; i < length && offset + i < data.size(); ++i) {
        unsigned char c = data[offset + i];
        if (c == 0) break;
        result.push_back(static_cast<char>(c));
    }
    return result;
}

bool DecodeMotecLd(const std::string& inputFile,
                   std::vector<MotecColumnInfo>& columns,
                   std::vector<double>& beaconMarkers,
                   std::string& errorMessage,
                   ExportProgressCallback progressCallback)
{
    errorMessage.clear();
    MotecExportError.clear();
    if (progressCallback)
        progressCallback(0);

    std::ifstream input(inputFile.c_str(), std::ios::binary);
    if (!input)
    {
        errorMessage = "Could not open the selected LD file.";
        MotecExportError = errorMessage;
        return false;
    }

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (data.size() < 1636)
    {
        errorMessage = "The LD file is too small or has an unsupported format.";
        MotecExportError = errorMessage;
        return false;
    }

    // Session metadata lives at fixed header offsets. Read it once here so
    // callers (parseMotecLd) no longer need to re-open and buffer the whole file.
    const std::string headerDriver  = readHeaderField(data, 158, 64);
    const std::string headerVehicle = readHeaderField(data, 222, 128);
    const std::string headerTrack   = readHeaderField(data, 350, 128);

    unsigned long headerStart = ReadU32(data, 8);
    std::vector<unsigned long> dataPointers;
    std::vector<unsigned long> blocks;
    std::vector<unsigned long> visited;
    bool hasIncompleteData = false;
    unsigned long block = headerStart;
    while (block != 0 && block + 24 < data.size() &&
        std::find(visited.begin(), visited.end(), block) == visited.end())
    {
        visited.push_back(block);
        blocks.push_back(block);
        unsigned long nextBlock = ReadU32(data, block + 4);
        unsigned long dataStart = ReadU32(data, block + 8);
        if (dataStart != 0 && dataStart < data.size())
            dataPointers.push_back(dataStart);
        else if (dataStart != 0)
            hasIncompleteData = true;
        block = nextBlock;
    }

    columns.clear();
    for (size_t index = 0; index < blocks.size(); ++index)
    {
        unsigned long start = blocks[index];
        unsigned long dataStart = ReadU32(data, start + 8);
        unsigned long count = ReadU32(data, start + 12);
        unsigned short type = ReadU16(data, start + 18);
        unsigned short entrySize = ReadU16(data, start + 20);
        int frequency = ReadI16(data, start + 22);
        int decimalShift = ReadI16(data, start + 30);
        if (dataStart >= data.size() || count == 0 || frequency <= 0 ||
            (entrySize != 2 && entrySize != 4))
            continue;

        unsigned long available = (unsigned long)data.size() - dataStart;
        unsigned long maxCount = available / entrySize;
        if (count > maxCount)
            count = maxCount;

        MotecColumnInfo column;
        column.name = ReadText(data, start + 32, 32);
        column.unit = ReadText(data, start + 72, 8);
        column.frequency = frequency;
        column.decimalShift = decimalShift;
        column.type = type;
        column.values.reserve(count);
        double divisor = (type != 7) ? std::pow(10.0, decimalShift) : 1.0;
        // Throttle progress: ~20 updates per channel instead of one per sample
        // (per-sample VCL UI updates froze the window on large EVO sessions).
        const std::size_t progressStep = std::max<std::size_t>(1, static_cast<std::size_t>(count) / 20);
        for (unsigned long sample = 0; sample < count; ++sample)
        {
            size_t offset = dataStart + sample * entrySize;
            double value;
            if (entrySize == 2)
                value = ReadI16(data, offset);
            else if (type == 7)
            {
                float floatValue;
                std::memcpy(&floatValue, &data[offset], sizeof(floatValue));
                value = floatValue;
            }
            else
                value = (long)(ReadU32(data, offset));

            if (type != 7)
                value /= divisor;
            column.values.push_back(value);
            if (progressCallback && ((sample + 1) % progressStep == 0))
            {
                int percent = static_cast<int>(((index + (double)(sample + 1) / count)
                                / std::max<size_t>(1, blocks.size())) * 50.0);
                if (percent >= 0 && percent <= 50)
                    progressCallback(percent);
            }
        }
        // End-of-block update so the bar never stalls between channels.
        if (progressCallback)
        {
            int percent = static_cast<int>(((index + 1.0) / std::max<size_t>(1, blocks.size())) * 50.0);
            if (percent >= 0 && percent <= 50)
                progressCallback(percent);
        }
        // Every decoded column carries the session metadata (first non-empty
        // column is the one parseMotecLd reads it from).
        column.driverName  = headerDriver;
        column.vehicleName = headerVehicle;
        column.trackName   = headerTrack;
        columns.push_back(column);
    }

    if (columns.empty())
    {
        std::ostringstream details;
        details << (hasIncompleteData ? "The LD file appears to be incomplete or truncated. " :
                        "No supported channels were found in the LD file. ")
                << "Size=" << data.size()
                << ", Header=" << headerStart
                << ", Blocks=" << blocks.size();
        errorMessage = details.str();
        MotecExportError = errorMessage;
        return false;
    }

    beaconMarkers = ReadBeaconMarkers(inputFile);
    return true;
}

bool ConvertMotecLdToCsv(const std::string& inputFile, const std::string& outputCsv,
                         ExportProgressCallback progressCallback)
{
    MotecExportError.clear();
    if (progressCallback)
        progressCallback(0);

    std::vector<MotecColumnInfo> columns;
    std::vector<double> beaconMarkers;
    std::string decodeError;
    if (!DecodeMotecLd(inputFile, columns, beaconMarkers, decodeError, progressCallback))
    {
        MotecExportError = decodeError;
        return false;
    }

    std::ifstream input(inputFile.c_str(), std::ios::binary);
    if (!input)
    {
        MotecExportError = "Could not open the selected LD file.";
        return false;
    }

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (data.size() < 1636)
    {
        MotecExportError = "The LD file is too small or has an unsupported format.";
        return false;
    }

    unsigned long raceTypeStart = ReadU32(data, 36);
    int masterFrequency = 1;
    double duration = 0.0;
    for (size_t index = 0; index < columns.size(); ++index)
    {
        masterFrequency = std::max(masterFrequency, columns[index].frequency);
        duration = std::max(duration, (double)columns[index].values.size() / columns[index].frequency);
    }
    unsigned long sampleCount = (unsigned long)(duration * masterFrequency);
    size_t speedColumn = columns.size();
    for (size_t index = 0; index < columns.size(); ++index)
    {
        if (columns[index].name == "SPEED")
        {
            speedColumn = index;
            break;
        }
    }

    std::ofstream output(outputCsv.c_str(), std::ios::out | std::ios::trunc);
    if (!output)
    {
        MotecExportError = "The CSV output file could not be created.";
        return false;
    }
    output << "\"Format\",\"MoTeC CSV File\"\n";
    output << "\"Device\","; WriteCsvValue(output, ReadText(data, 74, 8)); output << "\n";
    output << "\"Date\","; WriteCsvValue(output, ReadText(data, 92, 34)); output << "\n";
    output << "\"Time\","; WriteCsvValue(output, ReadText(data, 126, 32)); output << "\n";
    output << "\"Driver\","; WriteCsvValue(output, ReadText(data, 158, 64)); output << "\n";
    output << "\"Vehicle\","; WriteCsvValue(output, ReadText(data, 222, 128)); output << "\n";
    output << "\"Track\","; WriteCsvValue(output, ReadText(data, 350, 128)); output << "\n";
    if (raceTypeStart < data.size())
    {
        output << "\"Type\",";
        WriteCsvValue(output, ReadText(data, raceTypeStart, 64));
        output << "\n";
    }
    output << "\"Comment\",";
    WriteCsvValue(output, ReadText(data, 1572, 64));
    output << "\n\"Beacon Markers\",";
    for (size_t index = 0; index < beaconMarkers.size(); ++index)
    {
        if (index > 0)
            output << ' ';
        output << std::setprecision(6) << beaconMarkers[index];
    }
    output << "\n\n";

    WriteCsvValue(output, "Time");
    output << ',';
    WriteCsvValue(output, "Distance");
    for (size_t index = 0; index < columns.size(); ++index)
    {
        output << ',';
        WriteCsvValue(output, columns[index].name);
    }
    output << "\n";
    WriteCsvValue(output, "s");
    output << ',';
    WriteCsvValue(output, "m");
    for (size_t index = 0; index < columns.size(); ++index)
    {
        output << ',';
        WriteCsvValue(output, I2Unit(columns[index]));
    }
    output << "\n";

    double distance = 0.0;
    for (unsigned long row = 0; row < sampleCount; ++row)
    {
        double currentTime = (double)row / masterFrequency;
        output << std::fixed << std::setprecision(3) << currentTime;
        output << ',';
        output << std::fixed << std::setprecision(0) << distance;
        for (size_t index = 0; index < columns.size(); ++index)
        {
            unsigned long sample = (unsigned long)(currentTime * columns[index].frequency + 0.000001);
            output << ',';
            if (sample < columns[index].values.size())
                WriteI2Value(output, columns[index], columns[index].values[sample]);
        }
        output << '\n';
        if (speedColumn < columns.size())
        {
            unsigned long speedSample = (unsigned long)(currentTime * columns[speedColumn].frequency + 0.000001);
            if (speedSample < columns[speedColumn].values.size())
                distance += columns[speedColumn].values[speedSample] / masterFrequency;
        }
        if (progressCallback && sampleCount > 0)
            progressCallback(50 + (int)(((row + 1.0) / sampleCount) * 50.0));
    }

    output.close();
    if (!output)
    {
        MotecExportError = "An error occurred while writing the CSV file.";
        return false;
    }

    return true;
}

bool ExportMotecToCsv(const std::string& motecExePath,
                      const std::string& inputFile,
                      const std::string& outputCsv,
                      ExportProgressCallback progressCallback)
{
    (void)motecExePath;
    return ConvertMotecLdToCsv(inputFile, outputCsv, progressCallback);
}

