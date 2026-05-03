#include "NetworkManager.h"
#include "NetworkSchema_generated.h" 
#include "../systems/PhysicsSystem.h"

using namespace VulkanPhysics::Network;

NetworkManager::NetworkManager() {
    m_peers.resize(4);
    for (int i = 0; i < 4; ++i) {
        m_peers[i].id = i;
    }
}

NetworkManager::~NetworkManager() {
    Shutdown();
}

void NetworkManager::ConfigurePeer(int peerId, const std::string& ip, uint16_t port) {
    if (peerId >= 0 && peerId < 4) {
        m_peers[peerId].ip = ip;
        m_peers[peerId].port = port;

        m_peers[peerId].udpAddr.sin_family = AF_INET;
        m_peers[peerId].udpAddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &m_peers[peerId].udpAddr.sin_addr);
    }
}

void NetworkManager::Restart() {
    Shutdown();
    Startup(m_localPort);
}

int NetworkManager::GetNextAvailableId() {
    // Peer 0 is always the host. Check which slots 1-3 are not connected.
    for (int i = 1; i < 4; ++i) {
        if (!m_peers[i].isConnected) return i;
    }
    return -1;
}

uint16_t NetworkManager::Startup(uint16_t basePort) {
    if (m_isRunning.load()) return m_localPort;

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    m_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    m_tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;

    // --- Auto-Port Discovery ---
    bool bound = false;
    for (uint16_t p = basePort; p < basePort + 10; ++p) {
        localAddr.sin_port = htons(p);
        if (bind(m_udpSocket, (sockaddr*)&localAddr, sizeof(localAddr)) != SOCKET_ERROR) {
            if (bind(m_tcpSocket, (sockaddr*)&localAddr, sizeof(localAddr)) != SOCKET_ERROR) {
                m_localPort = p;
                bound = true;
                break;
            }
        }
    }

    if (!bound) return 0;

    u_long mode = 1;
    ioctlsocket(m_udpSocket, FIONBIO, &mode);
    ioctlsocket(m_tcpSocket, FIONBIO, &mode);
    listen(m_tcpSocket, SOMAXCONN);

    // Anchor Logic: The first instance to grab basePort becomes Peer 0
    if (m_localPort == basePort) {
        m_localPeerId = 0;
        PhysicsSystem::localPeerId = 0;
        if (m_debugLogging) std::cout << "[NetworkManager] Bound to base port. Identifying as Peer 0 (Host).\n";
    }
    else {
        m_localPeerId = -1; // Request assignment from Peer 0
        if (m_debugLogging) std::cout << "[NetworkManager] Bound to port " << m_localPort << ". Searching for Host...\n";
    }

    m_isRunning.store(true);
    m_receiveThread = std::thread(&NetworkManager::ReceiveLoop, this);
    SetThreadAffinityMask(m_receiveThread.native_handle(), (static_cast<DWORD_PTR>(1) << 2));

    m_sendThread = std::thread(&NetworkManager::SendLoop, this);
    SetThreadAffinityMask(m_sendThread.native_handle(), (static_cast<DWORD_PTR>(1) << 3));

    return m_localPort;
}

void NetworkManager::Shutdown() {
    m_isRunning.store(false);
    if (m_receiveThread.joinable()) m_receiveThread.join();
    if (m_sendThread.joinable()) m_sendThread.join();

    for (auto& p : m_peers) {
        if (p.tcpSocket != INVALID_SOCKET) {
            closesocket(p.tcpSocket);
            p.tcpSocket = INVALID_SOCKET;
        }
        p.isConnected = false;
        p.tcpBuffer.clear();
    }

    for (auto& pending : m_pendingConnections) {
        if (pending.socket != INVALID_SOCKET) closesocket(pending.socket);
    }
    m_pendingConnections.clear();

    if (m_udpSocket != INVALID_SOCKET) closesocket(m_udpSocket);
    if (m_tcpSocket != INVALID_SOCKET) closesocket(m_tcpSocket);
    WSACleanup();
}

void NetworkManager::ReceiveLoop() {
    while (m_isRunning.load()) {
        AcceptIncomingConnections();
        ReceiveUDP();
        ReceiveTCP();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void NetworkManager::SendLoop() {
    while (m_isRunning.load()) {
        MaintainOutgoingConnections();
        // Step 9 will implement the outbound queue processing here[cite: 2]
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void NetworkManager::AcceptIncomingConnections() {
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    SOCKET s = accept(m_tcpSocket, (sockaddr*)&addr, &addrLen);
    if (s != INVALID_SOCKET) {
        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);
        m_pendingConnections.push_back({ s, {} });
        if (m_debugLogging) std::cout << "[NetworkManager] Accepted incoming TCP connection.\n";
    }
}

void NetworkManager::ReceiveTCP() {
    char buffer[4096];

    // 1. Process Pending Handshakes (Wait for ID assignment or normal handshake)
    for (auto it = m_pendingConnections.begin(); it != m_pendingConnections.end(); ) {
        int bytes = recv(it->socket, buffer, sizeof(buffer), 0);
        if (bytes > 0) {
            it->buffer.insert(it->buffer.end(), buffer, buffer + bytes);

            // Check if we have at least the 4-byte size prefix
            if (it->buffer.size() >= 4) {
                uint32_t packetSize;
                memcpy(&packetSize, it->buffer.data(), 4);

                // Check if the full packet has arrived
                if (it->buffer.size() >= 4 + packetSize) {
                    auto msg = GetNetworkMessage(it->buffer.data() + 4);

                    // --- CLIENT LOGIC: Handle ID Assignment from Host ---
                    if (msg->payload_type() == Payload_ReliableEvent) {
                        auto ev = msg->payload_as_ReliableEvent();
                        if (ev->event_type() == EventType_IdAssignment) {
                            int assignedId = std::stoi(ev->string_payload()->str());
                            m_localPeerId = assignedId;
                            PhysicsSystem::localPeerId = assignedId; // Update ECS Authority[cite: 1]

                            // Link this socket as our connection to the Host (Peer 0)
                            m_peers[0].tcpSocket = it->socket;
                            m_peers[0].isConnected = true;
                            m_peerCount++;

                            if (m_debugLogging) std::cout << "[NetworkManager] Successfully joined as Peer " << assignedId << "\n";

                            it = m_pendingConnections.erase(it);
                            continue;
                        }
                    }

                    // --- HOST LOGIC: Process handshake from new client ---
                    if (m_localPeerId == 0) {
                        HandleHandshake(it->socket, msg->sender_peer_id());
                    }

                    it = m_pendingConnections.erase(it);
                    continue;
                }
            }
        }
        else if (bytes == 0 || (bytes == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
            if (m_debugLogging) {
                std::cout << "[NetworkManager] Pending connection closed (Mesh full or Host disconnected).\n";
            }
            closesocket(it->socket);
            it = m_pendingConnections.erase(it);
            continue;
        }
        ++it;
    }

    // 2. Process Established Peers (Standard reliable traffic)
    for (auto& peer : m_peers) {
        if (!peer.isConnected || peer.id == m_localPeerId) continue;

        int bytes = recv(peer.tcpSocket, buffer, sizeof(buffer), 0);
        if (bytes > 0) {
            peer.tcpBuffer.insert(peer.tcpBuffer.end(), buffer, buffer + bytes);
            while (peer.tcpBuffer.size() >= 4) {
                uint32_t packetSize;
                memcpy(&packetSize, peer.tcpBuffer.data(), 4);
                if (peer.tcpBuffer.size() < 4 + packetSize) break;

                std::vector<uint8_t> packetData(peer.tcpBuffer.begin() + 4, peer.tcpBuffer.begin() + 4 + packetSize);
                PushInboundEvent(packetData);
                peer.tcpBuffer.erase(peer.tcpBuffer.begin(), peer.tcpBuffer.begin() + 4 + packetSize);
            }
        }
        else if (bytes == 0 || (bytes == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
            if (m_debugLogging) std::cout << "[NetworkManager] Peer " << peer.id << " disconnected.\n";
            peer.isConnected = false;
            closesocket(peer.tcpSocket);
            peer.tcpSocket = INVALID_SOCKET;
            m_peerCount--;
        }
    }
}

void NetworkManager::ReceiveUDP() {
    char buffer[2048];
    sockaddr_in from;
    int fromLen = sizeof(from);
    while (true) {
        int bytes = recvfrom(m_udpSocket, buffer, sizeof(buffer), 0, (sockaddr*)&from, &fromLen);
        if (bytes <= 0) break;
        PushInboundEvent(std::vector<uint8_t>(buffer, buffer + bytes));
    }
}

void NetworkManager::MaintainOutgoingConnections() {
    // Only search for anchor if we don't have an ID and aren't already waiting on a connection
    if (m_localPeerId == -1 && m_pendingConnections.empty() && m_peerCount < 3) { // 4 PEER CAP
        static auto lastAttempt = std::chrono::steady_clock::now();
        if (std::chrono::steady_clock::now() - lastAttempt > std::chrono::seconds(2)) {
            lastAttempt = std::chrono::steady_clock::now();

            if (m_debugLogging) std::cout << "[NetworkManager] Searching for Anchor (Peer 0) on port 27015...\n";

            sockaddr_in anchorAddr{};
            anchorAddr.sin_family = AF_INET;
            anchorAddr.sin_port = htons(27015);
            inet_pton(AF_INET, "127.0.0.1", &anchorAddr.sin_addr);

            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) return;

            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);

            // Connect is non-blocking
            connect(s, (sockaddr*)&anchorAddr, sizeof(anchorAddr));

            // Add to pending so we don't trigger this 'if' block again next frame
            m_pendingConnections.push_back({ s, {} });

            // Send our initial handshake identifying as -1
            SendHandshake(s);
        }
    }
}

void NetworkManager::HandleHandshake(SOCKET s, int remoteId) {
    if (m_localPeerId == 0 && remoteId == -1) {
        int newId = GetNextAvailableId();
        if (newId != -1) {
            flatbuffers::FlatBufferBuilder b(128);

            // Create the string ID first
            auto idStr = b.CreateString(std::to_string(newId));

            // Create the event using the string
            auto ev = CreateReliableEvent(b, EventType_IdAssignment, idStr);

            auto msg = CreateNetworkMessage(b, m_localPeerId, Payload_ReliableEvent, ev.Union());
            b.Finish(msg);

            uint32_t size = b.GetSize();
            send(s, (char*)&size, 4, 0);
            send(s, (char*)b.GetBufferPointer(), size, 0);

            m_peers[newId].tcpSocket = s;
            m_peers[newId].isConnected = true;
            m_peerCount++;
            if (m_debugLogging) std::cout << "[NetworkManager] Peer " << newId << " joined the mesh.\n";
        }
        else {
            if (m_debugLogging) std::cout << "[NetworkManager] Mesh is full (4/4). Rejecting connection.\n";
            closesocket(s);
        }
    }
    else if (remoteId >= 0 && remoteId < 4) {
        m_peers[remoteId].tcpSocket = s;
        m_peers[remoteId].isConnected = true;
        m_peerCount++;
    }
}

void NetworkManager::SendHandshake(SOCKET s) {
    flatbuffers::FlatBufferBuilder b(128);
    auto ev = CreateReliableEventDirect(b, EventType_SceneLoad, "HANDSHAKE");
    auto msg = CreateNetworkMessage(b, m_localPeerId, Payload_ReliableEvent, ev.Union());
    b.Finish(msg);

    uint32_t size = b.GetSize();
    send(s, (char*)&size, 4, 0);
    send(s, (char*)b.GetBufferPointer(), size, 0);
}

void NetworkManager::PushInboundEvent(const std::vector<uint8_t>& d) {
    std::lock_guard<std::mutex> lock(m_inboundMutex);
    m_inboundQueue.push(d);
}

bool NetworkManager::PopInboundEvent(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(m_inboundMutex);
    if (m_inboundQueue.empty()) return false;
    out = std::move(m_inboundQueue.front());
    m_inboundQueue.pop();
    return true;
}

void NetworkManager::SendUDP(const std::vector<uint8_t>& d) {
    for (auto& p : m_peers) {
        if (p.isConnected && p.id != m_localPeerId) {
            sendto(m_udpSocket, (char*)d.data(), (int)d.size(), 0, (sockaddr*)&p.udpAddr, sizeof(p.udpAddr));
        }
    }
}

void NetworkManager::SendTCP(const std::vector<uint8_t>& d) {
    uint32_t size = (uint32_t)d.size();
    for (auto& p : m_peers) {
        if (p.isConnected && p.tcpSocket != INVALID_SOCKET && p.id != m_localPeerId) {
            send(p.tcpSocket, (char*)&size, 4, 0);
            send(p.tcpSocket, (char*)d.data(), size, 0);
        }
    }
}