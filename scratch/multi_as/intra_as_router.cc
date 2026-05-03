#include "intra_as_router.h"

#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("OspfLikeRouter");

namespace multias {

OspfLikeRouter::OspfLikeRouter(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

void OspfLikeRouter::Install(const ns3::NodeContainer& nodes)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("Populating global routing tables (" << nodes.GetN() << " nodes)");
    ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables();
}

void OspfLikeRouter::RecomputeAfterTopologyChange()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("Recomputing global routing tables after topology change");
    ns3::Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
}

} // namespace multias
