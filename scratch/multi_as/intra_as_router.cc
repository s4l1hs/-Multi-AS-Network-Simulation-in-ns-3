#include "intra_as_router.h"

#include "ns3/log.h"
#include "ns3/ipv4-global-routing-helper.h"

NS_LOG_COMPONENT_DEFINE("OspfLikeRouter");

namespace multias {

OspfLikeRouter::OspfLikeRouter(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

void OspfLikeRouter::Install(const ns3::NodeContainer& nodes)
{
    NS_LOG_FUNCTION(this);
    // Placeholder: global routing approximates OSPF convergence for now.
    ns3::Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    NS_LOG_INFO("Intra-AS routing installed on " << nodes.GetN() << " nodes");
}

} // namespace multias
