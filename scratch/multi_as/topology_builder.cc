#include "topology_builder.h"

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

NS_LOG_COMPONENT_DEFINE("TopologyBuilder");

namespace multias {

// ── Construction ──────────────────────────────────────────────────────────────

TopologyBuilder::TopologyBuilder(const ScenarioConfig& cfg)
    : m_cfg(cfg)
{
}

// ── Public API ────────────────────────────────────────────────────────────────

const ns3::NodeContainer& TopologyBuilder::GetAsNodes(uint32_t asId) const
{
    NS_ASSERT_MSG(asId >= 1 && asId <= 3, "asId must be 1, 2, or 3");
    return m_asNodes[asId - 1];
}

ns3::Ptr<ns3::Node> TopologyBuilder::GetBorderRouter(uint32_t asId, uint32_t which) const
{
    NS_ASSERT_MSG(asId >= 1 && asId <= 3, "asId must be 1, 2, or 3");
    NS_ASSERT_MSG(which <= 1, "which must be 0 (BR_a) or 1 (BR_b)");
    return m_brNodes[asId - 1][which];
}

ns3::NodeContainer TopologyBuilder::AllNodes() const
{
    ns3::NodeContainer all;
    for (uint32_t i = 0; i < 3; ++i) all.Add(m_asNodes[i]);
    return all;
}

const std::vector<ns3::Ipv4InterfaceContainer>& TopologyBuilder::GetAllInterfaces() const
{
    return m_allIfaces;
}

const std::vector<InterAsLink>& TopologyBuilder::GetInterAsLinks() const
{
    return m_interEdges;
}

// ── Build ─────────────────────────────────────────────────────────────────────

void TopologyBuilder::Build()
{
    // Reproducibility: must be set before any RandomVariable is consumed.
    ns3::RngSeedManager::SetSeed(m_cfg.seed);
    ns3::RngSeedManager::SetRun(static_cast<uint64_t>(m_cfg.runId));

    m_rng = ns3::CreateObject<ns3::UniformRandomVariable>();

    // ── Validate & compute per-AS sizes ───────────────────────────────────
    auto sizes = ComputeAsSize();

    NS_LOG_INFO("Node distribution: AS1=" << sizes[0]
                << " AS2=" << sizes[1] << " AS3=" << sizes[2]);

    // ── Create nodes ──────────────────────────────────────────────────────
    for (uint32_t i = 0; i < 3; ++i) {
        m_asNodes[i].Create(sizes[i]);
    }

    // ── Install IPv4 stack with ListRouting on all nodes ─────────────────
    // Priority 10: Ipv4StaticRouting  — border routers use this for inter-AS
    //              policy routes; non-BR nodes have empty static tables and
    //              fall through to global routing automatically.
    // Priority  5: Ipv4GlobalRouting  — fills intra-AS and fallback routes.
    ns3::Ipv4StaticRoutingHelper staticRtHelper;
    ns3::Ipv4GlobalRoutingHelper globalRtHelper;
    ns3::Ipv4ListRoutingHelper   listRtHelper;
    listRtHelper.Add(staticRtHelper, 10);
    listRtHelper.Add(globalRtHelper,  5);

    ns3::InternetStackHelper internet;
    internet.SetRoutingHelper(listRtHelper);
    for (uint32_t i = 0; i < 3; ++i) {
        internet.Install(m_asNodes[i]);
    }

    // ── Initialise per-AS address helpers ─────────────────────────────────
    // Intra-AS: 10.{asId}.0.0/30, NewNetwork() advances by 4 each link.
    // Capacity: 64 links per /24 block; wraps automatically into next block.
    m_intraAddrHelper[0].SetBase("10.1.0.0", "255.255.255.252");
    m_intraAddrHelper[1].SetBase("10.2.0.0", "255.255.255.252");
    m_intraAddrHelper[2].SetBase("10.3.0.0", "255.255.255.252");

    // Inter-AS: 172.16.1.0/30, 172.16.1.4/30, ... (5 links max)
    m_interAddrHelper.SetBase("172.16.1.0", "255.255.255.252");

    // ── Build topology ────────────────────────────────────────────────────
    for (uint32_t i = 0; i < 3; ++i) {
        BuildIntraAs(i);
    }
    BuildInterAs();

    LogTopologySummary();
}

// ── ComputeAsSize ─────────────────────────────────────────────────────────────

std::array<uint32_t, 3> TopologyBuilder::ComputeAsSize() const
{
    const int n = m_cfg.nodeCount;

    if (n != 20 && n != 50 && n != 100) {
        NS_FATAL_ERROR("nodeCount must be 20, 50, or 100. Got: " << n);
    }

    std::array<uint32_t, 3> sz{};

    if (m_cfg.distributionMode == "balanced") {
        // ~33/33/34: give ceiling to AS1 and AS2, remainder to AS3.
        sz[0] = static_cast<uint32_t>((n + 2) / 3); // ceil(N/3)
        sz[1] = static_cast<uint32_t>((n + 2) / 3);
        sz[2] = static_cast<uint32_t>(n) - sz[0] - sz[1];
    } else if (m_cfg.distributionMode == "unbalanced") {
        // ~20/30/50
        sz[0] = static_cast<uint32_t>(std::round(n * 0.20));
        sz[1] = static_cast<uint32_t>(std::round(n * 0.30));
        sz[2] = static_cast<uint32_t>(n) - sz[0] - sz[1];
    } else {
        NS_FATAL_ERROR("distributionMode must be 'balanced' or 'unbalanced'. Got: "
                       << m_cfg.distributionMode);
    }

    // Sanity: every AS needs at least 2 nodes (2 border routers).
    for (uint32_t i = 0; i < 3; ++i) {
        NS_ASSERT_MSG(sz[i] >= 2,
                      "AS" << (i + 1) << " has " << sz[i] << " nodes — minimum is 2");
    }
    return sz;
}

// ── BuildIntraAs ──────────────────────────────────────────────────────────────
//
// Algorithm guarantees 2 vertex-disjoint paths between BR0 and BR1 for any N >= 2:
//
//  1. Divide interior nodes [2..N-1] into two groups via seeded shuffle +
//     alternating assignment. Each group forms a random spanning tree hanging
//     off its respective BR (BR0 → groupA, BR1 → groupB).
//
//  2. Cross-primary edge: direct BR0–BR1 link.
//
//  3. Cross-alternative edge: a non-BR node from groupA to a non-BR node from
//     groupB (or directly to the opposite BR if only one interior node exists).
//     Combined with (2), this creates two vertex-disjoint paths:
//       • BR0 → BR1  (direct)
//       • BR0 →…→ u → v →…→ BR1  (via interior nodes; no shared internals)
//
//  4. Extra random edges (max(2, N/4)) for additional OSPF shortest-path
//     candidates.

void TopologyBuilder::BuildIntraAs(uint32_t asIdx)
{
    const uint32_t n   = m_asNodes[asIdx].GetN();
    const uint32_t asId = asIdx + 1;

    NS_LOG_INFO("Building intra-AS topology: AS" << asId << " (" << n << " nodes)");

    // Border routers are always node indices 0 (BR_a) and 1 (BR_b) within the AS.
    m_brNodes[asIdx][0] = m_asNodes[asIdx].Get(0);
    m_brNodes[asIdx][1] = m_asNodes[asIdx].Get(1);

    std::set<std::pair<uint32_t, uint32_t>> edgeSet;

    ns3::PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", ns3::StringValue(kIntraLink.dataRate));
    p2p.SetChannelAttribute("Delay",   ns3::StringValue(kIntraLink.delay));

    // Returns true if the edge was new and was successfully installed.
    auto addEdge = [&](uint32_t u, uint32_t v) -> bool {
        if (u == v) return false;
        auto key = std::make_pair(std::min(u, v), std::max(u, v));
        if (!edgeSet.insert(key).second) return false;

        m_intraEdges[asIdx].emplace_back(u, v);

        ns3::NetDeviceContainer devs = p2p.Install(
            ns3::NodeContainer(m_asNodes[asIdx].Get(u),
                               m_asNodes[asIdx].Get(v)));

        auto iface = m_intraAddrHelper[asIdx].Assign(devs);
        m_intraAddrHelper[asIdx].NewNetwork();
        m_allIfaces.push_back(iface);
        return true;
    };

    // ── Phase 1: Two-group spanning trees ─────────────────────────────────
    std::vector<uint32_t> groupA = {0}; // rooted at BR0
    std::vector<uint32_t> groupB = {1}; // rooted at BR1

    // Shuffle interior nodes [2..n-1] using seeded RNG (Fisher-Yates).
    std::vector<uint32_t> remaining;
    remaining.reserve(n - 2);
    for (uint32_t i = 2; i < n; ++i) remaining.push_back(i);

    for (auto i = static_cast<uint32_t>(remaining.size()); i > 1; --i) {
        uint32_t j = m_rng->GetInteger(0, i - 1);
        std::swap(remaining[i - 1], remaining[j]);
    }

    // Alternate assignment: even-index nodes → groupA, odd → groupB.
    for (uint32_t idx = 0; idx < static_cast<uint32_t>(remaining.size()); ++idx) {
        uint32_t node = remaining[idx];
        if (idx % 2 == 0) {
            uint32_t parent = groupA[m_rng->GetInteger(0, static_cast<uint32_t>(groupA.size()) - 1)];
            addEdge(node, parent);
            groupA.push_back(node);
        } else {
            uint32_t parent = groupB[m_rng->GetInteger(0, static_cast<uint32_t>(groupB.size()) - 1)];
            addEdge(node, parent);
            groupB.push_back(node);
        }
    }

    // ── Phase 2: Cross edges ──────────────────────────────────────────────
    // Primary: direct BR0-BR1.
    addEdge(0, 1);

    // Alternative: non-BR cross edge to guarantee a second vertex-disjoint path.
    if (groupA.size() > 1 && groupB.size() > 1) {
        uint32_t u = groupA[m_rng->GetInteger(1, static_cast<uint32_t>(groupA.size()) - 1)];
        uint32_t v = groupB[m_rng->GetInteger(1, static_cast<uint32_t>(groupB.size()) - 1)];
        addEdge(u, v);
    } else if (groupA.size() > 1) {
        // groupB only contains BR1 → connect a groupA interior node directly to BR1.
        uint32_t u = groupA[m_rng->GetInteger(1, static_cast<uint32_t>(groupA.size()) - 1)];
        addEdge(u, 1);
    } else if (groupB.size() > 1) {
        // groupA only contains BR0 → connect BR0 to a groupB interior node.
        uint32_t v = groupB[m_rng->GetInteger(1, static_cast<uint32_t>(groupB.size()) - 1)];
        addEdge(0, v);
    }
    // n == 2: only the direct BR0-BR1 edge is possible; single path is unavoidable.

    // ── Phase 3: Extra random edges ───────────────────────────────────────
    // Adds redundancy for multiple OSPF shortest-path candidates; O(N) total.
    const uint32_t extraTarget  = std::max(2u, n / 4);
    const uint32_t maxAttempts  = extraTarget * 20; // bounded, avoids dense-graph hang
    uint32_t       extraAdded   = 0;

    for (uint32_t attempt = 0; attempt < maxAttempts && extraAdded < extraTarget; ++attempt) {
        uint32_t u = m_rng->GetInteger(0, n - 1);
        uint32_t v = m_rng->GetInteger(0, n - 1);
        if (addEdge(u, v)) ++extraAdded;
    }

    NS_LOG_INFO("AS" << asId << ": " << m_intraEdges[asIdx].size() << " intra links installed");
}

// ── BuildInterAs ─────────────────────────────────────────────────────────────
//
// Link matrix:
//   AS1↔AS2 primary : BR(1,a) ↔ BR(2,a)   1 Gbps / 5 ms
//   AS1↔AS2 backup  : BR(1,b) ↔ BR(2,b)   100 Mbps / 15 ms
//   AS2↔AS3 primary : BR(2,a) ↔ BR(3,a)   1 Gbps / 5 ms
//   AS2↔AS3 backup  : BR(2,b) ↔ BR(3,b)   100 Mbps / 15 ms
//   AS1↔AS3 direct  : BR(1,a) ↔ BR(3,a)   1 Gbps / 5 ms
//
// When AS1↔AS3 direct fails, traffic reroutes AS1→AS2→AS3 via the peerings above.

void TopologyBuilder::BuildInterAs()
{
    NS_LOG_INFO("Building inter-AS links");

    MakeInterLink(1, 2, 0, 0, kInterPrimary, true);   // AS1↔AS2 primary
    MakeInterLink(1, 2, 1, 1, kInterBackup,  false);  // AS1↔AS2 backup
    MakeInterLink(2, 3, 0, 0, kInterPrimary, true);   // AS2↔AS3 primary
    MakeInterLink(2, 3, 1, 1, kInterBackup,  false);  // AS2↔AS3 backup
    MakeInterLink(1, 3, 0, 0, kInterPrimary, true);   // AS1↔AS3 direct

    NS_LOG_INFO("Inter-AS links installed: " << m_interEdges.size());
}

void TopologyBuilder::MakeInterLink(uint32_t srcAs, uint32_t dstAs,
                                    uint32_t srcBrIdx, uint32_t dstBrIdx,
                                    const LinkProfile& profile, bool isPrimary)
{
    ns3::PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", ns3::StringValue(profile.dataRate));
    p2p.SetChannelAttribute("Delay",   ns3::StringValue(profile.delay));

    ns3::Ptr<ns3::Node> srcNode = m_brNodes[srcAs - 1][srcBrIdx];
    ns3::Ptr<ns3::Node> dstNode = m_brNodes[dstAs - 1][dstBrIdx];

    ns3::NetDeviceContainer devs = p2p.Install(ns3::NodeContainer(srcNode, dstNode));

    auto iface = m_interAddrHelper.Assign(devs);
    m_interAddrHelper.NewNetwork();
    m_allIfaces.push_back(iface);

    InterAsLink link;
    link.srcAs     = srcAs;
    link.dstAs     = dstAs;
    link.srcBrIdx  = srcBrIdx;
    link.dstBrIdx  = dstBrIdx;
    link.isPrimary = isPrimary;
    link.devices   = devs;
    link.interfaces = iface;
    m_interEdges.push_back(std::move(link));
}

// ── LogTopologySummary ────────────────────────────────────────────────────────

void TopologyBuilder::LogTopologySummary() const
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < 3; ++i) total += m_asNodes[i].GetN();

    NS_ASSERT_MSG(total == static_cast<uint32_t>(m_cfg.nodeCount),
                  "Node count mismatch: built=" << total
                  << " expected=" << m_cfg.nodeCount);

    uint32_t totalIntra = 0;
    for (uint32_t i = 0; i < 3; ++i) totalIntra += m_intraEdges[i].size();

    NS_LOG_UNCOND("=== Topology Summary ===");
    NS_LOG_UNCOND("  mode=" << m_cfg.distributionMode
                  << "  total_nodes=" << total
                  << "  (expected=" << m_cfg.nodeCount << ")  OK");

    for (uint32_t i = 0; i < 3; ++i) {
        NS_LOG_UNCOND("  AS" << (i + 1)
                      << ": count=" << m_asNodes[i].GetN()
                      << ", BR0_id=" << m_brNodes[i][0]->GetId()
                      << ", BR1_id=" << m_brNodes[i][1]->GetId()
                      << ", intra_links=" << m_intraEdges[i].size());
    }

    NS_LOG_UNCOND("  inter_links=" << m_interEdges.size()
                  << "  (2x AS1<->AS2, 2x AS2<->AS3, 1x AS1<->AS3)");
    NS_LOG_UNCOND("  total_intra_links=" << totalIntra);
    NS_LOG_UNCOND("========================");
}

// ── DumpTopologyJson ──────────────────────────────────────────────────────────

void TopologyBuilder::DumpTopologyJson(const std::string& path) const
{
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    std::ofstream out(path);
    if (!out.is_open()) {
        NS_LOG_WARN("DumpTopologyJson: cannot open " << path);
        return;
    }

    auto comma = [](bool last) { return last ? "" : ","; };

    out << "{\n";
    out << "  \"nodeCount\": "        << m_cfg.nodeCount        << ",\n";
    out << "  \"distributionMode\": \""<< m_cfg.distributionMode << "\",\n";
    out << "  \"seed\": "             << m_cfg.seed             << ",\n";
    out << "  \"runId\": "            << m_cfg.runId            << ",\n";

    // ── Autonomous systems ──────────────────────────────────────────────
    out << "  \"autonomousSystems\": [\n";
    for (uint32_t i = 0; i < 3; ++i) {
        out << "    {\n";
        out << "      \"asId\": "       << (i + 1)                  << ",\n";
        out << "      \"nodeCount\": "  << m_asNodes[i].GetN()       << ",\n";
        out << "      \"BR_a\": "       << m_brNodes[i][0]->GetId()  << ",\n";
        out << "      \"BR_b\": "       << m_brNodes[i][1]->GetId()  << ",\n";
        out << "      \"nodes\": [";
        for (uint32_t j = 0; j < m_asNodes[i].GetN(); ++j) {
            out << m_asNodes[i].Get(j)->GetId();
            if (j + 1 < m_asNodes[i].GetN()) out << ", ";
        }
        out << "],\n";
        out << "      \"intraLinks\": " << m_intraEdges[i].size() << "\n";
        out << "    }" << comma(i == 2) << "\n";
    }
    out << "  ],\n";

    // ── Inter-AS links ──────────────────────────────────────────────────
    out << "  \"interAsLinks\": [\n";
    for (size_t i = 0; i < m_interEdges.size(); ++i) {
        const auto& l = m_interEdges[i];
        out << "    {\n";
        out << "      \"srcAs\": "    << l.srcAs    << ",\n";
        out << "      \"dstAs\": "    << l.dstAs    << ",\n";
        out << "      \"srcBrIdx\": " << l.srcBrIdx << ",\n";
        out << "      \"dstBrIdx\": " << l.dstBrIdx << ",\n";
        out << "      \"isPrimary\": "<< (l.isPrimary ? "true" : "false") << "\n";
        out << "    }" << comma(i + 1 == m_interEdges.size()) << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    NS_LOG_INFO("Topology JSON written to " << path);
}

} // namespace multias
