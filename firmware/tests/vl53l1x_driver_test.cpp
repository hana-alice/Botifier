#include "distance_sensor/vl53l1x.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <esp_timer.h>
#include <freertos/task.h>

namespace {

int64_t virtual_time_us = 0;
size_t delay_calls      = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void checkEmpty(const distance_sensor::Reading& reading, bool ready)
{
    check(!ready && !reading.valid && reading.millimeters == 0 && reading.raw_status == 255,
          "No sample/error must clear both outputs, including a previously valid Reading");
}

uint16_t decode16(const uint8_t* bytes)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

}  // namespace

// A register-level device model, not a second driver implementation. The model
// exposes boot time, autonomous completion, latched IRQ and injected bus faults.
// Tests run the production .cpp through the same synchronous I2C boundary.
struct FakeI2cDevice {
    struct Transfer {
        bool read;
        uint16_t reg;
        size_t length;
    };

    struct Failure {
        bool armed        = false;
        bool read         = false;
        uint16_t reg      = 0;
        size_t call       = 0;  // Nonzero selects an absolute transaction number.
        esp_err_t error   = ESP_FAIL;
        bool apply_write  = false;  // Simulate timeout after the chip accepted a write.
    };

    std::array<uint8_t, 65536> registers{};
    std::vector<Transfer> transfers;
    Failure failure;
    int64_t boot_ready_at_us   = 0;  // Negative means never boots.
    int64_t next_frame_at_us   = -1;
    bool automatic_frames     = true;
    bool pending              = false;
    bool ranging              = false;
    size_t acknowledgements   = 0;
    size_t result_reads       = 0;

    FakeI2cDevice()
    {
        virtual_time_us = 0;
        delay_calls     = 0;
        set16(0x010F, 0xEACC);
        // High bits test ST's 10-bit oscillator mask; low ten bits are 300.
        set16(0x00DE, 0xA12C);
        updateIrq();
    }

    void set16(uint16_t reg, uint16_t value)
    {
        registers[reg]     = static_cast<uint8_t>(value >> 8);
        registers[reg + 1] = static_cast<uint8_t>(value);
    }

    uint16_t get16(uint16_t reg) const
    {
        return decode16(registers.data() + reg);
    }

    uint32_t get32(uint16_t reg) const
    {
        return (static_cast<uint32_t>(get16(reg)) << 16) | get16(reg + 2);
    }

    void updateIrq()
    {
        const uint8_t active_level = (registers[0x0030] & 0x10) == 0 ? 1 : 0;
        const uint8_t level = pending ? active_level : static_cast<uint8_t>(active_level ^ 1);
        registers[0x0031] = static_cast<uint8_t>((registers[0x0031] & 0xFE) | level);
    }

    void publish(uint8_t raw_status, uint16_t millimeters)
    {
        registers[0x0089] = raw_status;
        set16(0x0096, millimeters);
        pending = true;
        updateIrq();
    }

    void refresh()
    {
        registers[0x00E5] = boot_ready_at_us >= 0 && virtual_time_us >= boot_ready_at_us ? 1 : 0;
        if (automatic_frames && ranging && next_frame_at_us >= 0 && virtual_time_us >= next_frame_at_us) {
            publish(9, 600);
            next_frame_at_us = virtual_time_us + 100000;
        }
    }

    void applyWrite(uint16_t reg, const uint8_t* data, size_t length)
    {
        for (size_t i = 0; i < length; ++i) {
            const size_t address = reg + i;
            // GPIO status is read-only, even though ST's default block spans it.
            if (address != 0x0031) registers[address] = data[i];
            if (address == 0x0086 && data[i] == 1) {
                pending = false;
                ++acknowledgements;
            }
            if (address == 0x0087) {
                ranging = data[i] == 0x40;
                next_frame_at_us = ranging ? virtual_time_us + 20000 : -1;
            }
        }
        updateIrq();
    }

    esp_err_t begin(bool read, uint16_t reg, size_t length, int timeout_ms)
    {
        check(timeout_ms > 0 && timeout_ms <= 50, "Every I2C call must have a finite timeout <=50 ms");
        check(static_cast<size_t>(reg) + length <= registers.size(), "I2C register access out of bounds");
        check(transfers.size() < 20000 && virtual_time_us < 5000000, "Unbounded polling/transaction loop");
        transfers.push_back({read, reg, length});
        virtual_time_us += 100;  // A successful/failing bus transaction consumes time.
        refresh();
        const bool matches = failure.call != 0 ? transfers.size() == failure.call
                                               : failure.read == read && failure.reg == reg;
        if (failure.armed && matches) {
            failure.armed = false;
            return failure.error;
        }
        return ESP_OK;
    }

    void failNext(bool read, uint16_t reg, esp_err_t error, bool apply_write = false)
    {
        failure = {true, read, reg, 0, error, apply_write};
    }
};

extern "C" esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t* data, size_t size,
                                         int timeout_ms)
{
    check(device != nullptr && data != nullptr && size >= 3, "Malformed I2C register write");
    const uint16_t reg = decode16(data);
    const esp_err_t error = device->begin(false, reg, size - 2, timeout_ms);
    if (error == ESP_OK || device->failure.apply_write) device->applyWrite(reg, data + 2, size - 2);
    return error;
}

extern "C" esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t* address,
                                                 size_t address_size, uint8_t* data, size_t size, int timeout_ms)
{
    check(device != nullptr && address != nullptr && address_size == 2 && data != nullptr && size > 0,
          "Register reads require a two-byte big-endian address and a destination");
    const uint16_t reg = decode16(address);
    const esp_err_t error = device->begin(true, reg, size, timeout_ms);
    if (error != ESP_OK) return error;
    if (reg == 0x0089) ++device->result_reads;
    for (size_t i = 0; i < size; ++i) data[i] = device->registers[reg + i];
    return ESP_OK;
}

extern "C" int64_t esp_timer_get_time()
{
    return virtual_time_us;
}

extern "C" void vTaskDelay(TickType_t ticks)
{
    check(ticks > 0, "Polling must yield at least one scheduler tick");
    ++delay_calls;
    virtual_time_us += static_cast<int64_t>(ticks) * 1000000 / configTICK_RATE_HZ;
}

namespace {

struct Fixture {
    FakeI2cDevice device;
    distance_sensor::Vl53l1x driver;

    void initialize()
    {
        check(driver.initialize(&device) == ESP_OK, "Expected successful initialization");
        // After calibration, runtime results are explicitly injected by each test.
        device.automatic_frames = false;
    }
};

void poison(distance_sensor::Reading& reading, bool& ready)
{
    reading = {100, 9, true};
    ready = true;
}

void testUninitializedAndNull()
{
    Fixture fixture;
    distance_sensor::Reading reading;
    bool ready = false;
    poison(reading, ready);
    check(fixture.driver.poll(reading, ready) == ESP_ERR_INVALID_STATE, "Uninitialized poll must fail");
    checkEmpty(reading, ready);
    check(fixture.driver.initialize(nullptr) == ESP_ERR_INVALID_ARG, "Null device must be rejected");
    check(fixture.driver.stop() == ESP_OK && fixture.device.transfers.empty(), "No bus access without a device");
}

void testWrongModelIsReadOnly()
{
    for (const uint16_t id : {uint16_t{0}, uint16_t{0xCCEA}, uint16_t{0xEEAA}}) {
        Fixture fixture;
        fixture.device.set16(0x010F, id);
        check(fixture.driver.initialize(&fixture.device) == ESP_ERR_NOT_SUPPORTED, "Wrong/swapped model ID accepted");
        for (const auto& transfer : fixture.device.transfers) check(transfer.read, "Wrong model received a write");
    }
}

void testBootDeadline()
{
    Fixture fixture;
    fixture.device.boot_ready_at_us = -1;
    check(fixture.driver.initialize(&fixture.device) == ESP_ERR_TIMEOUT, "Never-booting sensor must time out");
    check(virtual_time_us >= 1000000 && virtual_time_us <= 1060000, "Boot wait exceeded its bounded deadline");
    check(delay_calls > 0, "Boot wait did not yield");
    for (const auto& transfer : fixture.device.transfers) check(transfer.read, "Unbooted sensor received writes");
}

void testCalibrationDeadlineAndStop()
{
    Fixture fixture;
    fixture.device.automatic_frames = false;
    check(fixture.driver.initialize(&fixture.device) == ESP_ERR_TIMEOUT, "Missing calibration result must time out");
    check(virtual_time_us >= 1000000 && virtual_time_us <= 1100000, "Calibration wait exceeded its deadline");
    check(delay_calls > 0 && !fixture.device.ranging, "Failed calibration must yield and attempt to stop");
    distance_sensor::Reading reading;
    bool ready = true;
    check(fixture.driver.poll(reading, ready) == ESP_ERR_INVALID_STATE, "Failed initialization left driver active");
    checkEmpty(reading, ready);
}

void testInitializationProgramsRequestedMode()
{
    Fixture fixture;
    fixture.device.boot_ready_at_us = 20000;
    fixture.initialize();
    const auto& device = fixture.device;
    check(delay_calls >= 4, "Delayed boot and calibration must use virtual scheduler delays");
    check(device.ranging && device.registers[0x0087] == 0x40, "Initialization did not start ranging");
    check(device.registers[0x004B] == 0x0A && device.registers[0x0060] == 0x0F &&
              device.registers[0x0063] == 0x0D && device.registers[0x0069] == 0xB8 &&
              device.get16(0x0078) == 0x0F0D && device.get16(0x007A) == 0x0E0E,
          "Register protocol does not select ST Long distance mode");
    check(device.get16(0x005E) == 0x00AD && device.get16(0x0061) == 0x00C6, "Budget is not Long-mode 50 ms");
    check(device.get32(0x006C) == 32250, "100 ms period must use masked, calibrated oscillator and big-endian order");
    check(device.registers[0x002E] == 1 && device.registers[0x002F] == 1, "3.3 V pads not configured");
    check(device.registers[0x0008] == 9 && device.registers[0x000B] == 0, "Post-VHV calibration setup missing");
    check(device.registers[0x0046] == 0x20, "New-sample-ready interrupt mode missing");
}

void testEveryInitializationTransactionFailure()
{
    size_t transaction_count = 0;
    {
        Fixture baseline;
        baseline.initialize();
        transaction_count = baseline.device.transfers.size();
    }
    for (size_t call = 1; call <= transaction_count; ++call) {
        Fixture fixture;
        fixture.device.failure = {true, false, 0, call, ESP_FAIL, false};
        check(fixture.driver.initialize(&fixture.device) == ESP_FAIL,
              "Initialization swallowed I2C failure at transaction " + std::to_string(call));
        distance_sensor::Reading reading;
        bool ready = true;
        check(fixture.driver.poll(reading, ready) == ESP_ERR_INVALID_STATE, "I2C failure left driver initialized");
        checkEmpty(reading, ready);
    }
    Fixture bad_clock;
    bad_clock.device.set16(0x00DE, 0);
    check(bad_clock.driver.initialize(&bad_clock.device) == ESP_ERR_INVALID_RESPONSE, "Zero oscillator accepted");
    check(!bad_clock.device.ranging, "Bad oscillator did not stop measurement");
}

void testFreshFrameAndNoReplay()
{
    Fixture fixture;
    fixture.initialize();
    distance_sensor::Reading reading;
    bool ready = false;
    poison(reading, ready);
    const size_t reads_before = fixture.device.result_reads;
    check(fixture.driver.poll(reading, ready) == ESP_OK, "No-data poll failed");
    checkEmpty(reading, ready);
    check(fixture.device.result_reads == reads_before, "Read old range without data-ready");

    const size_t acks_before = fixture.device.acknowledgements;
    fixture.device.publish(0xE9, 0x02AB);
    check(fixture.driver.poll(reading, ready) == ESP_OK && ready && reading.valid, "Fresh valid frame not published");
    check(reading.raw_status == 0xE9 && reading.millimeters == 683, "Raw status bits/distance byte order lost");
    check(!fixture.device.pending && fixture.device.acknowledgements == acks_before + 1,
          "Consumed result not acknowledged exactly once");
    check(fixture.driver.poll(reading, ready) == ESP_OK, "Poll after consumed frame failed");
    checkEmpty(reading, ready);
    check(fixture.device.result_reads == reads_before + 1, "Previously consumed result replayed");
}

void testBothInterruptPolarities()
{
    for (const uint8_t mux : {uint8_t{0x01}, uint8_t{0x11}}) {
        Fixture fixture;
        fixture.initialize();
        fixture.device.registers[0x0030] = mux;
        fixture.device.updateIrq();
        distance_sensor::Reading reading;
        bool ready = false;
        check(fixture.driver.poll(reading, ready) == ESP_OK && !ready, "Inactive IRQ treated as ready");
        fixture.device.publish(9, 300);
        check(fixture.driver.poll(reading, ready) == ESP_OK && ready && reading.valid,
              "Data-ready ignored for a supported interrupt polarity");
        check(fixture.driver.poll(reading, ready) == ESP_OK, "Post-ack polarity read failed");
        checkEmpty(reading, ready);
    }
}

void testOpticalStatusAndRangeSafety()
{
    Fixture fixture;
    fixture.initialize();
    distance_sensor::Reading reading;
    bool ready = false;
    for (uint8_t status = 0; status < 32; ++status) {
        fixture.device.publish(static_cast<uint8_t>(0x80 | status), 100);
        check(fixture.driver.poll(reading, ready) == ESP_OK && ready, "Invalid optical frame must still be consumed");
        check(reading.valid == (status == 9), "ST range-status failure accepted as a valid near sample");
        check(reading.raw_status == (0x80 | status), "Raw status was normalized instead of preserved");
        check(!fixture.device.pending, "Invalid optical frame left interrupt latched");
    }
    for (const uint16_t mm : {uint16_t{0}, uint16_t{39}, uint16_t{40}, uint16_t{4000}, uint16_t{4001}, uint16_t{65535}}) {
        fixture.device.publish(9, mm);
        check(fixture.driver.poll(reading, ready) == ESP_OK && ready, "Boundary frame was not consumed");
        check(reading.valid == (mm == 40 || mm == 4000), "40..4000 mm inclusive validity boundary broken");
        check(mm != 0 || !reading.valid, "Zero distance could trigger a near-distance alert");
    }
}

void testReadAndAcknowledgeFailures()
{
    for (int scenario = 0; scenario < 4; ++scenario) {
        Fixture fixture;
        fixture.initialize();
        fixture.device.publish(9, 100);
        const bool fail_read = scenario < 2;
        const uint16_t reg = scenario == 0 ? 0x0030 : scenario == 1 ? 0x0089 : 0x0086;
        const bool committed_ack = scenario == 3;
        fixture.device.failNext(fail_read, reg, ESP_ERR_TIMEOUT, committed_ack);
        distance_sensor::Reading reading;
        bool ready = false;
        poison(reading, ready);
        check(fixture.driver.poll(reading, ready) == ESP_ERR_TIMEOUT, "Poll swallowed an I2C read/ack error");
        checkEmpty(reading, ready);

        check(fixture.driver.poll(reading, ready) == ESP_OK, "Poll did not recover after one-shot failure");
        if (committed_ack) {
            checkEmpty(reading, ready);  // An ambiguous write may lose a frame, but must not replay it.
        } else {
            check(ready && reading.valid && reading.millimeters == 100, "Unpublished pending frame not recovered");
        }
        check(fixture.driver.poll(reading, ready) == ESP_OK, "Final no-data poll failed");
        checkEmpty(reading, ready);
    }
}

void testStopAndFailedStop()
{
    for (const bool fail_stop : {false, true}) {
        Fixture fixture;
        fixture.initialize();
        if (fail_stop) fixture.device.failNext(false, 0x0087, ESP_ERR_TIMEOUT);
        check(fixture.driver.stop() == (fail_stop ? ESP_ERR_TIMEOUT : ESP_OK), "Stop result not propagated");
        check(fail_stop || !fixture.device.ranging, "Successful stop left sensor running");
        const size_t calls_after_stop = fixture.device.transfers.size();
        distance_sensor::Reading reading;
        bool ready = true;
        check(fixture.driver.poll(reading, ready) == ESP_ERR_INVALID_STATE, "Poll after stop still active");
        checkEmpty(reading, ready);
        check(fixture.driver.stop() == ESP_OK && fixture.device.transfers.size() == calls_after_stop,
              "Repeated stop must not access the forgotten borrowed handle");
    }
}

}  // namespace

int main()
{
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"uninitialized/null device", testUninitializedAndNull},
        {"wrong model is read-only", testWrongModelIsReadOnly},
        {"bounded boot timeout", testBootDeadline},
        {"bounded calibration timeout/cleanup", testCalibrationDeadlineAndStop},
        {"Long 50 ms / calibrated 100 ms initialization", testInitializationProgramsRequestedMode},
        {"every initialization I2C failure / invalid oscillator", testEveryInitializationTransactionFailure},
        {"fresh result / no replay", testFreshFrameAndNoReplay},
        {"both IRQ polarities", testBothInterruptPolarities},
        {"all status codes / distance safety boundaries", testOpticalStatusAndRangeSafety},
        {"read/ack failures including ambiguous committed ack", testReadAndAcknowledgeFailures},
        {"stop / failed stop", testStopAndFailedStop},
    };
    size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " driver boundary tests passed (checks active under NDEBUG)\n";
    return 0;
}
