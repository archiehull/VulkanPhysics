#pragma once

#include <cstdint>
#include <functional>

namespace Network {

using UdpReceiveCallback = std::function<void(const uint8_t* data, size_t size)>;

class UDPTransport {
public:
    UDPTransport();
    ~UDPTransport();

    bool Open(uint16_t localPort);
    void Close();

    bool SendTo(const char* ip, uint16_t port, const uint8_t* data, size_t size);
    void SetReceiveCallback(UdpReceiveCallback cb);

private:
    void PollRecv();
    UdpReceiveCallback m_cb;
    void* m_internal = nullptr;
};

} // namespace Network
