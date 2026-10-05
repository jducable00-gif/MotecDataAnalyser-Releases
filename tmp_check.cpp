#include "MotecSetupAnalyzer.h"
#include <iostream>
int main() {
    DataProvider p;
    p.setValue(AnalysisAlias::TyreTempFLInner, 82.0);
    p.setValue(AnalysisAlias::TyreTempFLMiddle, 72.0);
    p.setValue(AnalysisAlias::TyreTempFLOuter, 60.0);
    p.setValue(AnalysisAlias::TyreTempFRInner, 64.0);
    p.setValue(AnalysisAlias::TyreTempFRMiddle, 66.0);
    p.setValue(AnalysisAlias::TyreTempFROuter, 62.0);
    p.setValue(AnalysisAlias::TyreTempRLInner, 72.0);
    p.setValue(AnalysisAlias::TyreTempRLMiddle, 70.0);
    p.setValue(AnalysisAlias::TyreTempRLOuter, 68.0);
    p.setValue(AnalysisAlias::TyreTempRRInner, 70.0);
    p.setValue(AnalysisAlias::TyreTempRRMiddle, 68.0);
    p.setValue(AnalysisAlias::TyreTempRROuter, 66.0);
    auto reg = buildDefaultAnalysisRegistry();
    auto res = reg.evaluateAll(p, buildDefaultAnalysisSettings());
    std::cout << "results=" << res.size() << "\n";
    for (auto const& r : res) {
        std::cout << r.title << " | " << r.description << "\n";
    }
    return 0;
}
