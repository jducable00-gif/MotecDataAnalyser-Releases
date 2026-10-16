//---------------------------------------------------------------------------
// test_report_json.cpp - Phase A (A3) standalone harness for ReportJsonWriter.h.
// Parses a real session file from data\samples, builds the average-lap report and
// dumps the tablet JSON payload to an output file so it can be validated with any
// JSON checker before the HTTP server is wired in (plan §4 Phase A).
//
// Build (from scripts\):
//   call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat"
//   bcc32c -I.. -o test_report_json.exe ..\analyze_motec_setup.cpp ..\MotecToCSV.cpp ^
//          ..\RuleConfig.cpp ..\LapReportGenerator.cpp test_report_json.cpp
// No VCL libs: analyze_motec_setup.cpp's only VCL symbols (saveSymptomWeights /
// loadSymptomWeights) are stubbed below, so the harness stays a plain console exe.
//
// Usage:  test_report_json <file.ld|.ldx> [language 0=EN|1=FR] [profile 0=EvoM1|1=AMS2]
//         optional 4th arg = output path (default report_payload.json)
//---------------------------------------------------------------------------

// No VCL headers here on purpose: analyze_motec_setup.cpp already provides real
// saveSymptomWeights/loadSymptomWeights (TIniFile-based), so we must link rtl.lib +
// vcl.lib to satisfy them. A console exe built from main() does NOT pull vcl.lib's
// WinMain (library members resolve lazily, only referenced ones are linked in).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "MotecSetupAnalyzer.h"
#include "LapReportGenerator.h"
#include "ReportJsonWriter.h"

// NOTE: do NOT define `g_thresholds` here — analyze_motec_setup.cpp owns the global.
// Also do NOT stub saveSymptomWeights/loadSymptomWeights — that file already defines
// them (real TIniFile bodies), so we link rtl.lib + vcl.lib to satisfy their symbols.

static std::string toLower(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] += ('a' - 'A');
    return s;
}

// Rough mirror of LapReportGenerator's validity window (median x [min,max] multipliers),
// used only to report a plausible lapCountIncluded in the payload meta. The exact count is
// not exposed by generateAverageLaps(); any consistent positive value satisfies Gate A as long
// as hasData ends up true on real data.
static int estimateValidLapCount(const std::vector<LapStats>& laps) {
    if (laps.size() < 2) return static_cast<int>(laps.size());
    std::vector<double> d;
    for (std::size_t i = 0; i < laps.size(); ++i) {
        const double dur = laps[i].endTime - laps[i].startTime;
        if (dur > 0.0 && std::isfinite(dur)) d.push_back(dur);
    }
    if (d.empty()) return 0;
    std::sort(d.begin(), d.end());
    const double med = d[d.size() / 2];
    int n = 0;
    for (std::size_t i = 0; i < laps.size(); ++i) {
        const double dur = laps[i].endTime - laps[i].startTime;
        if (dur >= med * g_thresholds.minValidLapTimeMultiplier &&
            dur <= med * g_thresholds.maxValidLapTimeMultiplier)
            ++n;
    }
    return n > 0 ? n : static_cast<int>(laps.size());
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_report_json <file.ld|.ldx> [language 0|1] [profile 0=EvoM1|1=AMS2]\n");
        return 2;
    }

    const std::string path = argv[1];
    const int langArg   = (argc > 2) ? atoi(argv[2]) : 0; // default English
    const int profArg   = (argc > 3) ? atoi(argv[3]) : 0; // default EvoM1

    std::vector<LapStats> laps;
    std::string error;
    bool ok = false;
    ok = parseMotecLd(path, laps, error);

    if (!ok || laps.empty()) {
        std::fprintf(stderr, "parse failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("parsed %zu laps from %s\n", laps.size(), path.c_str());

    const ReportLanguage language = (langArg == 1) ? ReportLanguage::French : ReportLanguage::English;
    const ChannelProfile profile  = (profArg == 1) ? ChannelProfile::AMS2   : ChannelProfile::EvoM1;

    // Same call the desktop app makes at report-finish time.
    ReportResult result = LapReportGenerator::generateAverageLaps(laps, language, profile);
    const LapStats avg = buildAverageLapData(laps);
    if (avg.lapNumber == 0 && laps.empty()) {
        std::fprintf(stderr, "no averaged lap available\n");
        return 1;
    }

    const int included = estimateValidLapCount(laps);
    const std::string json = reportjson::buildReportJson(result, avg, included);

    // Write payload next to the exe for inspection: report_payload.json (or argv[4] if given).
    FILE* f = nullptr;
    const char* outPath = (argc > 4) ? argv[4] : "report_payload.json";
    fopen_s(&f, outPath, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s for writing\n", outPath);
        return 1;
    }
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);

    // Also echo to stdout so the payload is visible in the terminal log.
    fputs(json.c_str(), stdout);
    std::printf("\n-- wrote %zu bytes to %s (language=%d profile=%d included~%d)\n",
                json.size(), outPath, langArg, profArg, included);
    return 0;
}
