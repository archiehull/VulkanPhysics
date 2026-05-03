#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace Network {

class TCPControl {
public:
    TCPControl();
    ~TCPControl();

    bool Start(uint16_t listenPort);
    void Stop();

    // Connect to a remote control port
    bool Connect(const std::string& ip, uint16_t port);

private:
};

} // namespace Network
