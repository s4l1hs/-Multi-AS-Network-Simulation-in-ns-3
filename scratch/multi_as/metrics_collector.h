#pragma once

#include "scenario_config.h"

#include "ns3/flow-monitor-helper.h"
#include "ns3/nstime.h"
#include "ns3/node-container.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace multias {

class MetricsCollector {
public:
    explicit MetricsCollector(const ScenarioConfig& cfg);

    // Install FlowMonitor on all nodes.
    void Setup(const ns3::NodeContainer& nodes);
    void Attach(const ns3::NodeContainer& nodes); // alias kept for compat

    // Register the AS source/destination for a flow identified by its dst port.
    // Call after TrafficGenerator::InstallFlows(), before Simulator::Run().
    void RegisterFlow(uint16_t port, uint32_t srcAs, uint32_t dstAs);

    // Called by BgpLikeRouter when the link physically fails.
    void MarkFailureEvent(uint32_t linkId, ns3::Time t);

    // Called by BgpLikeRouter when BGP tables have re-converged and the
    // alternative path is active. Convergence time = t - failureTime.
    void MarkConvergenceEvent(uint32_t linkId, ns3::Time t);

    // Called by BgpLikeRouter when the link recovers.
    void MarkRecoveryEvent(uint32_t linkId, ns3::Time t);

    // Write per-flow stats as CSV to `path`.
    // Also writes a single-row summary to <stem>_summary.csv.
    // CSV columns:
    //   scenarioId,runId,nodeCount,distribution,flowId,srcAs,dstAs,
    //   meanDelayMs,throughputMbps,packetLossPct,convergenceSec,simTimeSec
    void DumpCsv(const std::string& path, const ScenarioConfig& cfg);

    // Write ns-3 FlowMonitor XML (histograms + probes enabled).
    void Dump();

    // Returns all recorded failure events (linkId, timestamp).
    const std::vector<std::pair<uint32_t, ns3::Time>>& GetFailureEvents() const;

private:
    struct FlowMeta { uint32_t srcAs; uint32_t dstAs; };

    ScenarioConfig m_cfg;
    ns3::FlowMonitorHelper             m_fmHelper;
    ns3::Ptr<ns3::FlowMonitor>         m_monitor;

    // dst port → AS membership (populated by RegisterFlow)
    std::unordered_map<uint16_t, FlowMeta> m_flowMeta;

    double   m_failureTimeSec    {-1.0}; // set by MarkFailureEvent
    double   m_convergenceTimeSec{-1.0}; // set by MarkConvergenceEvent
    uint32_t m_failureLinkId     {0};

    std::vector<std::pair<uint32_t, ns3::Time>> m_failureEvents;
    std::vector<std::pair<uint32_t, ns3::Time>> m_recoveryEvents;
};

} // namespace multias
