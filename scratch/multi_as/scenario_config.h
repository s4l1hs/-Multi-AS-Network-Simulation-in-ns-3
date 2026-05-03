#pragma once

#include <cstdint>
#include <string>

namespace multias {

struct ScenarioConfig {
    int         nodeCount       = 20;
    std::string distributionMode = "balanced";
    std::string scenarioId;
    int         runId           = 1;
    double      simTime         = 60.0;
    std::string outDir          = "results";
    uint32_t    seed            = 1;
};

} // namespace multias
