#pragma once

#include <cstdint>

namespace Network {

enum PacketType : uint8_t {
    PT_HELLO = 1,
    PT_HELLO_ACK = 2,
    PT_STATE_UDP = 10,
    PT_SPAWN_RPC = 20,
    PT_SCENE_RPC = 21,
    PT_OWNERSHIP_RPC = 22,
    PT_IMPULSE_RPC = 23,
    PT_HEARTBEAT = 30,
    PT_ACK = 31
};

// Lightweight header placed before FlatBuffer payloads
#pragma pack(push, 1)
struct NetHeader {
    uint8_t type;       // PacketType
    uint32_t sender_id; // local peer id
    uint32_t seq;       // sequence number
    uint64_t timestamp; // ms since epoch or local sim time
};
#pragma pack(pop)

} // namespace Network
