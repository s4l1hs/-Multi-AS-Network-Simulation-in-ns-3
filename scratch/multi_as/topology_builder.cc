#include "topology_builder.h"

#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("TopologyBuilder");

namespace multias {

TopologyBuilder::TopologyBuilder(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

ns3::NodeContainer TopologyBuilder::Build()
{
    NS_LOG_FUNCTION(this);
    ns3::NodeContainer nodes;
    nodes.Create(m_cfg.nodeCount);
    NS_LOG_INFO("Created " << m_cfg.nodeCount << " nodes (mode=" << m_cfg.distributionMode << ")");
    return nodes;
}

} // namespace multias
