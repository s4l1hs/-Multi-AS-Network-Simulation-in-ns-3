#include "inter_as_router.h"

#include "topology_builder.h"
#include "metrics_collector.h"

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

NS_LOG_COMPONENT_DEFINE("BgpLikeRouter");

namespace multias {

static const char* LinkIdToString(InterAsLinkId id)
{
    switch (id) {
        case InterAsLinkId::AS1_AS2_PRIMARY: return "AS1_AS2_PRIMARY";
        case InterAsLinkId::AS1_AS2_BACKUP:  return "AS1_AS2_BACKUP";
        case InterAsLinkId::AS2_AS3_PRIMARY: return "AS2_AS3_PRIMARY";
        case InterAsLinkId::AS2_AS3_BACKUP:  return "AS2_AS3_BACKUP";
        case InterAsLinkId::AS1_AS3_PRIMARY: return "AS1_AS3_PRIMARY";
        default: return "UNKNOWN";
    }
}

InterAsLinkId LinkIdFromString(const std::string& s)
{
    if (s == "AS1_AS2_PRIMARY") return InterAsLinkId::AS1_AS2_PRIMARY;
    if (s == "AS1_AS2_BACKUP")  return InterAsLinkId::AS1_AS2_BACKUP;
    if (s == "AS2_AS3_PRIMARY") return InterAsLinkId::AS2_AS3_PRIMARY;
    if (s == "AS2_AS3_BACKUP")  return InterAsLinkId::AS2_AS3_BACKUP;
    if (s == "AS1_AS3_PRIMARY") return InterAsLinkId::AS1_AS3_PRIMARY;
    NS_FATAL_ERROR("Unknown InterAsLinkId string: '" << s
                   << "'. Valid: AS1_AS2_PRIMARY, AS1_AS2_BACKUP, "
                   "AS2_AS3_PRIMARY, AS2_AS3_BACKUP, AS1_AS3_PRIMARY");
}

// Returns the local interface index on `node` for the device `dev`.
static uint32_t IfFor(ns3::Ptr<ns3::Node> node, ns3::Ptr<ns3::NetDevice> dev)
{
    ns3::Ptr<ns3::Ipv4> ipv4 = node->GetObject<ns3::Ipv4>();
    NS_ASSERT_MSG(ipv4, "Node " << node->GetId() << " has no IPv4 object");
    int32_t idx = ipv4->GetInterfaceForDevice(dev);
    NS_ASSERT_MSG(idx >= 0, "Device not found on node " << node->GetId());
    return static_cast<uint32_t>(idx);
}

BgpLikeRouter::BgpLikeRouter(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

ns3::Ptr<ns3::Ipv4StaticRouting>
BgpLikeRouter::GetStaticRouting(ns3::Ptr<ns3::Node> node) const
{
    ns3::Ipv4StaticRoutingHelper helper;
    ns3::Ptr<ns3::Ipv4StaticRouting> sr =
        helper.GetStaticRouting(node->GetObject<ns3::Ipv4>());
    NS_ASSERT_MSG(sr, "Node " << node->GetId() << " has no Ipv4StaticRouting — "
                  "did TopologyBuilder install ListRouting?");
    return sr;
}

void BgpLikeRouter::AddPolicyRoute(ns3::Ptr<ns3::Node>  node,
                                   ns3::Ipv4Address     destNet,
                                   ns3::Ipv4Mask        destMask,
                                   ns3::Ipv4Address     nextHop,
                                   uint32_t             ifIndex,
                                   uint32_t             metric,
                                   InterAsLinkId        viaLink)
{
    auto sr = GetStaticRouting(node);
    sr->AddNetworkRouteTo(destNet, destMask, nextHop, ifIndex, metric);

    NS_LOG_INFO("[BR " << node->GetId() << "] route " << destNet << "/"
                << destMask << " nh=" << nextHop << " if=" << ifIndex
                << " metric=" << metric << " via=" << LinkIdToString(viaLink));

    m_policyRoutes.push_back({viaLink, node, nextHop, destNet, destMask, ifIndex, metric});
}

void BgpLikeRouter::RemoveRoutesVia(ns3::Ptr<ns3::Ipv4StaticRouting> sr,
                                    ns3::Ipv4Address                 nextHop)
{
    // Iterate backward-safe: RemoveRoute(i) shifts higher indices down by 1.
    uint32_t i = 0;
    while (i < sr->GetNRoutes()) {
        ns3::Ipv4RoutingTableEntry route = sr->GetRoute(i);
        if (route.GetGateway() == nextHop) {
            NS_LOG_UNCOND("    Withdraw " << route.GetDestNetwork()
                          << "/" << route.GetDestNetworkMask()
                          << " nh=" << nextHop);
            sr->RemoveRoute(i);
            // Don't advance i; the next entry shifted into this slot.
        } else {
            ++i;
        }
    }
}

// Link indices match BuildInterAs() order: [0]=AS1-AS2 pri, [1]=AS1-AS2 bk,
// [2]=AS2-AS3 pri, [3]=AS2-AS3 bk, [4]=AS1-AS3 direct (fails at t=failureTime).
// Primary metric=30 (AS1↔AS3 direct), backup metric=80 (via AS2).
void BgpLikeRouter::ConfigureBorderPolicies(TopologyBuilder& topo)
{
    m_topo = &topo;
    m_rng  = ns3::CreateObject<ns3::UniformRandomVariable>();

    const auto& L = topo.GetInterAsLinks(); // shorthand

    // Destination-network /16 aggregates matching the 10.i.X.Y/30 intra subnets.
    const ns3::Ipv4Address as1Net("10.1.0.0");
    const ns3::Ipv4Address as2Net("10.2.0.0");
    const ns3::Ipv4Address as3Net("10.3.0.0");
    const ns3::Ipv4Mask    slash16("255.255.0.0");

    // ── AS1.BR_a ─────────────────────────────────────────────────────────
    // Directly connected to: link[0] (AS1↔AS2 primary), link[4] (AS1↔AS3 direct)
    {
        ns3::Ptr<ns3::Node> n = topo.GetBorderRouter(1, 0);

        // TO AS2: primary via link[0]
        AddPolicyRoute(n, as2Net, slash16,
                       L[0].interfaces.GetAddress(1),      // AS2.BR_a IP on link0
                       IfFor(n, L[0].devices.Get(0)),
                       50, InterAsLinkId::AS1_AS2_PRIMARY);

        // TO AS3: direct (primary) via link[4]
        AddPolicyRoute(n, as3Net, slash16,
                       L[4].interfaces.GetAddress(1),      // AS3.BR_a IP on link4
                       IfFor(n, L[4].devices.Get(0)),
                       30, InterAsLinkId::AS1_AS3_PRIMARY);

        // TO AS3: alternative (backup) via AS2 on link[0]
        // Activates automatically when the metric=30 route is withdrawn.
        AddPolicyRoute(n, as3Net, slash16,
                       L[0].interfaces.GetAddress(1),      // AS2.BR_a IP on link0
                       IfFor(n, L[0].devices.Get(0)),
                       80, InterAsLinkId::AS1_AS2_PRIMARY);
    }

    // ── AS1.BR_b ─────────────────────────────────────────────────────────
    // Directly connected to: link[1] (AS1↔AS2 backup)
    {
        ns3::Ptr<ns3::Node> n = topo.GetBorderRouter(1, 1);

        // TO AS2: via link[1]
        AddPolicyRoute(n, as2Net, slash16,
                       L[1].interfaces.GetAddress(1),      // AS2.BR_b IP on link1
                       IfFor(n, L[1].devices.Get(0)),
                       50, InterAsLinkId::AS1_AS2_BACKUP);

        // TO AS3: via AS2.BR_b (must transit AS2 — no direct link)
        AddPolicyRoute(n, as3Net, slash16,
                       L[1].interfaces.GetAddress(1),      // AS2.BR_b IP on link1
                       IfFor(n, L[1].devices.Get(0)),
                       80, InterAsLinkId::AS1_AS2_BACKUP);
    }

    // ── AS2.BR_a ─────────────────────────────────────────────────────────
    // Directly connected to: link[0] (AS1↔AS2 primary), link[2] (AS2↔AS3 primary)
    {
        ns3::Ptr<ns3::Node> n = topo.GetBorderRouter(2, 0);

        // TO AS1: via link[0] (AS2 is dst, so device.Get(1))
        AddPolicyRoute(n, as1Net, slash16,
                       L[0].interfaces.GetAddress(0),      // AS1.BR_a IP on link0
                       IfFor(n, L[0].devices.Get(1)),
                       50, InterAsLinkId::AS1_AS2_PRIMARY);

        // TO AS3: via link[2] (AS2 is src, so device.Get(0))
        AddPolicyRoute(n, as3Net, slash16,
                       L[2].interfaces.GetAddress(1),      // AS3.BR_a IP on link2
                       IfFor(n, L[2].devices.Get(0)),
                       50, InterAsLinkId::AS2_AS3_PRIMARY);
    }

    // ── AS2.BR_b ─────────────────────────────────────────────────────────
    // Directly connected to: link[1] (AS1↔AS2 backup), link[3] (AS2↔AS3 backup)
    {
        ns3::Ptr<ns3::Node> n = topo.GetBorderRouter(2, 1);

        // TO AS1: via link[1] (AS2 is dst)
        AddPolicyRoute(n, as1Net, slash16,
                       L[1].interfaces.GetAddress(0),      // AS1.BR_b IP on link1
                       IfFor(n, L[1].devices.Get(1)),
                       50, InterAsLinkId::AS1_AS2_BACKUP);

        // TO AS3: via link[3] (AS2 is src)
        AddPolicyRoute(n, as3Net, slash16,
                       L[3].interfaces.GetAddress(1),      // AS3.BR_b IP on link3
                       IfFor(n, L[3].devices.Get(0)),
                       50, InterAsLinkId::AS2_AS3_BACKUP);
    }

    // ── AS3.BR_a ─────────────────────────────────────────────────────────
    // Directly connected to: link[2] (AS2↔AS3 primary), link[4] (AS1↔AS3 direct)
    {
        ns3::Ptr<ns3::Node> n = topo.GetBorderRouter(3, 0);

        // TO AS2: via link[2] (AS3 is dst, device.Get(1))
        AddPolicyRoute(n, as2Net, slash16,
                       L[2].interfaces.GetAddress(0),      // AS2.BR_a IP on link2
                       IfFor(n, L[2].devices.Get(1)),
                       50, InterAsLinkId::AS2_AS3_PRIMARY);

        // TO AS1: direct (primary) via link[4] (AS3 is dst)
        AddPolicyRoute(n, as1Net, slash16,
                       L[4].interfaces.GetAddress(0),      // AS1.BR_a IP on link4
                       IfFor(n, L[4].devices.Get(1)),
                       30, InterAsLinkId::AS1_AS3_PRIMARY);

        // TO AS1: alternative (backup) via AS2 on link[2]
        AddPolicyRoute(n, as1Net, slash16,
                       L[2].interfaces.GetAddress(0),      // AS2.BR_a IP on link2
                       IfFor(n, L[2].devices.Get(1)),
                       80, InterAsLinkId::AS2_AS3_PRIMARY);
    }

    // ── AS3.BR_b ─────────────────────────────────────────────────────────
    // Directly connected to: link[3] (AS2↔AS3 backup)
    {
        ns3::Ptr<ns3::Node> n = topo.GetBorderRouter(3, 1);

        // TO AS2: via link[3] (AS3 is dst)
        AddPolicyRoute(n, as2Net, slash16,
                       L[3].interfaces.GetAddress(0),      // AS2.BR_b IP on link3
                       IfFor(n, L[3].devices.Get(1)),
                       50, InterAsLinkId::AS2_AS3_BACKUP);

        // TO AS1: via AS2.BR_b (must transit AS2 — no direct link)
        AddPolicyRoute(n, as1Net, slash16,
                       L[3].interfaces.GetAddress(0),      // AS2.BR_b IP on link3
                       IfFor(n, L[3].devices.Get(1)),
                       80, InterAsLinkId::AS2_AS3_BACKUP);
    }

    NS_LOG_UNCOND("[BGP] Border policies configured: "
                  << m_policyRoutes.size() << " routes on 6 border routers");
    NS_LOG_UNCOND("  AS1↔AS3 direct (metric=30) is best path until failure");
    NS_LOG_UNCOND("  AS1→AS2→AS3 (metric=80) is alternative path after failure");
}

void BgpLikeRouter::SetLinkDown(InterAsLinkId linkId)
{
    NS_ASSERT_MSG(m_topo, "ConfigureBorderPolicies() must be called first");
    const auto& link = m_topo->GetInterAsLinks()[static_cast<uint32_t>(linkId)];

    auto bringDown = [](ns3::Ptr<ns3::NetDevice> dev) {
        ns3::Ptr<ns3::Node> node = dev->GetNode();
        ns3::Ptr<ns3::Ipv4> ipv4 = node->GetObject<ns3::Ipv4>();
        int32_t idx = ipv4->GetInterfaceForDevice(dev);
        NS_ASSERT_MSG(idx >= 0, "Device not found on node");
        ipv4->SetDown(static_cast<uint32_t>(idx));
        NS_LOG_INFO("  Node " << node->GetId() << " if=" << idx << " → DOWN");
    };

    bringDown(link.devices.Get(0));
    bringDown(link.devices.Get(1));
}

void BgpLikeRouter::SetLinkUp(InterAsLinkId linkId)
{
    NS_ASSERT_MSG(m_topo, "ConfigureBorderPolicies() must be called first");
    const auto& link = m_topo->GetInterAsLinks()[static_cast<uint32_t>(linkId)];

    auto bringUp = [](ns3::Ptr<ns3::NetDevice> dev) {
        ns3::Ptr<ns3::Node> node = dev->GetNode();
        ns3::Ptr<ns3::Ipv4> ipv4 = node->GetObject<ns3::Ipv4>();
        int32_t idx = ipv4->GetInterfaceForDevice(dev);
        NS_ASSERT_MSG(idx >= 0, "Device not found on node");
        ipv4->SetUp(static_cast<uint32_t>(idx));
        NS_LOG_INFO("  Node " << node->GetId() << " if=" << idx << " → UP");
    };

    bringUp(link.devices.Get(0));
    bringUp(link.devices.Get(1));
}

void BgpLikeRouter::ScheduleLinkFailure(ns3::Time        at,
                                        InterAsLinkId    linkId,
                                        MetricsCollector& mc)
{
    NS_LOG_UNCOND("[BGP] Link failure " << LinkIdToString(linkId)
                  << " scheduled at t=" << at.GetSeconds() << "s");
    ns3::Simulator::Schedule(at, &BgpLikeRouter::TriggerFailure, this, linkId, &mc);
}

void BgpLikeRouter::TriggerFailure(InterAsLinkId linkId, MetricsCollector* mc)
{
    NS_LOG_UNCOND("=== [t=" << ns3::Simulator::Now().GetSeconds()
                  << "s] LINK FAILURE: " << LinkIdToString(linkId) << " ===");

    // 1. Bring the link down at the IP layer.
    SetLinkDown(linkId);
    m_downLinks.insert(linkId);

    // 2. Notify MetricsCollector (convergence timer starts here).
    m_mc = mc; // stash for MarkConvergenceEvent in RecomputeBgpTables
    if (mc) mc->MarkFailureEvent(static_cast<uint32_t>(linkId), ns3::Simulator::Now());

    // 3. Simulate BGP hold-timer + UPDATE propagation delay: uniform [2s, 5s].
    double convDelay = m_rng->GetValue(2.0, 5.0);
    NS_LOG_UNCOND("[BGP] Convergence in " << convDelay
                  << "s (BGP hold-timer + UPDATE propagation)");

    ns3::Simulator::Schedule(ns3::Seconds(convDelay),
                             &BgpLikeRouter::RecomputeBgpTables, this, linkId);
}

void BgpLikeRouter::RecomputeBgpTables(InterAsLinkId linkId)
{
    NS_LOG_UNCOND("=== [t=" << ns3::Simulator::Now().GetSeconds()
                  << "s] BGP RE-CONVERGENCE: withdrawing routes via "
                  << LinkIdToString(linkId) << " ===");

    // Collect unique (brNode, nextHop) pairs for routes via the failed link.
    // Using a per-BR loop avoids calling RemoveRoutesVia multiple times with
    // the same arguments.
    std::set<std::pair<ns3::Ptr<ns3::Node>, ns3::Ipv4Address>> toWithdraw;

    for (const auto& pr : m_policyRoutes) {
        if (pr.viaLink == linkId) {
            toWithdraw.insert({pr.brNode, pr.nextHop});
        }
    }

    for (auto& [node, nh] : toWithdraw) {
        NS_LOG_UNCOND("  BR " << node->GetId() << ": withdraw all routes via nh=" << nh);
        RemoveRoutesVia(GetStaticRouting(node), nh);
    }

    // Recompute global routing for non-BR nodes so they follow the new topology.
    ns3::Ipv4GlobalRoutingHelper::RecomputeRoutingTables();

    // Record actual convergence time (failure → BGP delay → this point).
    if (m_mc) m_mc->MarkConvergenceEvent(static_cast<uint32_t>(linkId),
                                          ns3::Simulator::Now());

    NS_LOG_UNCOND("[BGP] Alternative path now ACTIVE:");
    NS_LOG_UNCOND("  Traffic rerouted via AS2 (metric=80 backup routes)");
    NS_LOG_UNCOND("  Path: AS1.BR_a → AS2.BR_a → AS3.BR_a");
}

void BgpLikeRouter::ScheduleLinkRecovery(ns3::Time at, InterAsLinkId linkId)
{
    NS_LOG_UNCOND("[BGP] Link recovery " << LinkIdToString(linkId)
                  << " scheduled at t=" << at.GetSeconds() << "s");
    ns3::Simulator::Schedule(at, &BgpLikeRouter::TriggerRecovery, this, linkId);
}

void BgpLikeRouter::TriggerRecovery(InterAsLinkId linkId)
{
    if (m_downLinks.find(linkId) == m_downLinks.end()) {
        NS_LOG_WARN("[BGP] Recovery for " << LinkIdToString(linkId)
                    << " but link was not marked down — skipping");
        return;
    }

    NS_LOG_UNCOND("=== [t=" << ns3::Simulator::Now().GetSeconds()
                  << "s] LINK RECOVERY: " << LinkIdToString(linkId) << " ===");

    // 1. Restore IP interfaces.
    SetLinkUp(linkId);
    m_downLinks.erase(linkId);

    // 2. Re-install the policy routes that were removed during failure.
    for (const auto& pr : m_policyRoutes) {
        if (pr.viaLink != linkId) continue;

        auto sr = GetStaticRouting(pr.brNode);
        sr->AddNetworkRouteTo(pr.destNetwork, pr.destMask,
                              pr.nextHop, pr.ifIndex, pr.metric);
        NS_LOG_UNCOND("  BR " << pr.brNode->GetId() << ": restored "
                      << pr.destNetwork << "/" << pr.destMask
                      << " nh=" << pr.nextHop << " metric=" << pr.metric);
    }

    // 3. Recompute global routing.
    ns3::Ipv4GlobalRoutingHelper::RecomputeRoutingTables();

    NS_LOG_UNCOND("[BGP] Primary path RESTORED: AS1↔AS3 direct link active again");
}

} // namespace multias
