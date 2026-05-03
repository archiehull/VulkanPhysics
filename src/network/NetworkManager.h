#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

namespace Network {

struct PeerInfo {
    uint32_t id = 0;
    std::string name;
    std::string ip;
    uint16_t udpPort = 0;
    uint16_t tcpPort = 0;
    uint64_t lastSeenMs = 0;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    bool Start(uint16_t localUdpPort = 9000, uint16_t localTcpPort = 9001);
    void Stop();

    // Send a state batch to all peers (payload is a FlatBuffer byte array)
    void SendStateBatch(const uint8_t* data, size_t size);

    // Send a reliable RPC (over TCP) - placeholder
    void SendSpawnRPC(const uint8_t* data, size_t size);

    // Stats
    uint64_t GetBytesSent() const;
    uint64_t GetBytesReceived() const;

private:
    void RxThreadMain();
    void TxThreadMain();

    std::atomic<bool> m_running{false};
    std::thread m_rxThread;
    std::thread m_txThread;

    // Opaque socket handle to avoid including Winsock in this header
    intptr_t m_udpSock = -1;

    uint16_t m_localUdpPort = 0;
    uint16_t m_localTcpPort = 0;

    std::vector<PeerInfo> m_peers;

    std::atomic<uint64_t> m_bytesSent{0};
    std::atomic<uint64_t> m_bytesReceived{0};
    std::mutex m_peerMutex;

    // TCP listen socket for control RPCs / pings
    intptr_t m_tcpListenSock = -1;
    std::thread m_tcpThread;

    // Debug logging flag
    bool m_debugLogging = true;

private:
    void TcpThreadMain();
public:
    size_t GetPeerCount();
    bool IsRunning() const { return m_running.load(); }
    // Reliable ping over TCP to configured peers. Returns number of successful pongs.
    int PingAllPeers(int timeoutMs = 500);
};

} // namespace Network
