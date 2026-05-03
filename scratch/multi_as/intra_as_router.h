#pragma once

#include "scenario_config.h"

#include "ns3/node-container.h"

namespace multias {

// Global-routing-based intra-AS helper (OSPF-like convergence approximation).
class OspfLikeRouter {
public:
    explicit OspfLikeRouter(const ScenarioConfig& cfg);

    // Populate routing tables across ALL nodes (call once after full topology setup).
    void Install(const ns3::NodeContainer& nodes);

    // Trigger a full routing-table recomputation after any topology change
    // (link failure, link recovery). Equivalent to BGP/OSPF re-convergence
    // for non-BR nodes.
    void RecomputeAfterTopologyChange();

private:
    ScenarioConfig m_cfg;
};

} // namespace multias
