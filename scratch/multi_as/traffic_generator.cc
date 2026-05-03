#include "traffic_generator.h"

#include "topology_builder.h"

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"

#include <algorithm>

NS_LOG_COMPONENT_DEFINE("TrafficGenerator");

namespace multias {

TrafficGenerator::TrafficGenerator(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

ns3::Ptr<ns3::Node>
TrafficGenerator::PickNonBr(const TopologyBuilder&               topo,
                             uint32_t                             asId,
                             ns3::Ptr<ns3::UniformRandomVariable> rng) const
{
    const ns3::NodeContainer& nodes = topo.GetAsNodes(asId);
    const uint32_t n = nodes.GetN();

    if (n <= 2) {
        // Degenerate AS: only BRs exist — use one of them.
        return nodes.Get(rng->GetInteger(0, n - 1));
    }
    // Non-BR nodes occupy indices [2 .. n-1].
    return nodes.Get(rng->GetInteger(2, n - 1));
}

ns3::Ipv4Address
TrafficGenerator::GetNodeFirstIp(ns3::Ptr<ns3::Node> node) const
{
    ns3::Ptr<ns3::Ipv4> ipv4 = node->GetObject<ns3::Ipv4>();
    NS_ASSERT_MSG(ipv4, "Node " << node->GetId() << " has no Ipv4 object");

    // Interface 0 is always the loopback; skip it.
    for (uint32_t i = 1; i < ipv4->GetNInterfaces(); ++i) {
        if (ipv4->GetNAddresses(i) > 0) {
            ns3::Ipv4Address addr = ipv4->GetAddress(i, 0).GetLocal();
            if (addr != ns3::Ipv4Address("127.0.0.1")) return addr;
        }
    }
    NS_FATAL_ERROR("Node " << node->GetId() << " has no usable IP address");
    return ns3::Ipv4Address("0.0.0.0"); // unreachable
}

ns3::ApplicationContainer
TrafficGenerator::InstallFlows(const TopologyBuilder& topo,
                                const ScenarioConfig&  cfg)
{
    // RNG inherits the global seed set by TopologyBuilder::Build().
    auto rng = ns3::CreateObject<ns3::UniformRandomVariable>();

    const double startTime = 2.0;                     // let topology stabilise
    const double stopTime  = cfg.simTime - 1.0;

    if (stopTime <= startTime) {
        NS_LOG_WARN("simTime=" << cfg.simTime
                    << "s too short for traffic (need > 3s) — no flows installed");
        return ns3::ApplicationContainer{};
    }

    // Base: 4 mandatory inter-AS pairs.
    // Extra: cfg.nodeCount/10 additional flows, cycling through an extended
    // set of directions (includes reverse directions for more variety).
    using Dir = std::pair<uint32_t, uint32_t>;

    const std::vector<Dir> baseDirs  = {{1,2},{2,3},{1,3},{3,1}};
    const std::vector<Dir> extraPool = {{2,1},{3,2},{1,2},{2,3},{1,3},{3,1}};

    std::vector<Dir> allDirs = baseDirs;
    const uint32_t   extra   = cfg.nodeCount / 10;
    for (uint32_t i = 0; i < extra; ++i) {
        allDirs.push_back(extraPool[i % extraPool.size()]);
    }

    ns3::ApplicationContainer allApps;
    uint16_t nextPort = 9000;

    NS_LOG_UNCOND("[Traffic] Installing " << allDirs.size()
                  << " flows (" << baseDirs.size() << " base + " << extra << " extra)"
                  << "  start=" << startTime << "s  stop=" << stopTime << "s");

    for (const auto& [srcAs, dstAs] : allDirs) {
        ns3::Ptr<ns3::Node> srcNode = PickNonBr(topo, srcAs, rng);
        ns3::Ptr<ns3::Node> dstNode = PickNonBr(topo, dstAs, rng);

        ns3::Ipv4Address dstAddr = GetNodeFirstIp(dstNode);
        const uint16_t   port    = nextPort++;

        ns3::PacketSinkHelper sinkHelper(
            "ns3::UdpSocketFactory",
            ns3::InetSocketAddress(ns3::Ipv4Address::GetAny(), port));

        ns3::ApplicationContainer sinkApp = sinkHelper.Install(dstNode);
        sinkApp.Start(ns3::Seconds(startTime - 1.0)); // sink up before source
        sinkApp.Stop(ns3::Seconds(cfg.simTime));
        allApps.Add(sinkApp);

        ns3::OnOffHelper onoff(
            "ns3::UdpSocketFactory",
            ns3::InetSocketAddress(dstAddr, port));

        // Constant on (no off period) — always-on UDP source.
        onoff.SetConstantRate(ns3::DataRate("5Mbps"), 1500);
        onoff.SetAttribute("OnTime",
                           ns3::StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime",
                           ns3::StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ns3::ApplicationContainer srcApp = onoff.Install(srcNode);
        srcApp.Start(ns3::Seconds(startTime));
        srcApp.Stop(ns3::Seconds(stopTime));
        allApps.Add(srcApp);

        m_flows.push_back({srcAs, dstAs,
                           srcNode->GetId(), dstNode->GetId(), port});

        NS_LOG_UNCOND("  Flow AS" << srcAs << "→AS" << dstAs
                      << "  src=node" << srcNode->GetId()
                      << "  dst=node" << dstNode->GetId()
                      << "  dstIP="   << dstAddr
                      << "  port="    << port);
    }

    NS_LOG_UNCOND("[Traffic] " << m_flows.size() << " flows installed"
                  << "  ports=" << 9000 << ".." << (nextPort - 1));

    return allApps;
}

} // namespace multias
