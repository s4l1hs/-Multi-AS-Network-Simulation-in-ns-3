#include "inter_as_router.h"

#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("BgpLikeRouter");

namespace multias {

BgpLikeRouter::BgpLikeRouter(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

void BgpLikeRouter::Install(const ns3::NodeContainer& borderRouters)
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("Inter-AS policy routing placeholder installed on "
                << borderRouters.GetN() << " border routers");
}

} // namespace multias
