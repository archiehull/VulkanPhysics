#include "NetworkManager.h"
#include "PacketDefs.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>

namespace Network {

static uint64_t GetTimeMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

NetworkManager::NetworkManager() {}

NetworkManager::~NetworkManager() {
    Stop();
}

bool NetworkManager::Start(uint16_t localUdpPort, uint16_t localTcpPort) {
    if (m_running.load()) return false;

    m_localUdpPort = localUdpPort;
    m_localTcpPort = localTcpPort;

    if (m_debugLogging) std::cout << "[NET] Starting NetworkManager on UDP:" << m_localUdpPort << " TCP:" << m_localTcpPort << std::endl;

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        if (m_debugLogging) std::cerr << "[NET] WSAStartup failed" << std::endl;
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        if (m_debugLogging) std::cerr << "[NET] Failed to create UDP socket" << std::endl;
        WSACleanup();
        return false;
    }
    m_udpSock = static_cast<intptr_t>(s);

    // Non-blocking mode
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(m_udpSock), FIONBIO, &mode);

    // Allow socket address reuse (helps with restart / TIME_WAIT state)
    int reuse = 1;
    setsockopt(static_cast<SOCKET>(m_udpSock), SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_localUdpPort);

    if (bind(static_cast<SOCKET>(m_udpSock), (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        if (m_debugLogging) std::cerr << "[NET] UDP bind failed: " << WSAGetLastError() << std::endl;
        closesocket(static_cast<SOCKET>(m_udpSock));
        WSACleanup();
        return false;
    }
    if (m_debugLogging) std::cout << "[NET] UDP socket bound successfully" << std::endl;

    m_running.store(true);

    // Setup TCP listen socket for RPCs/pings
    SOCKET ts = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ts == INVALID_SOCKET) {
        if (m_debugLogging) std::cerr << "[NET] Failed to create TCP listen socket" << std::endl;
        closesocket(static_cast<SOCKET>(m_udpSock));
        WSACleanup();
        return false;
    }
    m_tcpListenSock = static_cast<intptr_t>(ts);

    // Allow reuse
    int opt = 1;
    setsockopt(static_cast<SOCKET>(m_tcpListenSock), SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in taddr{};
    taddr.sin_family = AF_INET;
    taddr.sin_addr.s_addr = INADDR_ANY;
    taddr.sin_port = htons(m_localTcpPort);

    if (bind(static_cast<SOCKET>(m_tcpListenSock), (sockaddr*)&taddr, sizeof(taddr)) == SOCKET_ERROR) {
        if (m_debugLogging) std::cerr << "[NET] TCP bind failed: " << WSAGetLastError() << std::endl;
        closesocket(static_cast<SOCKET>(m_tcpListenSock));
        closesocket(static_cast<SOCKET>(m_udpSock));
        WSACleanup();
        return false;
    }

    if (listen(static_cast<SOCKET>(m_tcpListenSock), SOMAXCONN) == SOCKET_ERROR) {
        if (m_debugLogging) std::cerr << "[NET] TCP listen failed: " << WSAGetLastError() << std::endl;
        closesocket(static_cast<SOCKET>(m_tcpListenSock));
        closesocket(static_cast<SOCKET>(m_udpSock));
        WSACleanup();
        return false;
    }
    if (m_debugLogging) std::cout << "[NET] TCP listen socket ready" << std::endl;

    // Add a loopback test peer for debugging
    {
        std::lock_guard<std::mutex> lk(m_peerMutex);
        PeerInfo testPeer;
        testPeer.id = 1;
        testPeer.name = "Loopback";
        testPeer.ip = "127.0.0.1";
        testPeer.udpPort = m_localUdpPort;
        testPeer.tcpPort = m_localTcpPort;
        m_peers.push_back(testPeer);
    }
    if (m_debugLogging) std::cout << "[NET] Added loopback test peer (127.0.0.1:" << m_localTcpPort << ")" << std::endl;

    // Start threads
    m_rxThread = std::thread(&NetworkManager::RxThreadMain, this);
    m_txThread = std::thread(&NetworkManager::TxThreadMain, this);
    m_tcpThread = std::thread(&NetworkManager::TcpThreadMain, this);
    if (m_debugLogging) std::cout << "[NET] Network threads started" << std::endl;

    return true;
}

void NetworkManager::Stop() {
    if (!m_running.load()) return;
    
    if (m_debugLogging) std::cout << "[NET] Stopping NetworkManager..." << std::endl;
    m_running.store(false);

    if (m_rxThread.joinable()) m_rxThread.join();
    if (m_txThread.joinable()) m_txThread.join();
    if (m_tcpThread.joinable()) m_tcpThread.join();

    if (m_udpSock != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(m_udpSock));
        m_udpSock = INVALID_SOCKET;
    }
    if (m_tcpListenSock != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(m_tcpListenSock));
        m_tcpListenSock = INVALID_SOCKET;
    }
    WSACleanup();
    if (m_debugLogging) std::cout << "[NET] NetworkManager stopped" << std::endl;
}

void NetworkManager::SendStateBatch(const uint8_t* data, size_t size) {
    // Placeholder: broadcast to peers via UDP
    for (const auto& p : m_peers) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(p.udpPort);
        inet_pton(AF_INET, p.ip.c_str(), &dest.sin_addr);

        // Prepend our header
        NetHeader hdr{};
        hdr.type = PT_STATE_UDP;
        hdr.sender_id = 0; // TODO: set local peer id
        hdr.seq = 0; // TODO: sequence
        hdr.timestamp = GetTimeMs();

        std::vector<char> packet(sizeof(hdr) + size);
        memcpy(packet.data(), &hdr, sizeof(hdr));
        memcpy(packet.data() + sizeof(hdr), data, size);

        int sent = sendto(static_cast<SOCKET>(m_udpSock), packet.data(), (int)packet.size(), 0, (sockaddr*)&dest, sizeof(dest));
        if (sent > 0) m_bytesSent += sent;
    }
}

void NetworkManager::SendSpawnRPC(const uint8_t* /*data*/, size_t /*size*/) {
    // Placeholder: send RPC over TCP (not implemented yet)
}

uint64_t NetworkManager::GetBytesSent() const { return m_bytesSent.load(); }
uint64_t NetworkManager::GetBytesReceived() const { return m_bytesReceived.load(); }

size_t NetworkManager::GetPeerCount() {
    std::lock_guard<std::mutex> lk(m_peerMutex);
    return m_peers.size();
}

void NetworkManager::RxThreadMain() {
    // Pin thread affinity here if desired (caller must call SetThreadAffinityMask)
    char buf[65536];
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (m_running.load()) {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(m_udpSock, &readset);
        timeval tv{0, 20000}; // 20ms
        int n = select(0, &readset, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(static_cast<SOCKET>(m_udpSock), &readset)) {
            int len = recvfrom(static_cast<SOCKET>(m_udpSock), buf, (int)sizeof(buf), 0, (sockaddr*)&from, &fromLen);
            if (len > 0) {
                m_bytesReceived += len;
                // Minimal processing: inspect header
                if (len >= (int)sizeof(NetHeader)) {
                    NetHeader hdr;
                    memcpy(&hdr, buf, sizeof(hdr));
                    // For now just print packet type
                    // std::cout << "Got packet type " << int(hdr.type) << " len=" << len << std::endl;
                }
            }
        }
    }
}

void NetworkManager::TxThreadMain() {
    // placeholder for batching outgoing state packets, retransmits, etc.
    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int NetworkManager::PingAllPeers(int timeoutMs) {
    int success = 0;
    std::lock_guard<std::mutex> lk(m_peerMutex);
    
    if (m_debugLogging) std::cout << "[NET] PingAllPeers: checking " << m_peers.size() << " peers (timeout " << timeoutMs << "ms)" << std::endl;
    
    for (const auto& p : m_peers) {
        if (m_debugLogging) std::cout << "[NET] Pinging " << p.name << " at " << p.ip << ":" << p.tcpPort << std::endl;
        
        // Create TCP socket
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) {
            if (m_debugLogging) std::cerr << "[NET] Failed to create ping socket to " << p.name << std::endl;
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(p.tcpPort);
        inet_pton(AF_INET, p.ip.c_str(), &addr.sin_addr);

        // set non-blocking connect with timeout
        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);

        int res = connect(s, (sockaddr*)&addr, sizeof(addr));
        if (res == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                if (m_debugLogging) std::cerr << "[NET] Connect failed to " << p.name << ": " << err << std::endl;
                closesocket(s);
                continue;
            }

            // wait for writable or timeout
            fd_set wf;
            FD_ZERO(&wf);
            FD_SET(s, &wf);
            timeval tv{ static_cast<long>(timeoutMs/1000), static_cast<long>((timeoutMs%1000)*1000) };
            int sel = select(0, nullptr, &wf, nullptr, &tv);
            if (sel <= 0 || !FD_ISSET(s, &wf)) {
                if (m_debugLogging) std::cerr << "[NET] Connect timeout to " << p.name << std::endl;
                closesocket(s);
                continue;
            }
        }

        if (m_debugLogging) std::cout << "[NET] Connected to " << p.name << ", sending ping..." << std::endl;
        
        // Connected — send a small ping payload (header + "PING")
        NetHeader hdr{};
        hdr.type = PT_HELLO;
        hdr.sender_id = 0;
        hdr.seq = 0;
        hdr.timestamp = GetTimeMs();

        const char payload[] = "PING";
        std::vector<char> packet(sizeof(hdr) + sizeof(payload));
        memcpy(packet.data(), &hdr, sizeof(hdr));
        memcpy(packet.data() + sizeof(hdr), payload, sizeof(payload));

        int sent = send(s, packet.data(), (int)packet.size(), 0);
        if (sent > 0) {
            m_bytesSent += sent;
            if (m_debugLogging) std::cout << "[NET] Sent " << sent << " bytes to " << p.name << std::endl;
        } else {
            if (m_debugLogging) std::cerr << "[NET] Send failed to " << p.name << std::endl;
            closesocket(s);
            continue;
        }

        // Wait for reply
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        timeval tv2{ static_cast<long>(timeoutMs/1000), static_cast<long>((timeoutMs%1000)*1000) };
        int sel2 = select(0, &rf, nullptr, nullptr, &tv2);
        if (sel2 > 0 && FD_ISSET(s, &rf)) {
            char buf[256];
            int r = recv(s, buf, (int)sizeof(buf), 0);
            if (r > 0) {
                m_bytesReceived += r;
                if (m_debugLogging) std::cout << "[NET] Received " << r << " bytes from " << p.name << std::endl;
                if (r >= (int)sizeof(NetHeader)) {
                    NetHeader resp;
                    memcpy(&resp, buf, sizeof(resp));
                    if (resp.type == PT_HELLO_ACK) {
                        if (m_debugLogging) std::cout << "[NET] GOT PONG from " << p.name << "!" << std::endl;
                        success++;
                    } else {
                        if (m_debugLogging) std::cout << "[NET] Unexpected response type from " << p.name << ": " << (int)resp.type << std::endl;
                    }
                }
            } else {
                if (m_debugLogging) std::cerr << "[NET] Recv failed from " << p.name << std::endl;
            }
        } else {
            if (m_debugLogging) std::cerr << "[NET] No response from " << p.name << " (timeout)" << std::endl;
        }

        closesocket(s);
    }

    if (m_debugLogging) std::cout << "[NET] PingAllPeers complete: " << success << "/" << m_peers.size() << " replies" << std::endl;
    return success;
}


void NetworkManager::TcpThreadMain() {
    // Accept loop: respond to PT_HELLO with PT_HELLO_ACK
    if (m_debugLogging) std::cout << "[NET] TcpThreadMain started (accept loop active)" << std::endl;
    SOCKET listenSock = static_cast<SOCKET>(m_tcpListenSock);
    fd_set rf;

    while (m_running.load()) {
        FD_ZERO(&rf);
        FD_SET(listenSock, &rf);
        timeval tv{0, 200000}; // 200ms
        int n = select(0, &rf, nullptr, nullptr, &tv);
        if (n > 0 && FD_ISSET(listenSock, &rf)) {
            sockaddr_in from{};
            int fromLen = sizeof(from);
            SOCKET client = accept(listenSock, (sockaddr*)&from, &fromLen);
            if (client != INVALID_SOCKET) {
                char fromIp[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, fromIp, INET_ADDRSTRLEN);
                if (m_debugLogging) std::cout << "[NET] Accepted connection from " << fromIp << ":" << ntohs(from.sin_port) << std::endl;
                
                // Read small incoming message
                char buf[1024];
                int r = recv(client, buf, (int)sizeof(buf), 0);
                if (r > 0) {
                    m_bytesReceived += r;
                    if (m_debugLogging) std::cout << "[NET] Received " << r << " bytes from " << fromIp << std::endl;
                    
                    if (r >= (int)sizeof(NetHeader)) {
                        NetHeader hdr;
                        memcpy(&hdr, buf, sizeof(hdr));
                        if (m_debugLogging) std::cout << "[NET] Packet type: " << (int)hdr.type << std::endl;
                        
                        if (hdr.type == PT_HELLO) {
                            if (m_debugLogging) std::cout << "[NET] Got PING, sending PONG..." << std::endl;
                            // send ack
                            NetHeader ack{};
                            ack.type = PT_HELLO_ACK;
                            ack.sender_id = 0;
                            ack.seq = hdr.seq;
                            ack.timestamp = GetTimeMs();

                            const char pong[] = "PONG";
                            std::vector<char> packet(sizeof(ack) + sizeof(pong));
                            memcpy(packet.data(), &ack, sizeof(ack));
                            memcpy(packet.data() + sizeof(ack), pong, sizeof(pong));
                            int s = send(client, packet.data(), (int)packet.size(), 0);
                            if (s > 0) {
                                m_bytesSent += s;
                                if (m_debugLogging) std::cout << "[NET] Sent PONG (" << s << " bytes)" << std::endl;
                            } else {
                                if (m_debugLogging) std::cerr << "[NET] Send PONG failed" << std::endl;
                            }
                        }
                    }
                } else {
                    if (m_debugLogging) std::cerr << "[NET] Recv failed on accepted connection" << std::endl;
                }
                // close client
                closesocket(client);
            }
        }
    }
    if (m_debugLogging) std::cout << "[NET] TcpThreadMain exiting" << std::endl;
}

} // namespace Network
