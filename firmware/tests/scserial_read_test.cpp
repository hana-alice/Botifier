#include "SCS.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using TickType_t = unsigned;
constexpr unsigned UART_NUM_MAX = 3;
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms) * 100u / 1000u)

namespace {
struct Byte { std::int64_t at_us; unsigned char value; };
struct Fixture {
    std::int64_t now_us = 0;
    std::vector<Byte> fifo;
    std::vector<unsigned char> response, consumed, transmitted;
    unsigned yields = 0, buffered_yields = 0, rx_flushes = 0, tx_flushes = 0, tx_ticks = 0;
    bool fail_first_read = false;
};
Fixture* active;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
unsigned available()
{
    return static_cast<unsigned>(std::count_if(active->fifo.begin(), active->fifo.end(),
        [](const Byte& byte) { return byte.at_us <= active->now_us; }));
}
}

std::int64_t esp_timer_get_time() { active->now_us += 50; return active->now_us; }
void uart_get_buffered_data_len(unsigned, std::size_t* size) { *size = available(); }
int uart_read_bytes(unsigned, unsigned char* byte, unsigned, unsigned)
{
    if (active->fail_first_read) { active->fail_first_read = false; return 0; }
    if (active->fifo.empty() || active->fifo.front().at_us > active->now_us) return 0;
    *byte = active->fifo.front().value;
    active->consumed.push_back(*byte);
    active->fifo.erase(active->fifo.begin());
    return 1;
}
void vTaskDelay(TickType_t ticks)
{
    check(ticks == 0, "100Hz 1ms delay must be a zero-tick yield");
    ++active->yields;
    if (available()) ++active->buffered_yields;
    // Deliberate competing-task model, not a claimed ESP scheduling duration.
    active->now_us += 1000;
}
int uart_write_bytes(unsigned, const char* data, int size)
{
    active->transmitted.insert(active->transmitted.end(), data, data + size);
    return size;
}
void uart_flush_input(unsigned) { ++active->rx_flushes; active->fifo.clear(); }
void uart_wait_tx_done(unsigned, TickType_t ticks)
{
    ++active->tx_flushes;
    active->tx_ticks = ticks;
    active->now_us += static_cast<std::int64_t>(active->transmitted.size()) * 10;
    for (const auto byte : active->response) active->fifo.push_back({active->now_us, byte});
}

// The generated include contains the six production SCSerial I/O definitions.
// Only the class name changes; the actual SCS packet parser is linked unchanged.
class HostSerial : public SCS {
public:
    HostSerial() : SCS(0, 1) { u8Error = 0; }
    unsigned uart_num = 1;
    unsigned long IOTimeOut = 10;
    int readSCS(unsigned char*, int) override;
    int readSCS(unsigned char*, int, unsigned long) override;
    int writeSCS(unsigned char*, int) override;
    int writeSCS(unsigned char) override;
    void rFlushSCS() override;
    void wFlushSCS() override;
};
#include "scserial_read_impl.inc"

namespace {
std::vector<unsigned char> packet(std::vector<unsigned char> payload, unsigned char status = 0)
{
    std::vector<unsigned char> bytes{255, 255, 1, static_cast<unsigned char>(payload.size() + 2), status};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    unsigned sum = 0;
    for (std::size_t i = 2; i < bytes.size(); ++i) sum += bytes[i];
    bytes.push_back(static_cast<unsigned char>(~sum));
    return bytes;
}

void buffered_reads_drain_without_yield()
{
    for (const bool total_timeout : {false, true}) {
        Fixture fixture; active = &fixture; HostSerial serial;
        for (unsigned char value = 0; value < 16; ++value) fixture.fifo.push_back({0, value});
        unsigned char output[16]{};
        const int size = total_timeout ? serial.readSCS(output, 16, 1000) : serial.readSCS(output, 16);
        check(size == 16 && output[0] == 0 && output[15] == 15, "buffered read changed byte order or count");
        check(fixture.yields == 0, "buffered bytes unnecessarily yielded to the scheduler");
    }
}

void real_packet_parser_and_flushes_remain_intact()
{
    for (const unsigned length : {1u, 2u, 15u}) {
        Fixture fixture; active = &fixture; HostSerial serial;
        fixture.response = packet(std::vector<unsigned char>(length, 42));
        unsigned char output[15]{};
        check(serial.Read(1, 56, output, static_cast<unsigned char>(length)) == static_cast<int>(length), "real parser rejected a valid read packet");
        check(output[0] == 42 && output[length - 1] == 42 && serial.getLastError() == 0, "read payload or status changed");
        check(fixture.buffered_yields == 0, "real parser yielded with ready packet bytes");
        check(fixture.rx_flushes == 1 && fixture.tx_flushes == 1 && fixture.tx_ticks == 10, "flush/100ms TX-wait behavior changed");
    }
    Fixture fixture; active = &fixture; HostSerial serial; fixture.response = packet({}, 4);
    check(serial.writeByte(1, 40, 1) == 1 && serial.getState() == 4 && serial.getLastError() == 0, "valid ACK status semantics changed");
    check(fixture.buffered_yields == 0, "ACK parser yielded with ready bytes");
}

void timeout_partial_and_gap_semantics_remain_intact()
{
    for (const bool total_timeout : {false, true}) {
        for (const bool partial : {false, true}) {
            Fixture fixture; active = &fixture; HostSerial serial;
            if (partial) fixture.fifo = {{0, 42}};
            unsigned char output[3]{};
            const int size = total_timeout ? serial.readSCS(output, 3, 10) : serial.readSCS(output, 3);
            check(size == (partial ? 1 : 0) && fixture.yields > 0, "empty/partial reads stopped waiting or changed count");
            check(fixture.now_us >= 10000 && fixture.now_us < 13000, "timeout deadline changed");
        }
        Fixture fixture; active = &fixture; HostSerial serial; fixture.fifo = {{0, 1}, {9000, 2}, {18000, 3}};
        unsigned char output[3]{};
        const int size = total_timeout ? serial.readSCS(output, 3, 10) : serial.readSCS(output, 3);
        check(size == (total_timeout ? 2 : 3), "total timeout and inter-byte timeout became interchangeable");
    }
}

void read_race_null_drain_and_boundaries_remain_intact()
{
    Fixture fixture; active = &fixture; HostSerial serial;
    fixture.fail_first_read = true; fixture.fifo = {{0, 42}, {0, 43}, {0, 44}, {0, 45}};
    check(serial.readSCS(nullptr, 3) == 3 && fixture.fifo.size() == 1 && fixture.fifo.front().value == 45, "drain crossed its requested packet boundary");
    check(fixture.yields == 1, "failed UART read must yield rather than busy-spin");
    serial.uart_num = UART_NUM_MAX;
    check(serial.readSCS(nullptr, 1) == 0 && serial.readSCS(nullptr, 1, 10) == 0, "invalid UART was accessed");
}

void real_packet_errors_remain_intact()
{
    for (int variant = 0; variant < 5; ++variant) {
        Fixture fixture; active = &fixture; HostSerial serial;
        fixture.response = packet({42});
        if (variant == 0) fixture.response.back() ^= 1;
        else if (variant == 1) fixture.response[2] = 2;
        else if (variant == 2) fixture.response[3] = 7;
        else if (variant == 3) fixture.response = {255, 255, 1};
        else fixture.response.clear();
        check(serial.readByte(1, 40) == -1 && serial.getLastError() != 0, "malformed/partial/missing reply was accepted");
    }
}
}

int main()
{
    try {
        buffered_reads_drain_without_yield();
        real_packet_parser_and_flushes_remain_intact();
        timeout_partial_and_gap_semantics_remain_intact();
        read_race_null_drain_and_boundaries_remain_intact();
        real_packet_errors_remain_intact();
        std::cout << "scserial_read_test: 5 groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scserial_read_test: " << error.what() << '\n';
        return 1;
    }
}
