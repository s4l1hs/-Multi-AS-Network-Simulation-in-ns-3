#include "metrics_collector.h"

#include "ns3/log.h"

#include <filesystem>
#include <string>

NS_LOG_COMPONENT_DEFINE("MetricsCollector");

namespace multias {

MetricsCollector::MetricsCollector(const ScenarioConfig& cfg)
    : m_cfg(cfg), m_monitor(nullptr)
{
}

void MetricsCollector::Attach(const ns3::NodeContainer& nodes)
{
    NS_LOG_FUNCTION(this);
    m_monitor = m_fmHelper.InstallAll();
    NS_LOG_INFO("FlowMonitor attached to " << nodes.GetN() << " nodes");
}

void MetricsCollector::Dump() const
{
    NS_LOG_FUNCTION(this);
    if (!m_monitor) {
        NS_LOG_WARN("Dump() called but FlowMonitor was never attached");
        return;
    }
    std::filesystem::create_directories(m_cfg.outDir);
    std::string filename = m_cfg.outDir + "/" + m_cfg.scenarioId
                         + "_run" + std::to_string(m_cfg.runId) + "_flows.xml";
    m_monitor->SerializeToXmlFile(filename, /*enableHistograms=*/true, /*enableProbes=*/true);
    NS_LOG_INFO("Flow stats written to " << filename);
}

} // namespace multias
