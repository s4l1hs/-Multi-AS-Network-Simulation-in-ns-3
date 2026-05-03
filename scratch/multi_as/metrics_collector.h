#pragma once

#include "scenario_config.h"

#include "ns3/flow-monitor-helper.h"
#include "ns3/node-container.h"

#include <string>

namespace multias {

class MetricsCollector {
public:
    explicit MetricsCollector(const ScenarioConfig& cfg);

    // Attach FlowMonitor to all nodes.
    void Attach(const ns3::NodeContainer& nodes);

    // Write per-flow stats to outDir/scenarioId_runId_flows.xml.
    void Dump() const;

private:
    ScenarioConfig            m_cfg;
    ns3::FlowMonitorHelper    m_fmHelper;
    ns3::Ptr<ns3::FlowMonitor> m_monitor;
};

} // namespace multias
