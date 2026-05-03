#pragma once

#include "scenario_config.h"

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ns3 {
class Node;
class UniformRandomVariable;
class Ipv4StaticRouting;
} // namespace ns3

namespace multias {

class TopologyBuilder;
class MetricsCollector;

// Mirrors the insertion order in TopologyBuilder::BuildInterAs().
enum class InterAsLinkId : uint32_t {
    AS1_AS2_PRIMARY = 0,
    AS1_AS2_BACKUP  = 1,
    AS2_AS3_PRIMARY = 2,
    AS2_AS3_BACKUP  = 3,
    AS1_AS3_PRIMARY = 4,
};

// Parse "AS1_AS3_PRIMARY" → InterAsLinkId::AS1_AS3_PRIMARY. Fatal on unknown.
InterAsLinkId LinkIdFromString(const std::string& s);

// BGP-like inter-domain routing helper.
//
// Installs /16 static policy routes on all 6 border routers so each BR
// knows which physical inter-AS link to use as primary vs backup.
// Ipv4StaticRouting (priority 10) takes precedence over Ipv4GlobalRouting
// (priority 5) inside ListRouting, so only BRs follow these policies;
// all other nodes fall through to the global routing table.
//
// Failure model:
//   1. Bring both link endpoints DOWN via Ipv4::SetDown().
//   2. Notify MetricsCollector (for convergence timing).
//   3. After a seeded uniform(2s, 5s) BGP convergence delay, call
//      RecomputeBgpTables() which withdraws all static routes that used
//      the failed link and triggers a global-routing recompute for non-BR nodes.
//   4. The backup static route (higher metric, lower priority in table)
//      is now the only match → it activates automatically.
class BgpLikeRouter {
public:
    explicit BgpLikeRouter(const ScenarioConfig& cfg);

    // Install /16 policy routes on all border routers.
    // Must be called after TopologyBuilder::Build() and before
    // OspfLikeRouter::Install() (PopulateRoutingTables).
    void ConfigureBorderPolicies(TopologyBuilder& topo);

    // Schedule link failure at simulation time `at`.
    void ScheduleLinkFailure(ns3::Time at, InterAsLinkId linkId, MetricsCollector& mc);

    // Schedule link recovery at simulation time `at` (optional).
    void ScheduleLinkRecovery(ns3::Time at, InterAsLinkId linkId);

    // Called after BGP convergence delay: removes stale static routes and
    // triggers Ipv4GlobalRoutingHelper::RecomputeRoutingTables().
    void RecomputeBgpTables(InterAsLinkId linkId);

private:
    // Full description of one installed policy route — kept so routes can
    // be removed on failure and re-added on recovery without re-running
    // ConfigureBorderPolicies().
    struct PolicyRoute {
        InterAsLinkId    viaLink;
        ns3::Ptr<ns3::Node>   brNode;
        ns3::Ipv4Address nextHop;     // remote endpoint's IP on the inter-AS link
        ns3::Ipv4Address destNetwork;
        ns3::Ipv4Mask    destMask;
        uint32_t         ifIndex;     // local interface on brNode for this link
        uint32_t         metric;      // lower → preferred; primary < backup
    };

    void TriggerFailure(InterAsLinkId linkId, MetricsCollector* mc);
    void TriggerRecovery(InterAsLinkId linkId);
    void SetLinkDown(InterAsLinkId linkId);
    void SetLinkUp(InterAsLinkId linkId);

    ns3::Ptr<ns3::Ipv4StaticRouting> GetStaticRouting(ns3::Ptr<ns3::Node> node) const;

    // Add one policy route and record it in m_policyRoutes.
    void AddPolicyRoute(ns3::Ptr<ns3::Node>  node,
                        ns3::Ipv4Address     destNet,
                        ns3::Ipv4Mask        destMask,
                        ns3::Ipv4Address     nextHop,
                        uint32_t             ifIndex,
                        uint32_t             metric,
                        InterAsLinkId        viaLink);

    // Remove all static routes on `sr` that use `nextHop` as gateway.
    void RemoveRoutesVia(ns3::Ptr<ns3::Ipv4StaticRouting> sr,
                         ns3::Ipv4Address                 nextHop);

    ScenarioConfig            m_cfg;
    TopologyBuilder*          m_topo = nullptr;
    MetricsCollector*         m_mc   = nullptr; // stored at first failure for convergence callback
    std::vector<PolicyRoute>  m_policyRoutes;
    std::set<InterAsLinkId>   m_downLinks;
    ns3::Ptr<ns3::UniformRandomVariable> m_rng;
};

} // namespace multias
