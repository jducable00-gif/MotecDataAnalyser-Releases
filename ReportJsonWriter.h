//---------------------------------------------------------------------------
// ReportJsonWriter.h - Phase A (Tablet Report Viewer plan, docs/TABLET_REPORT_VIEWER_PLAN.md §3).
// Header-only JSON dumper for the tablet payload. No VCL: pure std::string I/O so it can be
// unit-tested standalone and later consumed by HttpServer.cpp without pulling GUI deps in.
// UTF-8 safe: multi-byte sequences pass through untouched; only '"', '\' and control chars
// below 0x20 are escaped (RFC 8259 §7). French accents therefore round-trip intact.
//---------------------------------------------------------------------------

#ifndef REPORTJSONWRITER_H
#define REPORTJSONWRITER_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MotecSetupAnalyzer.h"   // LapStats, StatSummary, VelocityHistogram, pickDamperHistogram()
#include "LapReportGenerator.h"    // ReportResult, SectionData, ReportSection

namespace reportjson {

// Bumped whenever the payload schema changes (plan §3).
static const int kSchemaVersion = 1;

// BCC on Windows encodes narrow string literals as CP1252. JSON requires UTF-8, so we
// convert any byte >= 0x80 to its proper multi-byte UTF-8 sequence before escaping.
inline std::string cp1252ToUtf8(const std::string& s) {
    // Non-trivial mappings for bytes 0x80..0x9F (the rest, 0xA0..0xFF, equal Latin-1).
    static const unsigned short kExtra[32] = {
        0x20AC, 0x0081, 0x201A, 0x0191, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC,
        0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    std::string out;
    out.reserve(s.size() * 2); // worst case: every high byte -> 2-3 UTF-8 bytes
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out.push_back(static_cast<char>(c)); continue; }
        // Determine Unicode codepoint.
        uint32_t cp = (c >= 0xA0) ? static_cast<uint32_t>(c) : kExtra[c - 0x80];
        if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline std::string jsonEscape(const std::string& s) {
    const std::string utf8 = cp1252ToUtf8(s); // ensure valid UTF-8 before escaping
    std::string out;
    out.reserve(utf8.size() + 8);
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Printable ASCII and every UTF-8 continuation/lead byte pass through.
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

inline std::string jsonNumber(double v, int precision) {
    if (!std::isfinite(v))
        return "0"; // NaN/inf are not valid JSON — emit a neutral 0.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
    // Trim trailing zeros and any dangling decimal point ("12.30" -> "12.3").
    size_t len = std::strlen(buf);
    while (len > 0 && buf[len - 1] == '0') --len;
    if (len > 0 && buf[len - 1] == '.') --len;
    return std::string(buf, len ? len : 1); // keep at least one digit ("0")
}

// Emits "key":[...] — a fixed-length double array built from StatSummary cells.
// mode: 0 = average(), 1 = min, 2 = max. A cell with count==0 emits null (keeps the
// index stable for the tablet renderer). The whole key is omitted when no cell has data.
inline void appendCellArray(std::string& out, const char* key,
                            std::vector<const StatSummary*> cells, int mode) {
    bool any = false;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (cells[i] && cells[i]->count > 0) { any = true; break; }
    if (!any) return;

    out += "\"";
    out += key;
    out += "\":[";
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const StatSummary* c = cells[i];
        double v = 0.0;
        if (!c || c->count == 0) {
            // null keeps the slot so indices line up with FL/FR/RL/RR x inner/mid/outer.
        } else if (mode == 1) {
            v = c->min;
        } else if (mode == 2) {
            v = c->max;
        } else {
            v = c->average();
        }
        if (i > 0) out += ",";
        if (!c || c->count == 0) {
            out += "null";
        } else {
            out += jsonNumber(v, 1);
        }
    }
    out += "]";
}

// All-or-nothing variant: the key is emitted only when EVERY cell has data.
inline void appendAllCellsArray(std::string& out, const char* key,
                                std::vector<const StatSummary*> cells, int mode) {
    if (cells.empty()) return;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (!cells[i] || cells[i]->count == 0) return;

    out += "\"";
    out += key;
    out += "\":[";
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const StatSummary* c = cells[i];
        double v = mode == 1 ? c->min : (mode == 2 ? c->max : c->average());
        if (i > 0) out += ",";
        out += jsonNumber(v, 1);
    }
    out += "]";
}

// Appends "cornerKey":{"bins":[41 ints],"mean":x,"stdDev":y}. No-op when the histogram is empty.
inline void appendDamperHistogram(std::string& out, const char* cornerKey,
                                  const VelocityHistogram& h) {
    if (h.totalCount == 0) return;

    out += "\"";
    out += cornerKey;
    out += "\":{\"bins\":[";
    for (int b = 0; b < VelocityHistogram::kBinCount; ++b) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(h.bins[b]));
        if (b > 0) out += ",";
        out += buf;
    }
    out += "],\"mean\":" + jsonNumber(h.mean(), 1);
    out += ",\"stdDev\":" + jsonNumber(h.stdDev(), 1);
    out += ",\"rebLo\":" + jsonNumber(h.percentReboundLow(g_thresholds.damperSlowSpeedRange), 1);
    out += ",\"rebHi\":" + jsonNumber(h.percentReboundHigh(g_thresholds.damperSlowSpeedRange), 1);
    out += ",\"rebAvg\":" + jsonNumber(h.percentReboundAvg(g_thresholds.damperSlowSpeedRange), 1);
    out += ",\"bmpLo\":" + jsonNumber(h.percentBumpLow(g_thresholds.damperSlowSpeedRange), 1);
    out += ",\"bmpHi\":" + jsonNumber(h.percentBumpHigh(g_thresholds.damperSlowSpeedRange), 1);
    out += ",\"bmpAvg\":" + jsonNumber(h.percentBumpAvg(g_thresholds.damperSlowSpeedRange), 1) + "}";
}

// Builds the full tablet payload (plan §3). `lap` is the averaged lap used for the report;
// `lapsIncluded` is the number of laps that went into it. Sub-objects are omitted when their
// source data has count==0, so a sparse session yields a smaller but always-valid document.
// `versionOverride`: pass an incremented counter (Phase B5) so polling tablets detect change;
// -1 keeps kSchemaVersion for standalone harness use.
inline std::string buildReportJson(const ReportResult& report, const LapStats& lap, int lapsIncluded = 0, int versionOverride = -1) {
    // meta.hasData: at least one section carries content AND we have real numbers to show.
    bool anySectionContent = false;
    for (int i = 0; i < 7 && !anySectionContent; ++i) {
        auto it = report.find(static_cast<ReportSection>(i));
        if (it != report.end() && !it->second.content.empty())
            anySectionContent = true;
    }

    // Tyre cells in the same order as the desktop paint box: FL inner/mid/outer, FR..., RL..., RR...
    static const StatSummary* tyreCells[12] = {
        &lap.tyreInnerTempLF,  &lap.tyreCentreTempLF, &lap.tyreOuterTempLF,
        &lap.tyreInnerTempRF,  &lap.tyreCentreTempRF, &lap.tyreOuterTempRF,
        &lap.tyreInnerTempLR,  &lap.tyreCentreTempLR, &lap.tyreOuterTempLR,
        &lap.tyreInnerTempRR,  &lap.tyreCentreTempRR, &lap.tyreOuterTempRR
    };

    static const StatSummary* brakeCorners[4] = {
        &lap.brakeTempLF, &lap.brakeTempRF, &lap.brakeTempLR, &lap.brakeTempRR
    };

    static const StatSummary* pressureCells[4] = {
        &lap.pressureLF, &lap.pressureRF, &lap.pressureLR, &lap.pressureRR
    };

    bool anyTyre = false;
    for (int i = 0; i < 12; ++i) if (tyreCells[i]->count > 0) { anyTyre = true; break; }
    bool allBrakes = brakeCorners[0] && brakeCorners[1] && brakeCorners[2] && brakeCorners[3];
    for (int i = 0; i < 4 && allBrakes; ++i) if (brakeCorners[i]->count == 0) allBrakes = false;

    bool chartsPresent = anyTyre || allBrakes;
    if (!chartsPresent) {
        for (int c = 0; c < 4; ++c)
            if (pickDamperHistogram(lap, c).totalCount > 0) { chartsPresent = true; break; }
    }

    const bool hasData = anySectionContent && ((lapsIncluded > 0) || chartsPresent);

    std::string out;
    char numBuf[32];
    int ver = (versionOverride >= 0) ? versionOverride : kSchemaVersion;   // Phase B5: form passes its own counter
    std::snprintf(numBuf, sizeof(numBuf), "%d", ver);
    out += "{\"version\":" + std::string(numBuf);

    // meta — carName/trackName come from the averaged lap (empty string when unknown).
    out += ",\"meta\":{";
    out += "\"carName\":\""  + jsonEscape(lap.vehicleName) + "\",";
    out += "\"trackName\":\"" + jsonEscape(lap.trackName) + "\",";
    std::snprintf(numBuf, sizeof(numBuf), "%d", lapsIncluded > 0 ? lapsIncluded : 0);
    out += "\"lapCountIncluded\":" + std::string(numBuf) + ",";
    out += std::string("\"hasData\":") + (hasData ? "true" : "false"); // hasData last — no trailing comma
    out += "}";

    // sections — ALL seven keys always present, in enum order. Missing entry -> empty content.
    static const char* sectionKeys[7] = {
        "Summary", "General", "Tyre", "Brake", "Suspension", "EngineCooling", "Handling"
    };
    out += ",\"sections\":{";
    for (int i = 0; i < 7; ++i) {
        auto it = report.find(static_cast<ReportSection>(i));
        if (it != report.end()) {
            const SectionData& sd = it->second;
            out += "\"";
            out += sectionKeys[i];
            out += "\":{\"content\":\"" + jsonEscape(sd.content) + "\",\"hasRecommendation\":";
            out += std::string(sd.hasRecommendation ? "true" : "false") + "}"; // close section object
        } else {
            out += "\"";
            out += sectionKeys[i];
            out += "\":{\"content\":\"\",\"hasRecommendation\":false}"; // close section object
        }
        if (i < 6) out += ",";
    }
    out += "}";

    // lapStats — numeric arrays + damper histograms. Empty sub-objects are omitted entirely,
    // so the inner object may legitimately be empty ("lapStats":{}).
    std::string stats;
    auto appendStat = [&stats](const std::string& entry) {
        if (entry.empty()) return;
        if (!stats.empty()) stats += ",";
        stats += entry;
    };

    // Tyre temps: 12-slot arrays, null for missing cells. One vector of pointers reused per mode.
    std::vector<const StatSummary*> tyreAvg(tyreCells, tyreCells + 12);
    {
        std::string e; appendCellArray(e, "tyreTempsAvg", tyreAvg, 0); appendStat(std::move(e));
        e.clear();     appendCellArray(e, "tyreTempsMin", tyreAvg, 1); appendStat(std::move(e));
        e.clear();     appendCellArray(e, "tyreTempsMax", tyreAvg, 2); appendStat(std::move(e));
    }

    // Brake temps: all-or-nothing (4 corners LF/RF/LR/RR).
    std::vector<const StatSummary*> brake(brakeCorners, brakeCorners + 4);
    {
        std::string e; appendAllCellsArray(e, "brakeTempAvg", brake, 0); appendStat(std::move(e));
        e.clear();     appendAllCellsArray(e, "brakeTempMax", brake, 2); appendStat(std::move(e));
    }

    // Water & Oil temps: single values (avg and max).
    if (lap.waterTemp.count > 0) {
        std::string e = "\"waterTempAvg\":" + jsonNumber(lap.waterTemp.average(), 1) + 
                        ",\"waterTempMax\":" + jsonNumber(lap.waterTemp.max, 1);
        appendStat(e);
    }
    if (lap.oilTemp.count > 0) {
        std::string e = "\"oilTempAvg\":" + jsonNumber(lap.oilTemp.average(), 1) + 
                        ",\"oilTempMax\":" + jsonNumber(lap.oilTemp.max, 1);
        appendStat(e);
    }

    // Tyre pressures: LF/RF/LR/RR.
    std::vector<const StatSummary*> press(pressureCells, pressureCells + 4);
    {
        std::string e; appendCellArray(e, "tyrePressures", press, 0); appendStat(std::move(e));
    }

    // Damper histograms: one entry per corner that has samples (direct suspVel channel or travel-derived).
    static const char* damperKeys[4] = { "LF", "RF", "LR", "RR" };
    std::string dampers;
    for (int c = 0; c < 4; ++c) {
        const VelocityHistogram& h = pickDamperHistogram(lap, c);
        if (h.totalCount == 0) continue;
        std::string entry;
        appendDamperHistogram(entry, damperKeys[c], h);
        if (!dampers.empty()) dampers += ",";
        dampers += entry;
    }
    if (!dampers.empty()) {
        stats += (stats.empty() ? "" : ",");
        stats += "\"damperHistograms\":{" + dampers + "}";
    }

    out += ",\"lapStats\":{" + stats + "}}\n"; // trailing newline: friendly for log inspection
    return out;
}

} // namespace reportjson

#endif // REPORTJSONWRITER_H
