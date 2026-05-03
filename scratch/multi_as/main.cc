#include "scenario_config.h"
#include "topology_builder.h"
#include "intra_as_router.h"
#include "inter_as_router.h"
#include "traffic_generator.h"
#include "metrics_collector.h"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/netanim-module.h"

#include <filesystem>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiAsSim");

int main(int argc, char* argv[])
{
    multias::ScenarioConfig cfg;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nodes",       "Total number of nodes (20|50|100)",        cfg.nodeCount);
    cmd.AddValue("dist",        "Distribution mode: balanced|unbalanced",   cfg.distributionMode);
    cmd.AddValue("scenarioId",  "Scenario identifier string",               cfg.scenarioId);
    cmd.AddValue("runId",       "Run index (for repeated trials)",          cfg.runId);
    cmd.AddValue("simTime",     "Simulation duration in seconds",           cfg.simTime);
    cmd.AddValue("outDir",      "Output directory for results",             cfg.outDir);
    cmd.AddValue("seed",        "RNG seed",                                 cfg.seed);
    cmd.AddValue("failureTime", "Link failure injection time (seconds)",    cfg.failureTime);
    cmd.AddValue("failureLink", "Link to fail (e.g. AS1_AS3_PRIMARY)",      cfg.failureLink);
    cmd.AddValue("noFailure",   "Disable failure injection (baseline run)", cfg.noFailure);
    cmd.Parse(argc, argv);

    std::filesystem::create_directories(cfg.outDir);

    NS_LOG_UNCOND("=== MultiAsSim configuration ===");
    NS_LOG_UNCOND("  nodes       = " << cfg.nodeCount);
    NS_LOG_UNCOND("  dist        = " << cfg.distributionMode);
    NS_LOG_UNCOND("  scenarioId  = " << cfg.scenarioId);
    NS_LOG_UNCOND("  runId       = " << cfg.runId);
    NS_LOG_UNCOND("  simTime     = " << cfg.simTime << "s");
    NS_LOG_UNCOND("  outDir      = " << cfg.outDir);
    NS_LOG_UNCOND("  seed        = " << cfg.seed);
    if (cfg.noFailure) {
        NS_LOG_UNCOND("  failure     = DISABLED");
    } else {
        NS_LOG_UNCOND("  failureTime = " << cfg.failureTime << "s");
        NS_LOG_UNCOND("  failureLink = " << cfg.failureLink);
    }
    NS_LOG_UNCOND("================================");

    // 1. build topology: nodes, P2P links, /30 IP addressing, routing stack
    multias::TopologyBuilder topo(cfg);
    topo.Build();

    if (!cfg.scenarioId.empty()) {
        topo.DumpTopologyJson(cfg.outDir + "/" + cfg.scenarioId
                              + "_run" + std::to_string(cfg.runId)
                              + "_topology.json");
    }

    // 2. inter-AS static policy routes on BRs (must precede PopulateRoutingTables)
    multias::BgpLikeRouter bgp(cfg);
    bgp.ConfigureBorderPolicies(topo);

    // 3. populate global routing tables (OSPF-like shortest paths)
    NodeContainer allNodes = topo.AllNodes();
    multias::OspfLikeRouter ospf(cfg);
    ospf.Install(allNodes);

    // 4. install traffic flows (UDP OnOff sources + PacketSink receivers)
    multias::TrafficGenerator tg(cfg);
    tg.InstallFlows(topo, cfg);

    // 5. attach FlowMonitor; register flow-to-AS mapping for CSV annotation
    multias::MetricsCollector mc(cfg);
    mc.Setup(allNodes);
    for (const auto& fi : tg.GetFlows()) {
        mc.RegisterFlow(fi.port, fi.srcAs, fi.dstAs);
    }

    // 6. schedule failure at t=failureTime; BGP reconverges after 2-5s; recovery at t+30
    if (!cfg.noFailure && cfg.failureTime < cfg.simTime) {
        auto linkId = multias::LinkIdFromString(cfg.failureLink);
        bgp.ScheduleLinkFailure(Seconds(cfg.failureTime), linkId, mc);

        double recoveryAt = cfg.failureTime + 30.0;
        if (recoveryAt < cfg.simTime) {
            bgp.ScheduleLinkRecovery(Seconds(recoveryAt), linkId);
        }
    }

    // 7. NetAnim: AS1/2/3 columns at x=15/55/95; BRs red, routers blue
    std::string animId   = cfg.scenarioId.empty() ? "sim" : cfg.scenarioId;
    std::string animFile = cfg.outDir + "/anim_" + animId
                         + "_run" + std::to_string(cfg.runId) + ".xml";
    AnimationInterface anim(animFile);
    anim.EnablePacketMetadata(true);
    anim.SetMaxPktsPerTraceFile(500000);

    constexpr double AS_X[4] = {0.0, 15.0, 55.0, 95.0};
    for (uint32_t asId = 1; asId <= 3; ++asId) {
        const ns3::NodeContainer& nc = topo.GetAsNodes(asId);
        uint32_t n = nc.GetN();
        for (uint32_t j = 0; j < n; ++j) {
            double y = (n <= 1) ? 50.0 : (5.0 + 90.0 * j / (n - 1));
            anim.SetConstantPosition(nc.Get(j), AS_X[asId], y);
        }
    }

    for (uint32_t asId = 1; asId <= 3; ++asId) {
        const ns3::NodeContainer& nc = topo.GetAsNodes(asId);
        const std::string tag = "AS" + std::to_string(asId);

        ns3::Ptr<ns3::Node> bra = topo.GetBorderRouter(asId, 0);
        ns3::Ptr<ns3::Node> brb = topo.GetBorderRouter(asId, 1);
        anim.UpdateNodeColor(bra, 255, 0, 0);
        anim.UpdateNodeDescription(bra, tag + "-BRa");
        anim.UpdateNodeColor(brb, 255, 0, 0);
        anim.UpdateNodeDescription(brb, tag + "-BRb");

        for (uint32_t j = 2; j < nc.GetN(); ++j) {
            anim.UpdateNodeColor(nc.Get(j), 0, 100, 200);
            anim.UpdateNodeDescription(nc.Get(j), tag + "-R" + std::to_string(j));
        }
    }

    static const char* kLinkLabels[5] = {
        "AS1-AS2 primary", "AS1-AS2 backup",
        "AS2-AS3 primary", "AS2-AS3 backup",
        "AS1-AS3 direct"
    };
    const auto& interLinks = topo.GetInterAsLinks();
    for (std::size_t i = 0; i < interLinks.size() && i < 5; ++i) {
        uint32_t n0 = interLinks[i].devices.Get(0)->GetNode()->GetId();
        uint32_t n1 = interLinks[i].devices.Get(1)->GetNode()->GetId();
        anim.UpdateLinkDescription(n0, n1, kLinkLabels[i]);
    }

    Simulator::Stop(Seconds(cfg.simTime));
    Simulator::Run();

    std::string csvBase = cfg.outDir + "/metrics";
    if (!cfg.scenarioId.empty()) csvBase += "_" + cfg.scenarioId
                                          + "_run" + std::to_string(cfg.runId);
    mc.DumpCsv(csvBase + ".csv", cfg);
    mc.Dump();

    Simulator::Destroy();
    return 0;
}
