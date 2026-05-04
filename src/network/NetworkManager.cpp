#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "NetworkManager.h"
#include "NetworkSchema_generated.h" 
#include "../systems/PhysicsSystem.h"
#include "../core/Components.h"

// Helper to ensure all bytes are pushed through a non-blocking TCP socket
bool SendAllTCP(SOCKET s, const char* data, int length) {
    int totalSent = 0;
    while (totalSent < length) {
        int sent = send(s, data + totalSent, length - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                // Buffer full, yield briefly and try again
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false; // Fatal socket error, connection dropped
        }
        totalSent += sent;
    }
    return true;
}

using namespace VulkanPhysics::Network;

NetworkManager::NetworkManager() {
    m_peers.resize(4);
    for (int i = 0; i < 4; ++i) {
        m_peers[i].id = i;
    }
    // Push the timestamp back so the first reconnect attempt fires immediately
    m_lastConnectionAttempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    for (auto& attempt : m_lastP2PAttempt) {
        attempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    }
}

NetworkManager::~NetworkManager() {
    Shutdown();
}

void NetworkManager::ConfigurePeer(int peerId, const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lock(m_peerMutex);
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
    std::lock_guard<std::mutex> lock(m_peerMutex);
    // Peer 0 is always the host. Check which slots 1-3 are not connected.
    for (int i = 1; i < 4; ++i) {
        if (!m_peers[i].isConnected) return i;
    }
    return -1;
}

uint16_t NetworkManager::Startup(uint16_t basePort) {
    if (m_isRunning.load()) return m_localPort;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 0;
    }

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;

    // --- Auto-Port Discovery ---
    // Sockets are recreated on each attempt so a successful UDP bind on port p followed
    // by a failed TCP bind does not leave the UDP socket stuck on p, blocking all further
    // iterations from binding UDP to any other port.
    bool bound = false;
    for (uint16_t p = basePort; p < basePort + 10; ++p) {
        if (m_udpSocket != INVALID_SOCKET) { closesocket(m_udpSocket); }
        if (m_tcpSocket != INVALID_SOCKET) { closesocket(m_tcpSocket); }

        m_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        m_tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

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

    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        for (auto& p : m_peers) {
            if (p.tcpSocket != INVALID_SOCKET) {
                closesocket(p.tcpSocket);
                p.tcpSocket = INVALID_SOCKET;
            }
            p.isConnected = false;
            p.tcpBuffer.clear();
        }
    }

    for (auto& pending : m_pendingConnections) {
        if (pending.socket != INVALID_SOCKET) closesocket(pending.socket);
    }
    m_pendingConnections.clear();

    if (m_udpSocket != INVALID_SOCKET) closesocket(m_udpSocket);
    if (m_tcpSocket != INVALID_SOCKET) closesocket(m_tcpSocket);
    WSACleanup();

    // Reset retry timer so a subsequent Startup() (e.g. from Restart()) attempts immediately
    m_lastConnectionAttempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    for (auto& attempt : m_lastP2PAttempt) {
        attempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    }
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
    auto lastPingTime = std::chrono::steady_clock::now();

    while (m_isRunning.load()) {
        MaintainOutgoingConnections();

        auto now = std::chrono::steady_clock::now();
        if (now - lastPingTime > std::chrono::seconds(1)) {
            lastPingTime = now;

            for (int i = 0; i < 4; ++i) {
                if (i == m_localPeerId.load()) continue;

                bool shouldPing = false;
                // 1. Lock briefly just to check status and set the timestamp
                {
                    std::lock_guard<std::mutex> lock(m_peerMutex);
                    if (m_peers[i].isConnected) {
                        m_peers[i].lastPingSent = now;
                        shouldPing = true;
                    }
                } // Mutex unlocks here

                // 2. Send the event safely OUTSIDE the lock
                if (shouldPing) {
                    SendReliableEventTo(i, NetworkEventType::SceneLoad, "__PING__", 0);
                }
            }
        }

        // Step 9: Outbound queue processing
        std::vector<uint8_t> packet;
        bool hasPacket = false;

        {
            std::lock_guard<std::mutex> lock(m_outboundMutex);
            if (!m_outboundQueue.empty()) {
                packet = std::move(m_outboundQueue.front());
                m_outboundQueue.pop();
                hasPacket = true;
            }
        }

        if (hasPacket) {
            // Execute the actual socket send on the dedicated network thread
            SendUDP(packet);
        }
        else {
            // Only yield/sleep if we have an empty queue to maintain high throughput
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
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
        if (m_debugLogging) {
            std::cout << "[NetworkManager] Accepted incoming TCP connection (Socket: " << s << ")." << std::endl;
        }
    }
}

void NetworkManager::ReceiveTCP() {
    char buffer[4096];

    // 1. Process Pending Handshakes (Wait for ID assignment or normal handshake)
    for (auto it = m_pendingConnections.begin(); it != m_pendingConnections.end(); ) {
        // For outgoing (client) connections, the non-blocking connect() may still be
        // in progress. Poll for write-readiness before attempting to send the handshake.
        if (it->isOutgoing && !it->handshakeSent) {
            fd_set writeSet, exceptSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&exceptSet);
            FD_SET(it->socket, &writeSet);
            FD_SET(it->socket, &exceptSet);
            timeval timeout = { 0, 0 }; // non-blocking poll
            int ready = select(0, nullptr, &writeSet, &exceptSet, &timeout);

            if (ready > 0 && FD_ISSET(it->socket, &exceptSet)) {
                // Connection failed
                if (m_debugLogging) std::cout << "[NetworkManager] Outgoing connection to anchor failed.\n";
                closesocket(it->socket);
                it = m_pendingConnections.erase(it);
                continue;
            }
            if (ready > 0 && FD_ISSET(it->socket, &writeSet)) {
                // Verify no socket-level error before sending
                int sockErr = 0;
                int errLen = sizeof(sockErr);
                getsockopt(it->socket, SOL_SOCKET, SO_ERROR, (char*)&sockErr, &errLen);
                if (sockErr != 0) {
                    if (m_debugLogging) std::cout << "[NetworkManager] Outgoing connect() error: " << sockErr << "\n";
                    closesocket(it->socket);
                    it = m_pendingConnections.erase(it);
                    continue;
                }
                // Connection established — safe to send the handshake now
                SendHandshake(it->socket);
                it->handshakeSent = true;
            }
            // Not yet connected — check again next tick
            if (!it->handshakeSent) { ++it; continue; }
        }

        int bytes = recv(it->socket, buffer, sizeof(buffer), 0);
        if (bytes > 0) {
            it->buffer.insert(it->buffer.end(), buffer, buffer + bytes);

            // Check if we have at least the 4-byte size prefix
            if (it->buffer.size() >= 4) {
                uint32_t packetSize;
                memcpy(&packetSize, it->buffer.data(), 4);

                // Check if the full packet has arrived
                if (it->buffer.size() >= 4 + packetSize) {

                    // --- FlatBuffers Verification ---
                    const uint8_t* fbData = it->buffer.data() + 4;
                    flatbuffers::Verifier verifier(fbData, packetSize);

                    if (!VerifyNetworkMessageBuffer(verifier)) {
                        if (m_debugLogging) std::cout << "[NetworkManager] Dropped corrupted handshake packet.\n";
                        // Erase the corrupted data and try to recover the stream
                        it->buffer.erase(it->buffer.begin(), it->buffer.begin() + 4 + packetSize);
                        continue;
                    }

                    auto msg = GetNetworkMessage(fbData);

                    // --- CLIENT LOGIC: Handle ID Assignment from Host ---
                    if (msg->payload_type() == Payload_ReliableEvent) {
                        auto ev = msg->payload_as_ReliableEvent();
                        if (ev->event_type() == EventType_IdAssignment) {
                            int assignedId = std::stoi(ev->string_payload()->str());
                            m_localPeerId = assignedId;
                            PhysicsSystem::localPeerId = assignedId; // Update ECS Authority
                            
                            // Link this socket as our connection to the Host (Peer 0)
                            {
                                std::lock_guard<std::mutex> lock(m_peerMutex);
                                m_peers[0].tcpSocket = it->socket;
                                m_peers[0].isConnected = true;
                            }
                            m_peerCount++;
                            if (m_debugLogging) std::cout << "[NetworkManager] Successfully joined as Peer " << assignedId << "\n";
                            it = m_pendingConnections.erase(it);
                            continue;
                        }
                    }

                    // --- MESH LOGIC: Process handshakes from ANY peer ---
                    // If we are Host(0) and sender is -1, it assigns an ID.
                    // If we are Peer 1 and sender is Peer 2, it establishes the P2P link.
                    if (m_debugLogging) {
                        std::cout << "[NetworkManager] Received Handshake from Peer " << msg->sender_peer_id() << " on socket " << it->socket << std::endl;
                    }
                    HandleHandshake(it->socket, msg->sender_peer_id());
                    
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
    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        for (auto& peer : m_peers) {
            if (!peer.isConnected || peer.id == m_localPeerId.load()) continue;

            int bytes = recv(peer.tcpSocket, buffer, sizeof(buffer), 0);
            if (bytes > 0) {
                peer.tcpBuffer.insert(peer.tcpBuffer.end(), buffer, buffer + bytes);
                while (peer.tcpBuffer.size() >= 4) {
                    uint32_t packetSize;
                    memcpy(&packetSize, peer.tcpBuffer.data(), 4);
                    if (peer.tcpBuffer.size() < 4 + packetSize) break;

                    std::vector<uint8_t> packetData(peer.tcpBuffer.begin() + 4, peer.tcpBuffer.begin() + 4 + packetSize);
                    PushInboundEvent(packetData, false);
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
}

void NetworkManager::ReceiveUDP() {
    // 1. Use uint8_t directly to perfectly match PushInboundEvent
    std::vector<uint8_t> buffer(65507);
    sockaddr_in from;
    int fromLen = sizeof(from);

    while (true) {
        // 2. Cast buffer.data() to char* just for the Windows API
        int bytes = recvfrom(m_udpSocket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0, (sockaddr*)&from, &fromLen);
        if (bytes <= 0) break;

        static int udpCounter = 0;
        if (m_debugLogging && (udpCounter++ % 100 == 0)) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, ip, INET_ADDRSTRLEN);
            std::cout << "[NetworkManager] UDP Snapshot received (" << bytes << " bytes) from " << ip << ":" << ntohs(from.sin_port) << std::endl;
        }

        // 3. Use iterators (.begin()) instead of trying to add to the vector itself
        PushInboundEvent(std::vector<uint8_t>(buffer.begin(), buffer.begin() + bytes), true);
    }
}

void NetworkManager::MaintainOutgoingConnections() {
    // Only search for anchor if we don't have an ID and aren't already waiting on a connection
    if (m_localPeerId.load() == -1 && m_pendingConnections.empty() && m_peerCount < 3) { // 4 PEER CAP
        if (std::chrono::steady_clock::now() - m_lastConnectionAttempt < std::chrono::seconds(2)) {
            return;
        }
        m_lastConnectionAttempt = std::chrono::steady_clock::now();
    }

    int localId = m_localPeerId.load();

    // 1. If we don't have an ID, look for the Anchor (Peer 0)
    if (localId == -1 && m_pendingConnections.empty() && m_peerCount < 3) {
        if (m_debugLogging) std::cout << "[NetworkManager] Searching for Anchor (Peer 0) on port 27015...\n";
        sockaddr_in anchorAddr{};
        anchorAddr.sin_family = AF_INET;
        anchorAddr.sin_port = htons(27015);
        inet_pton(AF_INET, "127.0.0.1", &anchorAddr.sin_addr);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);

            // Non-blocking connect — returns WSAEWOULDBLOCK immediately on Windows.
            // The handshake is deferred until the connection is confirmed write-ready
            // in ReceiveTCP to avoid sending on a not-yet-connected socket.
            connect(s, (sockaddr*)&anchorAddr, sizeof(anchorAddr));

            m_pendingConnections.push_back({ s, {}, true, false });
        }
    }
    // 2. If we DO have an ID, ensure we are connected to all other peers to form the Full Mesh
    else if (localId > 0 && m_peerCount < 3) {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        
        // Rule: A peer only initiates TCP connections to peers with a LOWER ID than itself.
        // This prevents P1 and P2 from cross-connecting simultaneously and creating duplicate sockets.
        for (int targetId = 0; targetId < localId; ++targetId) {
            if (!m_peers[targetId].isConnected && m_peers[targetId].port != 0) {
                if (std::chrono::steady_clock::now() - m_lastP2PAttempt[targetId] < std::chrono::seconds(2)) {
                    continue;
                }
                m_lastP2PAttempt[targetId] = std::chrono::steady_clock::now();
                if (m_debugLogging) std::cout << "[NetworkManager] Attempting P2P link to Peer " << targetId << "...\n";
                
                SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (s != INVALID_SOCKET) {
                    u_long mode = 1;
                    ioctlsocket(s, FIONBIO, &mode);
                    // In this engine, TCP and UDP share the same port assignment
                    connect(s, (sockaddr*)&m_peers[targetId].udpAddr, sizeof(sockaddr_in)); 
                    
                    // --- THE FIX IS HERE ---
                    // Active outbound connections to known peers bypass the pending list.
                    // We instantly register them as active connections!
                    m_peers[targetId].tcpSocket = s;
                    m_peers[targetId].isConnected = true;
                    m_peerCount++;
                    
                    SendHandshake(s); // Send our ID so the receiving peer can register us
                }
            }
        }
    }
}

void NetworkManager::HandleHandshake(SOCKET s, int remoteId) {
    if (m_localPeerId.load() == 0 && remoteId == -1) {
        int newId = GetNextAvailableId();
        if (newId != -1) {
            flatbuffers::FlatBufferBuilder b(128);

            // Create the string ID first
            auto idStr = b.CreateString(std::to_string(newId));

            // Create the event using the string
            auto ev = CreateReliableEvent(b, EventType_IdAssignment, idStr);

            auto msg = CreateNetworkMessage(b, static_cast<int8_t>(m_localPeerId.load()), Payload_ReliableEvent, ev.Union());
            b.Finish(msg);

            uint32_t size = b.GetSize();
            if (SendAllTCP(s, (char*)&size, 4)) {
                SendAllTCP(s, (char*)b.GetBufferPointer(), size);
            }

            {
                std::lock_guard<std::mutex> lock(m_peerMutex);
                m_peers[newId].tcpSocket = s;
                m_peers[newId].isConnected = true;
            }
            m_peerCount++;
            if (m_debugLogging) {
                std::cout << "[NetworkManager] Peer " << newId << " joined the mesh (Assigned by Host). Mesh Size: " << m_peerCount.load() << "/4" << std::endl;
            }

            if (m_peerJoinedCallback) {
                m_peerJoinedCallback(newId);
            }
        }
        else {
            if (m_debugLogging) std::cout << "[NetworkManager] Mesh is full (4/4). Rejecting connection.\n";
            closesocket(s);
        }
    }
    else if (remoteId >= 0 && remoteId < 4) {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        m_peers[remoteId].tcpSocket = s;
        m_peers[remoteId].isConnected = true;
        m_peerCount++;
        if (m_debugLogging) {
            std::cout << "[NetworkManager] Peer " << remoteId << " established P2P link. Mesh Size: " << m_peerCount.load() << "/4" << std::endl;
        }
    }
}

void NetworkManager::SendHandshake(SOCKET s) {
    flatbuffers::FlatBufferBuilder b(128);
    auto ev = CreateReliableEventDirect(b, EventType_SceneLoad, "HANDSHAKE");
    auto msg = CreateNetworkMessage(b, static_cast<int8_t>(m_localPeerId.load()), Payload_ReliableEvent, ev.Union());
    b.Finish(msg);

    uint32_t size = b.GetSize();
    if (SendAllTCP(s, (char*)&size, 4)) {
        SendAllTCP(s, (char*)b.GetBufferPointer(), size);
    }
}

void NetworkManager::PushInboundEvent(const std::vector<uint8_t>& d, bool isUDP) {
    // ONLY drop UDP packets! 
    // TCP handles its own loss via the OS. Dropping TCP here permanently deletes PINGs!
    if (isUDP && m_simulatedPacketLoss > 0.0f) {
        float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        if (r < m_simulatedPacketLoss) return; // Drop UDP packet
    }

    std::lock_guard<std::mutex> lock(m_inboundMutex);

    // Latency applies to ALL packets to simulate network distance
    if (m_simulatedLatencyMs > 0.0f) {
        auto deliveryTime = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(m_simulatedLatencyMs));
        m_delayedInboundQueue.push_back({ deliveryTime, d });
    }
    else {
        m_inboundQueue.push(d);
    }
}

bool NetworkManager::PopInboundEvent(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(m_inboundMutex);

    // Process delayed queue
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_delayedInboundQueue.begin(); it != m_delayedInboundQueue.end(); ) {
        if (now >= it->deliveryTime) {
            m_inboundQueue.push(std::move(it->data));
            it = m_delayedInboundQueue.erase(it);
        }
        else {
            ++it;
        }
    }

    if (m_inboundQueue.empty()) return false;
    out = std::move(m_inboundQueue.front());
    m_inboundQueue.pop();
    return true;
}

void NetworkManager::SendUDP(const std::vector<uint8_t>& d) {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    for (auto& p : m_peers) {
        if (p.isConnected && p.id != m_localPeerId.load()) {
            sendto(m_udpSocket, (char*)d.data(), (int)d.size(), 0, (sockaddr*)&p.udpAddr, sizeof(p.udpAddr));
        }
    }
}

void NetworkManager::SendTCP(const std::vector<uint8_t>& d) {
    uint32_t size = (uint32_t)d.size();
    std::lock_guard<std::mutex> lock(m_peerMutex);
    for (auto& p : m_peers) {
        if (p.isConnected && p.tcpSocket != INVALID_SOCKET && p.id != m_localPeerId) {
            // Only send payload if the header successfully transmits
            if (!SendAllTCP(p.tcpSocket, (char*)&size, 4)) continue;
            SendAllTCP(p.tcpSocket, (char*)d.data(), size);
        }
    }
}

void NetworkManager::BroadcastState(Registry& registry, const std::vector<Entity>& locallyOwnedEntities) {
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();

    flatbuffers::FlatBufferBuilder builder(4096);
    std::vector<flatbuffers::Offset<EntityState>> entityStates;

    float timestamp = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_broadcastStartTime).count();

    entityStates.reserve(std::min((Entity)locallyOwnedEntities.size(), (Entity)512));

    const int8_t localId = static_cast<int8_t>(m_localPeerId.load());

    for (Entity i : locallyOwnedEntities) {
        // Double check component availability just in case, though they should be present
        if (transformArray->HasData(i) && physicsArray->HasData(i)) {
            auto& t = transformArray->GetData(i);
            auto& p = physicsArray->GetData(i);

            // Build the math primitives
            Vec3 pos(t.position.x, t.position.y, t.position.z);
            Vec3 rot(t.rotation.x, t.rotation.y, t.rotation.z);
            Vec3 linVel(p.velocity.x, p.velocity.y, p.velocity.z);
            Vec3 angVel(p.angularVelocity.x, p.angularVelocity.y, p.angularVelocity.z);

            // Create the EntityState table
            auto state = CreateEntityState(
                builder,
                static_cast<uint32_t>(i),
                localId,
                &pos, &rot, &linVel, &angVel
            );
            entityStates.push_back(state);
        }
    }

    if (entityStates.empty()) return;

    // Create the PhysicsSnapshot (The UDP Payload)
    auto statesVector = builder.CreateVector(entityStates);
    auto snapshot = CreatePhysicsSnapshot(builder, m_broadcastSequence++, static_cast<uint64_t>(timestamp * 1000.0f), statesVector);

    // Wrap it in the Root NetworkMessage
    auto msg = CreateNetworkMessage(
        builder,
        localId,
        Payload_PhysicsSnapshot,
        snapshot.Union()
    );

    builder.Finish(msg);

    // Broadcast via UDP
    std::vector<uint8_t> data(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    
    {
        std::lock_guard<std::mutex> lock(m_outboundMutex);
        m_outboundQueue.push(std::move(data));
    }
}

void NetworkManager::BroadcastSingleEntity(Registry& registry, Entity entity) {
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray   = registry.GetComponentArray<PhysicsComponent>();
    if (!transformArray->HasData(entity) || !physicsArray->HasData(entity)) return;

    auto& t = transformArray->GetData(entity);
    auto& p = physicsArray->GetData(entity);

    float timestamp = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_broadcastStartTime).count();

    flatbuffers::FlatBufferBuilder builder(256);
    Vec3 pos(t.position.x, t.position.y, t.position.z);
    Vec3 rot(t.rotation.x, t.rotation.y, t.rotation.z);
    Vec3 linVel(p.velocity.x, p.velocity.y, p.velocity.z);
    Vec3 angVel(p.angularVelocity.x, p.angularVelocity.y, p.angularVelocity.z);

    const int8_t localId = static_cast<int8_t>(m_localPeerId.load());
    auto state    = CreateEntityState(builder, static_cast<uint32_t>(entity), localId, &pos, &rot, &linVel, &angVel);
    auto statesVec = builder.CreateVector(std::vector<flatbuffers::Offset<EntityState>>{ state });
    auto snapshot = CreatePhysicsSnapshot(builder, m_broadcastSequence++, static_cast<uint64_t>(timestamp * 1000.0f), statesVec);
    auto msg      = CreateNetworkMessage(builder, localId, Payload_PhysicsSnapshot, snapshot.Union());
    builder.Finish(msg);

    std::vector<uint8_t> data(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    std::lock_guard<std::mutex> lock(m_outboundMutex);
    m_outboundQueue.push(std::move(data));
}

void NetworkManager::SendReliableEvent(NetworkEventType type, const std::string& payload, uint32_t targetEntity) {
    flatbuffers::FlatBufferBuilder builder(512);

    EventType fbType;
    switch (type) {
    case NetworkEventType::SceneLoad: fbType = EventType_SceneLoad; break;
    case NetworkEventType::SpawnObject: fbType = EventType_SpawnObject; break;
    case NetworkEventType::DespawnObject: fbType = EventType_DespawnObject; break;
    default: return;
    }

    auto payloadStr = builder.CreateString(payload);
    auto ev = CreateReliableEvent(builder, fbType, payloadStr, targetEntity);
    auto msg = CreateNetworkMessage(builder, static_cast<int8_t>(m_localPeerId.load()), Payload_ReliableEvent, ev.Union());
    builder.Finish(msg);

    std::vector<uint8_t> data(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    SendTCP(data);
}

void NetworkManager::ProcessInboundPackets(Registry& registry) {
    std::vector<uint8_t> packet;
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();

    while (PopInboundEvent(packet)) {
        // --- FlatBuffers Verification ---
        flatbuffers::Verifier verifier(packet.data(), packet.size());
        if (!VerifyNetworkMessageBuffer(verifier)) {
            if (m_debugLogging) {
                std::cout << "[NetworkManager] WARNING: Dropped corrupted network packet!\n";
            }
            continue; // Safely skip this garbage packet
        }
        // It is now memory-safe to parse this message
        auto netMsg = GetNetworkMessage(packet.data());

        if (netMsg->payload_type() == Payload_PhysicsSnapshot) {
            auto snapshot = netMsg->payload_as_PhysicsSnapshot();
            int senderId = netMsg->sender_peer_id();
            float rawTimestamp = static_cast<float>(snapshot->tick_index()) / 1000.0f;
            float normalizedTimestamp = rawTimestamp;

            {
                std::lock_guard<std::mutex> pLock(m_peerMutex);
                if (senderId >= 0 && senderId < 4) {
                    auto& peer = m_peers[senderId]; // Declare this FIRST

                    if (snapshot->sequence_number() <= peer.lastReceivedSequence && peer.lastReceivedSequence != 0) {
                        continue;
                    }

                    // --- PACKET LOSS CALCULATION ---
                    if (peer.lastReceivedSequence != 0) {
                        uint32_t expected = peer.lastReceivedSequence + 1;
                        if (snapshot->sequence_number() > expected) {
                            peer.packetsLost += (snapshot->sequence_number() - expected);
                        }
                    }
                    peer.packetsReceived++;
                    peer.lastReceivedSequence = snapshot->sequence_number();

                    // Update moving average every 100 packets
                    uint32_t totalPackets = peer.packetsReceived + peer.packetsLost;
                    if (totalPackets >= 100) {
                        peer.actualPacketLossPct = static_cast<float>(peer.packetsLost) / totalPackets;
                        peer.packetsReceived = 0;
                        peer.packetsLost = 0;
                    }
                    // -------------------------------

                    // On first packet from this sender, compute an offset so their timestamps
                    // align with our local playback timeline.
                    if (!peer.hasTimestampOffset && m_playbackTime > 0.0f) {
                        peer.timestampOffset = m_playbackTime - rawTimestamp;
                        peer.hasTimestampOffset = true;
                    }
                    normalizedTimestamp = rawTimestamp + peer.timestampOffset;
                }
            }

            std::lock_guard<std::mutex> hLock(m_historyMutex);

            if (snapshot->states() == nullptr) continue;

            for (auto state : *snapshot->states()) {
                Entity id = static_cast<Entity>(state->entity_id());

                if (transformArray->HasData(id) && physicsArray->HasData(id)) {
                    bool isMine = false;
                    if (ownershipArray->HasData(id)) {
                        isMine = (static_cast<int>(ownershipArray->GetData(id).GetOwnerIndex()) == m_localPeerId);
                    }

                    if (!isMine) {
                        auto& history = m_remoteHistories[id];
                        RemoteState rs;
                        rs.position = glm::vec3(state->position()->x(), state->position()->y(), state->position()->z());
                        rs.rotation = glm::vec3(state->rotation()->x(), state->rotation()->y(), state->rotation()->z());
                        rs.velocity = glm::vec3(state->linear_velocity()->x(), state->linear_velocity()->y(), state->linear_velocity()->z());
                        rs.timestamp = normalizedTimestamp;

                        if (history.states.empty() || rs.timestamp > history.states.back().timestamp) {
                            if (m_playbackTime == 0.0f) {
                                m_playbackTime = rs.timestamp;
                                // Retroactively anchor any seeds that were queued before the clock was established.
                                // They were stored with timestamp 0 as a sentinel; place them just behind the cursor.
                                int anchored = 0;
                                for (auto& [eid, h] : m_remoteHistories) {
                                    if (!h.states.empty() && h.states.front().timestamp == 0.0f) {
                                        h.states.front().timestamp = m_playbackTime - kInterpolationDelay;
                                        ++anchored;
                                    }
                                }
                                if (m_spawnLogging && anchored > 0) {
                                    std::cout << "[NetSpawn] Clock established at " << m_playbackTime
                                        << " - anchored " << anchored << " sentinel seed(s) to cursor "
                                        << (m_playbackTime - kInterpolationDelay) << "\n";
                                }
                            }

                            if (m_spawnLogging && history.states.size() <= 1) {
                                // Log the first real UDP state received for this entity
                                std::cout << "[NetSpawn] First UDP state for entity " << id
                                    << "  normalizedTs=" << rs.timestamp
                                    << "  cursor=" << (m_playbackTime - kInterpolationDelay)
                                    << "  tsAheadOfCursor=" << (rs.timestamp - (m_playbackTime - kInterpolationDelay))
                                    << "  pos=(" << rs.position.x << "," << rs.position.y << "," << rs.position.z << ")"
                                    << "  vel=(" << rs.velocity.x << "," << rs.velocity.y << "," << rs.velocity.z << ")";
                                if (!history.states.empty())
                                    std::cout << "  seedTs=" << history.states.back().timestamp
                                        << "  gapToSeed=" << (rs.timestamp - history.states.back().timestamp);
                                std::cout << "\n";
                            }

                            history.states.push_back(rs);
                            if (history.states.size() > RemoteHistory::MAX_HISTORY) {
                                history.states.pop_front();
                            }
                        } else if (m_debugLogging) {
                            std::cout << "[NetSpawn] UDP state DROPPED for entity " << id
                                << "  ts=" << rs.timestamp
                                << " <= lastTs=" << history.states.back().timestamp << "\n";
                        }

                        // --- NEW: Ensure visual sync even if OwnershipComponent arrived late ---
                        if (registry.HasComponent<RenderComponent>(id) && ownershipArray->HasData(id)) {
                            auto& render = registry.GetComponent<RenderComponent>(id);
                            if (!render.useColorTint) {
                                render.useDebugOverlay = false;
                                render.useColorTint = true;
                                render.tintColor = ownershipArray->GetData(id).GetOwnerColor();
                                render.tintColor.a = 1.0f;
                            }
                        }
                    }
                }
            }
        }
        else if (netMsg->payload_type() == Payload_ReliableEvent) {
            auto ev = netMsg->payload_as_ReliableEvent();
            std::string payloadStr = ev->string_payload() ? ev->string_payload()->str() : "";

            // --- PING INTERCEPTION ---
            if (ev->event_type() == EventType_SceneLoad) {
                if (payloadStr == "__PING__") {
                    SendReliableEventTo(netMsg->sender_peer_id(), NetworkEventType::SceneLoad, "__PONG__", 0);
                    continue; // Stop processing, don't pass to game
                }
                else if (payloadStr == "__PONG__") {
                    int sender = netMsg->sender_peer_id();
                    std::lock_guard<std::mutex> pLock(m_peerMutex);
                    if (sender >= 0 && sender < 4) {
                        auto rtt = std::chrono::steady_clock::now() - m_peers[sender].lastPingSent;
                        m_peers[sender].rttMs = std::chrono::duration<float, std::milli>(rtt).count();
                    }
                    continue; // Stop processing, don't pass to game
                }
            }

            if (m_eventCallback) {
                NetworkEventType type;
                bool valid = true;
                switch (ev->event_type()) {
                case EventType_SceneLoad: type = NetworkEventType::SceneLoad; break;
                case EventType_SpawnObject: type = NetworkEventType::SpawnObject; break;
                case EventType_DespawnObject: type = NetworkEventType::DespawnObject; break;
                default: valid = false; break;
                }

                if (valid) {
                    m_eventCallback(type, ev->string_payload() ? ev->string_payload()->str() : "", ev->target_entity_id());
                }
            }
        }
    }
}

void NetworkManager::UpdateInterpolation(Registry& registry, float dt) {
    if (m_playbackTime == 0.0f) return;

    // 1. Advance Playback Clock
    m_playbackTime += dt;

    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();

    std::lock_guard<std::mutex> hLock(m_historyMutex);

    // 2. Clock Synchronization & Management
    // Find the latest timestamp received across all remote objects
    float latestPacketTimestamp = 0.0f;
    for (const auto& [id, history] : m_remoteHistories) {
        if (!history.states.empty()) {
            latestPacketTimestamp = std::max(latestPacketTimestamp, history.states.back().timestamp);
        }
    }

    if (latestPacketTimestamp > 0.0f) {
        // We want targetPlaybackTime to be roughly (kInterpolationDelay) behind the latest packet.
        // If it's too far behind, catch up. If it's too close, slow down.
        float currentLag = latestPacketTimestamp - (m_playbackTime - kInterpolationDelay);

        if (currentLag > 0.3f) { // Too much lag (> 300ms) — advance at 5x total speed
            m_playbackTime += dt * 4.0f; // +4 on top of the dt already added = 5x total
        }
        else if (currentLag < 0.05f) { // Too close to edge — advance at 0.5x total speed
            m_playbackTime -= dt * 0.5f; // -0.5 from the dt already added = 0.5x total
        }
    }

    float targetPlaybackTime = m_playbackTime - kInterpolationDelay;

    // 3. Process Interpolation and Cleanup Stale History
    for (auto it = m_remoteHistories.begin(); it != m_remoteHistories.end(); ) {
        uint32_t id = it->first;
        auto& history = it->second;

        // Cleanup: If the entity was deleted from the registry, remove its network history
        if (!transformArray->HasData(id) || !physicsArray->HasData(id)) {
            it = m_remoteHistories.erase(it);
            continue;
        }

        auto& t = transformArray->GetData(id);
        auto& p = physicsArray->GetData(id);

        if (history.states.empty()) {
            ++it;
            continue;
        }

        RemoteState* s0 = nullptr;
        RemoteState* s1 = nullptr;

        for (size_t i = 0; i < history.states.size(); ++i) {
            if (history.states[i].timestamp > targetPlaybackTime) {
                s1 = &history.states[i];
                if (i > 0) s0 = &history.states[i - 1];
                break;
            }
        }

        if (s0 && s1) {
            float t_blend = (targetPlaybackTime - s0->timestamp) / (s1->timestamp - s0->timestamp);
            t.position = glm::mix(s0->position, s1->position, t_blend);
            t.rotation = glm::mix(s0->rotation, s1->rotation, t_blend);
            p.velocity = s1->velocity;
            t.UpdateMatrix();

            if (m_interpLogging && history.states.size() <= 2) {
                std::cout << "[Interp] entity=" << id << " INTERPOLATING"
                    << "  blend=" << t_blend
                    << "  s0Ts=" << s0->timestamp << "  s1Ts=" << s1->timestamp
                    << "  cursor=" << targetPlaybackTime
                    << "  pos=(" << t.position.x << "," << t.position.y << "," << t.position.z << ")"
                    << "  s0pos=(" << s0->position.x << "," << s0->position.y << "," << s0->position.z << ")"
                    << "  s1pos=(" << s1->position.x << "," << s1->position.y << "," << s1->position.z << ")\n";
            }
        }
        else if (s1) {
            t.position = s1->position;
            t.rotation = s1->rotation;
            p.velocity = s1->velocity;
            t.UpdateMatrix();

            if (m_interpLogging && history.states.size() <= 2) {
                std::cout << "[Interp] entity=" << id << " CLAMPED TO FUTURE STATE"
                    << "  s1Ts=" << s1->timestamp << "  cursor=" << targetPlaybackTime
                    << "  cursorBehindByMs=" << (s1->timestamp - targetPlaybackTime) * 1000.0f
                    << "  pos=(" << t.position.x << "," << t.position.y << "," << t.position.z << ")\n";
            }
        }
        else {
            // Extrapolate
            const auto& last = history.states.back();
            float extrapolationTime = std::min(targetPlaybackTime - last.timestamp, 0.5f);
            t.position = last.position + last.velocity * extrapolationTime;
            t.rotation = last.rotation;
            p.velocity = last.velocity;
            t.UpdateMatrix();

            if (m_interpLogging && history.states.size() <= 2) {
                std::cout << "[Interp] entity=" << id << " EXTRAPOLATING"
                    << "  lastTs=" << last.timestamp << "  cursor=" << targetPlaybackTime
                    << "  extrapolationTime=" << extrapolationTime
                    << "  pos=(" << t.position.x << "," << t.position.y << "," << t.position.z << ")"
                    << "  vel=(" << last.velocity.x << "," << last.velocity.y << "," << last.velocity.z << ")\n";
            }
        }
        ++it;
    }
}

void NetworkManager::SendTCPTo(int targetPeerId, const std::vector<uint8_t>& data) {
    uint32_t size = (uint32_t)data.size();
    std::lock_guard<std::mutex> lock(m_peerMutex);
    if (targetPeerId >= 0 && targetPeerId < m_peers.size()) {
        auto& p = m_peers[targetPeerId];
        if (p.isConnected && p.tcpSocket != INVALID_SOCKET) {
            if (!SendAllTCP(p.tcpSocket, (char*)&size, 4)) return;
            SendAllTCP(p.tcpSocket, (char*)data.data(), size);
        }
    }
}


void NetworkManager::SeedRemoteState(Entity id, glm::vec3 pos, glm::vec3 vel, glm::vec3 rot, float normalizedSpawnTs) {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    auto& history = m_remoteHistories[id];
    if (!history.states.empty()) return; // Already has real data

    RemoteState rs;
    rs.position = pos;
    rs.rotation = rot;
    rs.velocity = vel;

    const char* tsSource = "cursor-based heuristic";
    bool seedClamped = false;
    float clampDelta = 0.0f;
    if (normalizedSpawnTs >= 0.0f) {
        // Use the actual spawn time propagated in the TCP message.
        // Clamp to the cursor so newly spawned objects do not hover in a future state.
        if (m_playbackTime > 0.0f) {
            float cursorTs = m_playbackTime - kInterpolationDelay;
            float clampedTs = std::min(normalizedSpawnTs, cursorTs);
            float rewind = clampedTs - normalizedSpawnTs;
            rs.timestamp = clampedTs;
            rs.position = pos + vel * rewind;
            seedClamped = (clampedTs != normalizedSpawnTs);
            clampDelta = clampedTs - normalizedSpawnTs;
            tsSource = seedClamped ? "clamped to cursor" : "propagated spawn timestamp";
        } else {
            rs.timestamp = normalizedSpawnTs;
            tsSource = "propagated spawn timestamp";
        }
    } else if (m_playbackTime > 0.0f) {
        rs.timestamp = m_playbackTime - kInterpolationDelay;
    } else {
        // Clock not established yet — use sentinel 0; ProcessInboundPackets will anchor it.
        rs.timestamp = 0.0f;
        tsSource = "SENTINEL (clock not ready)";
    }

    history.states.push_back(rs);

    if (m_spawnLogging) {
        std::cout << "[NetSpawn] Seed created for entity " << id
            << "  playbackTime=" << m_playbackTime
            << "  seedTimestamp=" << rs.timestamp
            << "  source=" << tsSource
            << "  cursorGap=" << (m_playbackTime - kInterpolationDelay - rs.timestamp)
            << "  pos=(" << pos.x << "," << pos.y << "," << pos.z << ")"
            << "  vel=(" << vel.x << "," << vel.y << "," << vel.z << ")\n";
        if (seedClamped) {
            std::cout << "[NetSpawn] Seed clamped for entity " << id
                << "  clampDelta=" << clampDelta
                << "  cursor=" << (m_playbackTime - kInterpolationDelay)
                << "  normalizedSpawnTs=" << normalizedSpawnTs << "\n";
        }
    }
}

int NetworkManager::GetRemoteEntityCount() {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    return static_cast<int>(m_remoteHistories.size());
}

float NetworkManager::GetLatestRemoteTimestamp() {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    float latest = 0.0f;
    for (const auto& [id, h] : m_remoteHistories)
        if (!h.states.empty())
            latest = std::max(latest, h.states.back().timestamp);
    return latest;
}

NetworkManager::PeerStatus NetworkManager::GetPeerStatus(int peerId) {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    if (peerId >= 0 && peerId < static_cast<int>(m_peers.size())) {
        return {
            m_peers[peerId].isConnected,
            m_peers[peerId].port,
            m_peers[peerId].ip,
            m_peers[peerId].rttMs,               
            m_peers[peerId].actualPacketLossPct  
        };
    }
    return {};
}

void NetworkManager::SendReliableEventTo(int targetPeerId, NetworkEventType type, const std::string& payload, uint32_t targetEntity) {
    flatbuffers::FlatBufferBuilder builder(512);
    EventType fbType;
    switch (type) {
    case NetworkEventType::SceneLoad: fbType = EventType_SceneLoad; break;
    case NetworkEventType::SpawnObject: fbType = EventType_SpawnObject; break;
    case NetworkEventType::DespawnObject: fbType = EventType_DespawnObject; break;
    default: return;
    }
    auto payloadStr = builder.CreateString(payload);
    auto ev = CreateReliableEvent(builder, fbType, payloadStr, targetEntity);
    auto msg = CreateNetworkMessage(builder, static_cast<int8_t>(m_localPeerId.load()), Payload_ReliableEvent, ev.Union());
    builder.Finish(msg);
    std::vector<uint8_t> data(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());

    SendTCPTo(targetPeerId, data);
}

void NetworkManager::ClearHistory() {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_remoteHistories.clear();
    m_playbackTime = 0.0f; // Reset the interpolation clock

    // Also reset peer timestamp offsets so they recalibrate to the new scene
    std::lock_guard<std::mutex> pLock(m_peerMutex);
    for (auto& peer : m_peers) {
        peer.hasTimestampOffset = false;
        peer.timestampOffset = 0.0f;
    }
}

void NetworkManager::SetSimulationConditions(float latencyMs, float packetLossPct) {
    m_simulatedLatencyMs = std::max(0.0f, latencyMs);
    m_simulatedPacketLoss = std::clamp(packetLossPct, 0.0f, 1.0f);
}

void NetworkManager::SetLocalPeerId(int id) {
    m_localPeerId.store(id);
    // Your Application::SimulationLoop already detects this and updates ECS partitions!
}

void NetworkManager::ReconfigurePeer(int peerId, const std::string& ip, uint16_t port) {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    if (peerId >= 0 && peerId < 4) {
        // If changing an active peer, disconnect them first
        if (m_peers[peerId].isConnected && m_peers[peerId].tcpSocket != INVALID_SOCKET) {
            closesocket(m_peers[peerId].tcpSocket);
            m_peers[peerId].tcpSocket = INVALID_SOCKET;
            m_peers[peerId].isConnected = false;
            m_peerCount--;
        }
        m_peers[peerId].ip = ip;
        m_peers[peerId].port = port;
        m_peers[peerId].udpAddr.sin_family = AF_INET;
        m_peers[peerId].udpAddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &m_peers[peerId].udpAddr.sin_addr);
    }
}