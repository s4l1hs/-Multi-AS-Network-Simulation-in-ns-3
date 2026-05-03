#pragma once

#include "scenario_config.h"

#include "ns3/application-container.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <vector>

namespace ns3 {
class Node;
class UniformRandomVariable;
} // namespace ns3

namespace multias {

class TopologyBuilder;

class TrafficGenerator {
public:
    explicit TrafficGenerator(const ScenarioConfig& cfg);

    // Install OnOff (source) + PacketSink (destination) pairs for all
    // inter-AS flow pairs. Call AFTER OspfLikeRouter::Install() so that
    // routing tables are ready when the simulation begins.
    //
    // Flow matrix (base, always present):
    //   AS1→AS2, AS2→AS3, AS1→AS3, AS3→AS1
    // Extra flows (cfg.nodeCount / 10 additional, cycling through directions):
    //   AS2→AS1, AS3→AS2, and the base directions again.
    //
    // Applications start at t=2s (after topology stabilises) and stop at
    // t=cfg.simTime-1s. Returns the combined ApplicationContainer.
    struct FlowInfo {
        uint32_t srcAs, dstAs;
        uint32_t srcNodeId, dstNodeId;
        uint16_t port;
    };

    ns3::ApplicationContainer InstallFlows(const TopologyBuilder& topo,
                                           const ScenarioConfig&  cfg);

    // Returns metadata for all installed flows (available after InstallFlows).
    const std::vector<FlowInfo>& GetFlows() const { return m_flows; }

private:

    // Returns a non-BR node from the given AS (index 2..N-1).
    // Falls back to BR nodes if the AS has exactly 2 nodes.
    ns3::Ptr<ns3::Node> PickNonBr(const TopologyBuilder&              topo,
                                   uint32_t                            asId,
                                   ns3::Ptr<ns3::UniformRandomVariable> rng) const;

    // Returns the IP address of the first non-loopback interface on `node`.
    ns3::Ipv4Address GetNodeFirstIp(ns3::Ptr<ns3::Node> node) const;

    ScenarioConfig        m_cfg;
    std::vector<FlowInfo> m_flows;
};

} // namespace multias
