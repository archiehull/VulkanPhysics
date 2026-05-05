#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <glm/glm.hpp>
#include <atomic>
#include <mutex>
#include <queue>
#include <deque>
#include <unordered_map>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

#include "../core/ECS.h"

enum class NetworkEventType {
    SceneLoad,
    SpawnObject,
    DespawnObject,
    RuntimeControl
};

using ReliableEventCallback = std::function<void(NetworkEventType, const std::string&, uint32_t)>;
using PeerJoinedCallback = std::function<void(int)>;
using PeerDisconnectedCallback = std::function<void(int)>;

struct RemoteState {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 velocity;
    float timestamp;
};

struct RemoteHistory {
    std::deque<RemoteState> states;
    static constexpr size_t MAX_HISTORY = 10;
};

struct RemotePeer {
    int id = -1;
    std::string ip = "127.0.0.1";
    uint16_t port = 0;

    SOCKET tcpSocket = INVALID_SOCKET;
    sockaddr_in udpAddr{};
    bool isConnected = false;
    std::vector<uint8_t> tcpBuffer;
    uint32_t lastReceivedSequence = 0;

    // Added to each sender's raw tick_index timestamps so all peers share one timeline
    float timestampOffset = 0.0f;
    bool hasTimestampOffset = false;

    float rttMs = 0.0f;
    std::chrono::steady_clock::time_point lastPingSent;

    uint32_t packetsReceived = 0;
    uint32_t packetsLost = 0;
    float actualPacketLossPct = 0.0f;
};

struct PendingConnection {
    SOCKET socket = INVALID_SOCKET;
    std::vector<uint8_t> buffer;
    bool isOutgoing = false;    // true = we initiated this connect() (client side)
    bool handshakeSent = false; // true = initial handshake already sent
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // Lifecycle
    uint16_t Startup(uint16_t basePort = 27015);
    void Shutdown();
    void Restart();

    // Configuration & State
    bool IsRunning() const { return m_isRunning.load(); }
    int GetPeerCount() const { return m_peerCount.load(); }
    int GetLocalPeerId() const { return m_localPeerId.load(); }
    uint16_t GetLocalPort() const { return m_localPort; }
    void SetDebugLogging(bool enabled) { m_debugLogging = enabled; }

    // Manual Mesh Config (Optional, usually handled by auto-discovery)
    void ConfigurePeer(int peerId, const std::string& ip, uint16_t port);

    // Data Flow
    void PushInboundEvent(const std::vector<uint8_t>& data, bool isUDP = false);
    bool PopInboundEvent(std::vector<uint8_t>& outData);

    void SendUDP(const std::vector<uint8_t>& data);
    void SendTCP(const std::vector<uint8_t>& data);

    void SendTCPTo(int targetPeerId, const std::vector<uint8_t>& data);
    void SendReliableEventTo(int targetPeerId, NetworkEventType type, const std::string& payload, uint32_t targetEntity = 0);

    void SendReliableEvent(NetworkEventType type, const std::string& payload, uint32_t targetEntity = 0);
    void SetReliableEventCallback(ReliableEventCallback callback) { m_eventCallback = callback; }
    void SetPeerJoinedCallback(PeerJoinedCallback callback) { m_peerJoinedCallback = callback; }
    void SetPeerDisconnectedCallback(PeerDisconnectedCallback callback) { m_peerDisconnectedCallback = callback; }

    void BroadcastState(Registry& registry, const std::vector<Entity>& locallyOwnedEntities);
    void BroadcastSingleEntity(Registry& registry, Entity entity);
    void ProcessInboundPackets(Registry& registry);
    void UpdateInterpolation(Registry& registry, float dt);

    // Seeds the remote history for a newly spawned entity so interpolation starts immediately.
    // normalizedSpawnTs: the spawning peer's broadcast timestamp after applying the peer offset.
    // Pass -1 (default) to fall back to the cursor-based heuristic.
    void SeedRemoteState(Entity id, glm::vec3 pos, glm::vec3 vel, glm::vec3 rot, float normalizedSpawnTs = -1.0f);

    // Returns the raw broadcast timestamp for the local peer (seconds since NetworkManager started).
    float GetCurrentBroadcastTimestamp() const {
        return m_currentSimulationTime;
    }

    // Returns the timestamp offset applied to packets from the given peer, and whether it is calibrated.
    float GetPeerTimestampOffset(int peerId) const {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        if (peerId >= 0 && peerId < static_cast<int>(m_peers.size()))
            return m_peers[peerId].timestampOffset;
        return 0.0f;
    }
    bool PeerHasTimestampOffset(int peerId) const {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        if (peerId >= 0 && peerId < static_cast<int>(m_peers.size()))
            return m_peers[peerId].hasTimestampOffset;
        return false;
    }

    // Telemetry accessors (approximate, not strictly thread-safe — for debug display only)
    float GetPlaybackTime() const { return m_playbackTime; }
    float GetInterpolationDelay() const { return m_interpolationDelay; }
    void SetInterpolationDelay(float delaySec) { m_interpolationDelay = delaySec; }
    int GetRemoteEntityCount();
    float GetLatestRemoteTimestamp();

    struct PeerStatus {
        bool connected = false;
        uint16_t port = 0;
        std::string ip;
        float pingMs = 0.0f;        
        float packetLossPct = 0.0f;
    };
    PeerStatus GetPeerStatus(int peerId);

    void ClearHistory();
    void ClearHistoryForEntity(Entity entity);

    void SetSimulationConditions(float latencyMs, float jitterMs, float packetLossPct);
    void SetLocalPeerId(int id);
    void ReconfigurePeer(int peerId, const std::string& ip, uint16_t port);

    float GetSimulatedLatency() const { return m_simulatedLatencyMs; }
    float GetSimulatedPacketLoss() const { return m_simulatedPacketLoss; }

    void AdvanceSimulationTime(float dt) {
        m_currentSimulationTime += dt;
    }

private:
    struct DelayedPacket {
        std::chrono::steady_clock::time_point deliveryTime;
        std::vector<uint8_t> data;
        bool isUDP = false;
    };
    std::vector<DelayedPacket> m_delayedInboundQueue;

    float m_simulatedLatencyMs = 0.0f;
    float m_simulatedPacketLoss = 0.0f;
    float m_simulatedJitterMs = 0.0f;

    std::atomic<bool> m_isRunning{ false };
    std::atomic<int> m_localPeerId{ -1 };
    std::atomic<int> m_peerCount{ 0 };

    uint16_t m_basePort{ 27015 };
    uint16_t m_localPort{ 0 };

    bool m_debugLogging{ true };
    bool m_interpLogging{ false };
    bool m_spawnLogging{ false };



    SOCKET m_udpSocket{ INVALID_SOCKET };
    SOCKET m_tcpSocket{ INVALID_SOCKET };

    PeerJoinedCallback m_peerJoinedCallback = nullptr;
    PeerDisconnectedCallback m_peerDisconnectedCallback = nullptr;

    std::vector<RemotePeer> m_peers;
    std::vector<PendingConnection> m_pendingConnections;
    mutable std::mutex m_peerMutex;

    std::mutex m_inboundMutex;
    std::queue<std::vector<uint8_t>> m_inboundQueue;

    std::mutex m_outboundMutex;
    std::queue<std::vector<uint8_t>> m_outboundQueue;

    std::thread m_receiveThread;
    std::thread m_sendThread;

    ReliableEventCallback m_eventCallback;

    // Interpolation State
    std::unordered_map<uint32_t, RemoteHistory> m_remoteHistories;
    std::mutex m_historyMutex;
    float m_playbackTime = 0.0f;
    float m_interpolationDelay = 0.25f;

    // Shared broadcast sequence counter and start time for all outgoing UDP snapshots
    uint32_t m_broadcastSequence = 0;
    float m_currentSimulationTime = 0.0f;

    // Connection retry state — member so Restart() resets it correctly
    std::chrono::steady_clock::time_point m_lastConnectionAttempt{};
    std::array<std::chrono::steady_clock::time_point, 4> m_lastP2PAttempt{};

    // Internal Logic
    void ReceiveLoop();
    void SendLoop();
    void AcceptIncomingConnections();
    void MaintainOutgoingConnections();
    void ReceiveTCP();
    void ReceiveUDP();

    void SendHandshake(SOCKET targetSocket);
    void HandleHandshake(SOCKET socket, int senderId);
    int GetNextAvailableId();
};