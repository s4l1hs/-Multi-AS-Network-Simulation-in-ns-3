#pragma once

#include "scenario_config.h"

#include "ns3/node-container.h"

namespace multias {

// BGP-like inter-AS routing helper (placeholder).
class BgpLikeRouter {
public:
    explicit BgpLikeRouter(const ScenarioConfig& cfg);

    // Install inter-AS policy routing on border routers.
    void Install(const ns3::NodeContainer& borderRouters);

private:
    ScenarioConfig m_cfg;
};

} // namespace multias
