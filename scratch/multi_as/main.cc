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
    // Topology
    cmd.AddValue("nodes",       "Total number of nodes (20|50|100)",        cfg.nodeCount);
    cmd.AddValue("dist",        "Distribution mode: balanced|unbalanced",   cfg.distributionMode);
    cmd.AddValue("scenarioId",  "Scenario identifier string",               cfg.scenarioId);
    cmd.AddValue("runId",       "Run index (for repeated trials)",          cfg.runId);
    cmd.AddValue("simTime",     "Simulation duration in seconds",           cfg.simTime);
    cmd.AddValue("outDir",      "Output directory for results",             cfg.outDir);
    cmd.AddValue("seed",        "RNG seed",                                 cfg.seed);
    // Failure scenario
    cmd.AddValue("failureTime", "Link failure injection time (seconds)",    cfg.failureTime);
    cmd.AddValue("failureLink", "Link to fail (e.g. AS1_AS3_PRIMARY)",      cfg.failureLink);
    cmd.AddValue("noFailure",   "Disable failure injection (baseline run)", cfg.noFailure);
    cmd.Parse(argc, argv);

    // Ensure output directory exists before any file is written.
    std::filesystem::create_directories(cfg.outDir);

    // ── Configuration summary ─────────────────────────────────────────────
    NS_LOG_UNCOND("=== MultiAsSim configuration ===");
    NS_LOG_UNCOND("  nodes       = " << cfg.nodeCount);
    NS_LOG_UNCOND("  dist        = " << cfg.distributionMode);
    NS_LOG_UNCOND("  scenarioId  = " << cfg.scenarioId);
    NS_LOG_UNCOND("  runId       = " << cfg.runId);
    NS_LOG_UNCOND("  simTime     = " << cfg.simTime << "s");
    NS_LOG_UNCOND("  outDir      = " << cfg.outDir);
    NS_LOG_UNCOND("  seed        = " << cfg.seed);
    if (cfg.noFailure) {
        NS_LOG_UNCOND("  failure     = DISABLED (baseline run)");
    } else {
        NS_LOG_UNCOND("  failureTime = " << cfg.failureTime << "s");
        NS_LOG_UNCOND("  failureLink = " << cfg.failureLink);
    }
    NS_LOG_UNCOND("================================");

    // ─────────────────────────────────────────────────────────────────────
    // Phase 1 — Build topology
    // Creates nodes, P2P links, /30 IP addresses, and installs
    // Ipv4ListRouting(StaticPriority=10, GlobalPriority=5) on all nodes.
    // Sets RNG seed/run for full reproducibility.
    // ─────────────────────────────────────────────────────────────────────
    multias::TopologyBuilder topo(cfg);
    topo.Build();

    if (!cfg.scenarioId.empty()) {
        topo.DumpTopologyJson(cfg.outDir + "/" + cfg.scenarioId
                              + "_run" + std::to_string(cfg.runId)
                              + "_topology.json");
    }

    // ─────────────────────────────────────────────────────────────────────
    // Phase 2 — BGP-like inter-AS border policies
    // Installs /16 static routes on the 6 border routers so they forward
    // inter-AS traffic according to policy (primary metric=30/50,
    // backup/alternative metric=80). Must come before PopulateRoutingTables.
    // ─────────────────────────────────────────────────────────────────────
    multias::BgpLikeRouter bgp(cfg);
    bgp.ConfigureBorderPolicies(topo);

    // ─────────────────────────────────────────────────────────────────────
    // Phase 3 — Global (OSPF-like) routing
    // Computes shortest-path tables for ALL nodes.
    // Non-BR nodes use these tables exclusively; BR nodes fall through from
    // static routing to global only for intra-AS destinations.
    // ─────────────────────────────────────────────────────────────────────
    NodeContainer allNodes = topo.AllNodes();
    multias::OspfLikeRouter ospf(cfg);
    ospf.Install(allNodes);

    // ─────────────────────────────────────────────────────────────────────
    // Phase 4 — Traffic generation
    // OnOff sources + PacketSink receivers for 4 + nodeCount/10 inter-AS
    // UDP flows (5 Mbps, 1500 B, always-on). Start at t=2s.
    // ─────────────────────────────────────────────────────────────────────
    multias::TrafficGenerator tg(cfg);
    tg.InstallFlows(topo, cfg);

    // ─────────────────────────────────────────────────────────────────────
    // Phase 5 — Metrics (FlowMonitor)
    // Install after routing and applications so all flows are captured.
    // Register each flow's AS membership so DumpCsv can annotate rows.
    // ─────────────────────────────────────────────────────────────────────
    multias::MetricsCollector mc(cfg);
    mc.Setup(allNodes);

    for (const auto& fi : tg.GetFlows()) {
        mc.RegisterFlow(fi.port, fi.srcAs, fi.dstAs);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Phase 6 — Failure timeline
    //
    //  t = 0 .. failureTime   : STABLE — direct AS1↔AS3 path (metric=30)
    //  t = failureTime        : FAILURE INJECTION
    //                           ↓ BGP convergence delay 2–5s
    //                           ↓ Alternative path via AS2 (metric=80)
    //  t = failureTime + 30s  : RECOVERY (if time permits)
    // ─────────────────────────────────────────────────────────────────────
    if (!cfg.noFailure && cfg.failureTime < cfg.simTime) {
        auto linkId = multias::LinkIdFromString(cfg.failureLink);
        bgp.ScheduleLinkFailure(Seconds(cfg.failureTime), linkId, mc);

        double recoveryAt = cfg.failureTime + 30.0;
        if (recoveryAt < cfg.simTime) {
            bgp.ScheduleLinkRecovery(Seconds(recoveryAt), linkId);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Phase 7 — NetAnim animation
    // Layout:  AS1 column x=15, AS2 x=55, AS3 x=95  (units: metres in NetAnim)
    //          y spread evenly in [5, 95] per AS node count.
    // Colours: BR=red, internal router=blue.
    // ─────────────────────────────────────────────────────────────────────
    std::string animId   = cfg.scenarioId.empty() ? "sim" : cfg.scenarioId;
    std::string animFile = cfg.outDir + "/anim_" + animId
                         + "_run" + std::to_string(cfg.runId) + ".xml";
    AnimationInterface anim(animFile);

    // Packet metadata on; cap file size for large scenarios.
    anim.EnablePacketMetadata(true);
    anim.SetMaxPktsPerTraceFile(500000);

    // ── Node positions ────────────────────────────────────────────────────
    constexpr double AS_X[4] = {0.0, 15.0, 55.0, 95.0}; // index 1-based

    for (uint32_t asId = 1; asId <= 3; ++asId) {
        const ns3::NodeContainer& nc = topo.GetAsNodes(asId);
        uint32_t n = nc.GetN();
        for (uint32_t j = 0; j < n; ++j) {
            double y = (n <= 1) ? 50.0 : (5.0 + 90.0 * j / (n - 1));
            anim.SetConstantPosition(nc.Get(j), AS_X[asId], y);
        }
    }

    // ── Node colours and descriptions ─────────────────────────────────────
    // BRs (index 0, 1 in each AS) → red
    // Non-BRs (index 2+) → blue
    for (uint32_t asId = 1; asId <= 3; ++asId) {
        const ns3::NodeContainer& nc = topo.GetAsNodes(asId);
        const std::string asTag = "AS" + std::to_string(asId);

        ns3::Ptr<ns3::Node> bra = topo.GetBorderRouter(asId, 0);
        ns3::Ptr<ns3::Node> brb = topo.GetBorderRouter(asId, 1);

        anim.UpdateNodeColor(bra, 255, 0, 0);
        anim.UpdateNodeDescription(bra, asTag + "-BRa");
        anim.UpdateNodeColor(brb, 255, 0, 0);
        anim.UpdateNodeDescription(brb, asTag + "-BRb");

        for (uint32_t j = 2; j < nc.GetN(); ++j) {
            anim.UpdateNodeColor(nc.Get(j), 0, 100, 200);
            anim.UpdateNodeDescription(nc.Get(j),
                asTag + "-R" + std::to_string(j));
        }
    }

    // ── Inter-AS link descriptions ────────────────────────────────────────
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

    // ─────────────────────────────────────────────────────────────────────
    // Run
    // ─────────────────────────────────────────────────────────────────────
    Simulator::Stop(Seconds(cfg.simTime));
    Simulator::Run();

    // ─────────────────────────────────────────────────────────────────────
    // Post-simulation output
    // ─────────────────────────────────────────────────────────────────────
    std::string csvBase = cfg.outDir + "/metrics";
    if (!cfg.scenarioId.empty()) csvBase += "_" + cfg.scenarioId
                                          + "_run" + std::to_string(cfg.runId);
    mc.DumpCsv(csvBase + ".csv", cfg);
    mc.Dump(); // also write the ns-3 FlowMonitor XML

    Simulator::Destroy();
    return 0;
}
