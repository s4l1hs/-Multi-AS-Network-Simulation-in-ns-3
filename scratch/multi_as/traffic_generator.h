#pragma once

#include "scenario_config.h"

#include "ns3/node-container.h"
#include "ns3/application-container.h"

namespace multias {

class TrafficGenerator {
public:
    explicit TrafficGenerator(const ScenarioConfig& cfg);

    // Install OnOff/PacketSink pairs on the provided nodes.
    // Returns all installed applications.
    ns3::ApplicationContainer Install(const ns3::NodeContainer& nodes);

private:
    ScenarioConfig m_cfg;
};

} // namespace multias
