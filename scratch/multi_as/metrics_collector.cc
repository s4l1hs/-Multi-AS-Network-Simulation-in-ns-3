#include "metrics_collector.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <filesystem>
#include <fstream>
#include <limits>

NS_LOG_COMPONENT_DEFINE("MetricsCollector");

namespace multias {

MetricsCollector::MetricsCollector(const ScenarioConfig& cfg)
    : m_cfg(cfg), m_monitor(nullptr)
{
}

void MetricsCollector::Setup(const ns3::NodeContainer& nodes)
{
    Attach(nodes);
}

void MetricsCollector::Attach(const ns3::NodeContainer& nodes)
{
    NS_LOG_FUNCTION(this);
    m_monitor = m_fmHelper.InstallAll();
    NS_LOG_INFO("FlowMonitor attached to " << nodes.GetN() << " nodes");
}

void MetricsCollector::RegisterFlow(uint16_t port, uint32_t srcAs, uint32_t dstAs)
{
    m_flowMeta[port] = {srcAs, dstAs};
}

void MetricsCollector::MarkFailureEvent(uint32_t linkId, ns3::Time t)
{
    m_failureLinkId   = linkId;
    m_failureTimeSec  = t.GetSeconds();
    m_failureEvents.emplace_back(linkId, t);
    NS_LOG_UNCOND("[METRICS] Link " << linkId << " FAILED at t="
                  << t.GetSeconds() << "s");
}

void MetricsCollector::MarkConvergenceEvent(uint32_t linkId, ns3::Time t)
{
    m_convergenceTimeSec = t.GetSeconds();
    NS_LOG_UNCOND("[METRICS] BGP convergence (link " << linkId << ") at t="
                  << t.GetSeconds() << "s  delay="
                  << (t.GetSeconds() - m_failureTimeSec) << "s");
}

void MetricsCollector::MarkRecoveryEvent(uint32_t linkId, ns3::Time t)
{
    m_recoveryEvents.emplace_back(linkId, t);
    NS_LOG_UNCOND("[METRICS] Link " << linkId << " RECOVERED at t="
                  << t.GetSeconds() << "s");
}

const std::vector<std::pair<uint32_t, ns3::Time>>&
MetricsCollector::GetFailureEvents() const
{
    return m_failureEvents;
}

void MetricsCollector::DumpCsv(const std::string& path,
                                const ScenarioConfig& cfg)
{
    NS_LOG_FUNCTION(this);
    if (!m_monitor) {
        NS_LOG_WARN("DumpCsv: FlowMonitor not attached — skipping");
        return;
    }

    m_monitor->CheckForLostPackets();

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    std::ofstream csv(path);
    if (!csv.is_open()) {
        NS_LOG_WARN("DumpCsv: cannot open " << path);
        return;
    }

    csv << "scenarioId,runId,nodeCount,distribution,"
           "flowId,srcAs,dstAs,"
           "meanDelayMs,throughputMbps,packetLossPct,"
           "convergenceSec,simTimeSec\n";

    ns3::Ptr<ns3::Ipv4FlowClassifier> clf =
        ns3::DynamicCast<ns3::Ipv4FlowClassifier>(m_fmHelper.GetClassifier());

    // Convergence duration (same for every flow in the scenario).
    double convergenceSec = -1.0;
    if (m_failureTimeSec >= 0.0 && m_convergenceTimeSec > m_failureTimeSec) {
        convergenceSec = m_convergenceTimeSec - m_failureTimeSec;
    }

    // Accumulators for summary row.
    uint32_t numFlows        = 0;
    double   sumDelayMs      = 0.0;
    double   sumThroughput   = 0.0;
    double   sumLoss         = 0.0;

    for (const auto& [fid, stat] : m_monitor->GetFlowStats()) {
        ns3::Ipv4FlowClassifier::FiveTuple ft = clf->FindFlow(fid);

        double meanDelayMs = 0.0;
        if (stat.rxPackets > 0) {
            meanDelayMs = stat.delaySum.GetMicroSeconds()
                          / 1000.0 / static_cast<double>(stat.rxPackets);
        }

        // Throughput over the active window of this flow.
        double throughputMbps = 0.0;
        if (stat.rxPackets > 0) {
            double durationSec =
                (stat.timeLastRxPacket - stat.timeFirstTxPacket).GetSeconds();
            if (durationSec > 0.0) {
                throughputMbps = stat.rxBytes * 8.0 / durationSec / 1e6;
            }
        }

        double lossPct = 0.0;
        if (stat.txPackets > 0) {
            lossPct = (1.0 - static_cast<double>(stat.rxPackets)
                              / static_cast<double>(stat.txPackets)) * 100.0;
        }

        uint32_t srcAs = 0;
        uint32_t dstAs = 0;
        auto it = m_flowMeta.find(ft.destinationPort);
        if (it != m_flowMeta.end()) {
            srcAs = it->second.srcAs;
            dstAs = it->second.dstAs;
        }

        csv << cfg.scenarioId  << "," << cfg.runId     << ","
            << cfg.nodeCount   << "," << cfg.distributionMode << ","
            << fid             << ","
            << srcAs           << "," << dstAs         << ","
            << meanDelayMs     << "," << throughputMbps << ","
            << lossPct         << "," << convergenceSec << ","
            << cfg.simTime     << "\n";

        sumDelayMs    += meanDelayMs;
        sumThroughput += throughputMbps;
        sumLoss       += lossPct;
        ++numFlows;
    }

    NS_LOG_UNCOND("[Metrics] CSV written (" << numFlows << " flows): " << path);

    std::filesystem::path p(path);
    std::string summaryPath =
        (p.parent_path() / (p.stem().string() + "_summary.csv")).string();

    std::ofstream sumCsv(summaryPath);
    if (sumCsv.is_open()) {
        sumCsv << "scenarioId,runId,nodeCount,distribution,"
                  "numFlows,meanDelayMs,meanThroughputMbps,"
                  "meanPacketLossPct,convergenceSec,simTimeSec\n";
        double n = (numFlows > 0) ? static_cast<double>(numFlows) : 1.0;
        sumCsv << cfg.scenarioId       << "," << cfg.runId              << ","
               << cfg.nodeCount        << "," << cfg.distributionMode   << ","
               << numFlows             << ","
               << (sumDelayMs    / n)  << ","
               << (sumThroughput / n)  << ","
               << (sumLoss       / n)  << ","
               << convergenceSec       << ","
               << cfg.simTime          << "\n";
        NS_LOG_UNCOND("[Metrics] Summary CSV written: " << summaryPath);
    }
}

void MetricsCollector::Dump()
{
    NS_LOG_FUNCTION(this);
    if (!m_monitor) {
        NS_LOG_WARN("Dump() called but FlowMonitor was never attached");
        return;
    }
    std::filesystem::create_directories(m_cfg.outDir);
    std::string filename = m_cfg.outDir + "/" + m_cfg.scenarioId
                         + "_run" + std::to_string(m_cfg.runId) + "_flows.xml";
    m_monitor->SerializeToXmlFile(filename, /*histograms=*/true, /*probes=*/true);
    NS_LOG_INFO("Flow stats XML written to " << filename);
}

} // namespace multias
