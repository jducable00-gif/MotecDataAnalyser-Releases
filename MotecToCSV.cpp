//---------------------------------------------------------------------------
// MotecToCSV.cpp - Decodage du format binaire LD MoTeC.
//
// Lit le fichier .ld (enchainement de blocs pointes par l'en-tete), extrait
// chaque canal (nom, unite, frequence, valeurs) et les marqueurs de balises
// depuis le fichier .ldx.
//
// NB : le decodage LD est independant de l'analyse CSV (voir AGENTS.md).
//---------------------------------------------------------------------------

#pragma hdrstop

#include "MotecToCSV.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

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
        // Input is a .ld file â†’ append 'x' to get .ldx
        ldxPath = inputFile + "x";
    } else if (dot != std::string::npos) {
        // Input has some other extension (e.g. .ldx) â†’ replace with .ldx
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
    if (progressCallback)
        progressCallback(0);

    std::ifstream input(inputFile.c_str(), std::ios::binary);
    if (!input)
    {
        errorMessage = "Could not open the selected LD file.";
        return false;
    }

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (data.size() < 1636)
    {
        errorMessage = "The LD file is too small or has an unsupported format.";
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
        return false;
    }

    beaconMarkers = ReadBeaconMarkers(inputFile);
    return true;
}

