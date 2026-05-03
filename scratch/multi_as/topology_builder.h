#pragma once

#include "scenario_config.h"

#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/ptr.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace ns3 {
class Node;
class UniformRandomVariable;
} // namespace ns3

namespace multias {

struct LinkProfile {
    std::string dataRate;
    std::string delay;
};

// Inline constants — single definition across translation units (C++17).
inline const LinkProfile kIntraLink    { "100Mbps", "2ms"  };
inline const LinkProfile kInterPrimary { "1Gbps",   "5ms"  };
inline const LinkProfile kInterBackup  { "100Mbps", "15ms" };

struct InterAsLink {
    uint32_t srcAs;    // 1-based AS index
    uint32_t dstAs;
    uint32_t srcBrIdx; // 0 = BR_a, 1 = BR_b
    uint32_t dstBrIdx;
    bool     isPrimary;
    ns3::NetDeviceContainer     devices;
    ns3::Ipv4InterfaceContainer interfaces;
};

class TopologyBuilder {
public:
    explicit TopologyBuilder(const ScenarioConfig& cfg);

    // Builds all nodes, links, and IP assignments.
    // Also calls LogTopologySummary() at the end.
    // Precondition: RngSeedManager not yet consumed (Build sets seed/run).
    void Build();

    // Accessors (asId is 1-based: 1, 2, or 3)
    const ns3::NodeContainer&                        GetAsNodes(uint32_t asId) const;
    ns3::Ptr<ns3::Node>                              GetBorderRouter(uint32_t asId, uint32_t which) const;
    const std::vector<ns3::Ipv4InterfaceContainer>&  GetAllInterfaces() const;
    const std::vector<InterAsLink>&                  GetInterAsLinks() const;

    // Returns a single NodeContainer holding all nodes from AS1, AS2, AS3.
    ns3::NodeContainer AllNodes() const;

    void LogTopologySummary() const;
    void DumpTopologyJson(const std::string& path) const;

private:
    // Returns per-AS node counts based on distributionMode and nodeCount.
    std::array<uint32_t, 3> ComputeAsSize() const;

    // Builds intra-AS topology for the given 0-based AS index.
    // Uses two-group spanning tree to guarantee 2 vertex-disjoint paths between BRs.
    void BuildIntraAs(uint32_t asIdx);

    // Builds all 5 inter-AS links.
    void BuildInterAs();

    // Installs a single inter-AS P2P link and assigns IP from m_interAddrHelper.
    void MakeInterLink(uint32_t srcAs, uint32_t dstAs,
                       uint32_t srcBrIdx, uint32_t dstBrIdx,
                       const LinkProfile& profile, bool isPrimary);

    ScenarioConfig m_cfg;

    // m_asNodes[0..2] → AS1..AS3; m_asNodes[i].Get(0)=BR_a, Get(1)=BR_b
    std::array<ns3::NodeContainer, 3>                        m_asNodes;
    std::array<std::array<ns3::Ptr<ns3::Node>, 2>, 3>        m_brNodes;
    std::array<std::vector<std::pair<uint32_t, uint32_t>>, 3> m_intraEdges;
    std::vector<ns3::Ipv4InterfaceContainer>                  m_allIfaces;
    std::vector<InterAsLink>                                  m_interEdges;

    // Per-AS address helpers, advanced with NewNetwork() after each link.
    // Intra: 10.{asId}.0.0/30 pools. Inter: 172.16.1.0/30 sequential.
    std::array<ns3::Ipv4AddressHelper, 3> m_intraAddrHelper;
    ns3::Ipv4AddressHelper                m_interAddrHelper;

    ns3::Ptr<ns3::UniformRandomVariable> m_rng;
};

} // namespace multias
