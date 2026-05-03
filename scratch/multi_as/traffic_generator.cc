#include "traffic_generator.h"

#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("TrafficGenerator");

namespace multias {

TrafficGenerator::TrafficGenerator(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

ns3::ApplicationContainer TrafficGenerator::Install(const ns3::NodeContainer& nodes)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("TrafficGenerator placeholder: no apps installed yet ("
                << nodes.GetN() << " nodes available)");
    return ns3::ApplicationContainer{};
}

} // namespace multias
