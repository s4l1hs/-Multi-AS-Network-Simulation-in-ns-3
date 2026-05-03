#pragma once

#include <cstdint>
#include <string>

namespace multias {

struct ScenarioConfig {
    // Topology
    int         nodeCount        = 20;
    std::string distributionMode = "balanced";
    std::string scenarioId;
    int         runId            = 1;
    double      simTime          = 60.0;
    std::string outDir           = "results";
    uint32_t    seed             = 1;

    // Failure scenario
    double      failureTime      = 20.0;
    std::string failureLink      = "AS1_AS3_PRIMARY";
    bool        noFailure        = false;
};

} // namespace multias
