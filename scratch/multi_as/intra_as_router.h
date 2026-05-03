#pragma once

#include "scenario_config.h"

#include "ns3/node-container.h"

namespace multias {

// OSPF-like intra-AS routing helper (placeholder).
class OspfLikeRouter {
public:
    explicit OspfLikeRouter(const ScenarioConfig& cfg);

    // Install routing on nodes within a single AS.
    void Install(const ns3::NodeContainer& nodes);

private:
    ScenarioConfig m_cfg;
};

} // namespace multias
