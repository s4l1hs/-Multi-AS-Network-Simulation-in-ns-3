#pragma once

#include "scenario_config.h"

#include "ns3/node-container.h"
#include "ns3/net-device-container.h"

namespace multias {

class TopologyBuilder {
public:
    explicit TopologyBuilder(const ScenarioConfig& cfg);

    // Build nodes and links according to cfg.distributionMode.
    // Returns the flat container of all nodes created.
    ns3::NodeContainer Build();

private:
    ScenarioConfig m_cfg;
};

} // namespace multias
