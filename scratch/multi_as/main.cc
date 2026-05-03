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

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MultiAsSim");

int main(int argc, char* argv[])
{
    multias::ScenarioConfig cfg;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nodes",      "Total number of nodes",                      cfg.nodeCount);
    cmd.AddValue("dist",       "Distribution mode: balanced|unbalanced",     cfg.distributionMode);
    cmd.AddValue("scenarioId", "Scenario identifier string",                 cfg.scenarioId);
    cmd.AddValue("runId",      "Run index (for repeated trials)",            cfg.runId);
    cmd.AddValue("simTime",    "Simulation duration in seconds",             cfg.simTime);
    cmd.AddValue("outDir",     "Output directory for results",               cfg.outDir);
    cmd.AddValue("seed",       "RNG seed (passed to RngSeedManager)",        cfg.seed);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(cfg.seed);
    RngSeedManager::SetRun(cfg.runId);

    NS_LOG_UNCOND("=== MultiAsSim configuration ===");
    NS_LOG_UNCOND("  nodes      = " << cfg.nodeCount);
    NS_LOG_UNCOND("  dist       = " << cfg.distributionMode);
    NS_LOG_UNCOND("  scenarioId = " << cfg.scenarioId);
    NS_LOG_UNCOND("  runId      = " << cfg.runId);
    NS_LOG_UNCOND("  simTime    = " << cfg.simTime << " s");
    NS_LOG_UNCOND("  outDir     = " << cfg.outDir);
    NS_LOG_UNCOND("  seed       = " << cfg.seed);
    NS_LOG_UNCOND("================================");

    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
