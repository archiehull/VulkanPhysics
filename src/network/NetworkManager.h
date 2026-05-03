#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

struct RemotePeer {
    int id = -1;
    std::string ip = "127.0.0.1";
    uint16_t port = 0;

    SOCKET tcpSocket = INVALID_SOCKET;
    sockaddr_in udpAddr{};
    bool isConnected = false;
    std::vector<uint8_t> tcpBuffer;
};

struct PendingConnection {
    SOCKET socket = INVALID_SOCKET;
    std::vector<uint8_t> buffer;
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
    void PushInboundEvent(const std::vector<uint8_t>& data);
    bool PopInboundEvent(std::vector<uint8_t>& outData);

    void SendUDP(const std::vector<uint8_t>& data);
    void SendTCP(const std::vector<uint8_t>& data);

private:
    std::atomic<bool> m_isRunning{ false };
    std::atomic<int> m_localPeerId{ -1 };
    std::atomic<int> m_peerCount{ 0 };

    uint16_t m_localPort{ 0 };
    bool m_debugLogging{ true };

    SOCKET m_udpSocket{ INVALID_SOCKET };
    SOCKET m_tcpSocket{ INVALID_SOCKET };

    std::vector<RemotePeer> m_peers;
    std::vector<PendingConnection> m_pendingConnections;

    std::mutex m_inboundMutex;
    std::queue<std::vector<uint8_t>> m_inboundQueue;

    std::thread m_receiveThread;
    std::thread m_sendThread;

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