#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace stackchan_demo {

struct Device {
    std::uint64_t address;
    std::string name;
};

enum class Channel { avatar, motion, rgb };

// Initialize a WinRT multithreaded apartment on each calling thread first.
// Only matching StackChan advertisements are returned; scanning never connects.
std::vector<Device> scan(std::chrono::seconds duration);

class BleLink {
public:
    explicit BleLink(std::uint64_t address);
    ~BleLink();
    BleLink(const BleLink&) = delete;
    BleLink& operator=(const BleLink&) = delete;

    // Sends one complete UTF-8 JSON value, at most 512 bytes, with a response.
    // Windows handles ATT long writes. Custom firmware fragments are not sent.
    void send(Channel channel, const std::string& json);
    std::uint16_t mtu() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace stackchan_demo
