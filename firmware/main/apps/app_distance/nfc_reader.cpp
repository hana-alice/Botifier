#include "nfc_reader.h"

#include <hal/board/hal_bridge.h>
#include <unit/unit_ST25R3916.hpp>
#include <M5HAL.hpp>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace nfc_sense {
namespace {

constexpr char kTag[] = "distance-nfc";
constexpr uint8_t kAddress = 0x50;
constexpr uint32_t kClockHz = 400000;
constexpr int kTransferTimeoutMs = 20;
constexpr uint32_t kInitBudgetMs = 1500;
constexpr uint32_t kPollBudgetMs = 250;

void fieldGuard()
{
    // NFC-A needs a real field-off interval to reset an ACTIVE card to IDLE.
    // Use 10 ms elapsed time (not a rounded-down 100 Hz FreeRTOS delay), also
    // allowing the newly powered card to settle after field-on.
    const auto until_us = esp_timer_get_time() + 10000;
    while (esp_timer_get_time() < until_us) vTaskDelay(1);
}

using HalError = m5::hal::error::error_t;
template <typename T>
using Result = m5::stl::expected<T, HalError>;
namespace chip = m5::unit::st25r3916;

class NativeBus;

class NativeAccess final : public m5::hal::bus::i2c::I2CMasterAccessor {
public:
    explicit NativeAccess(NativeBus& bus);
    void configure(const m5::hal::bus::I2CMasterAccessConfig& config)
    {
        _access_config = config;
    }
    Result<void> startWrite() override;
    Result<void> startRead() override;
    Result<void> stop() override;
    Result<size_t> write(const uint8_t* data, size_t length) override;
    Result<size_t> readWithLastNackFlag(uint8_t* data, size_t length, bool last_nack) override;

private:
    NativeBus& bus_;
    bool reading_ = false;
};

// The official bool NFC API loses the distinction between RF silence and an
// I2C error. This M5HAL facade latches every transfer failure for the operation.
// Only the device handle is owned; IDF serializes each transfer on the existing
// shared bus. A register prefix is buffered until the combined repeated-START
// read, so no mutex or bus ownership crosses an RF wait or worker delay.
class NativeBus final : public m5::hal::bus::i2c::I2CBus {
public:
    NativeBus() : access_(*this)
    {
        _config.pin_sda = nullptr;
        _config.pin_scl = nullptr;
    }

    ~NativeBus() override
    {
        const auto error = closeDevice();
        if (error != ESP_OK) ESP_LOGE(kTag, "I2C device cleanup failed: %s", esp_err_to_name(error));
    }

    esp_err_t open(i2c_master_bus_handle_t bus)
    {
        if (!bus) return ESP_ERR_INVALID_STATE;
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = kAddress;
        config.scl_speed_hz = kClockHz;
        return i2c_master_bus_add_device(bus, &config, &device_);
    }

    esp_err_t closeDevice()
    {
        if (!device_) return ESP_OK;
        const auto error = i2c_master_bus_rm_device(device_);
        if (error == ESP_OK) device_ = nullptr;
        return error;
    }

    bool attached() const { return device_ != nullptr; }

    void beginOperation(uint32_t budget_ms)
    {
        error_ = ESP_OK;
        pending_size_ = 0;
        irq_seen_ = 0;
        deadline_us_ = esp_timer_get_time() + static_cast<int64_t>(budget_ms) * 1000;
    }

    esp_err_t error() const { return error_; }
    uint32_t irqSeen() const { return irq_seen_; }

    Result<m5::hal::bus::Accessor*> beginAccess(const m5::hal::bus::AccessConfig& config) override
    {
        if (!ready() || active_ || config.getBusType() != m5::hal::types::BusType::I2C) {
            return m5::stl::make_unexpected(fail(ESP_ERR_INVALID_STATE));
        }
        const auto& i2c = static_cast<const m5::hal::bus::I2CMasterAccessConfig&>(config);
        if (i2c.i2c_addr != kAddress || i2c.address_is_10bit || i2c.freq != kClockHz) {
            return m5::stl::make_unexpected(fail(ESP_ERR_INVALID_ARG));
        }
        active_ = true;
        access_.configure(i2c);
        return static_cast<m5::hal::bus::Accessor*>(&access_);
    }

    HalError endAccess(m5::hal::bus::Accessor* access) override
    {
        if (access != &access_ || !active_) return fail(ESP_ERR_INVALID_STATE);
        active_ = false;
        return error_ == ESP_OK ? HalError::OK : HalError::I2C_BUS_ERROR;
    }

    bool ready()
    {
        if (error_ != ESP_OK) return false;
        if (!device_) {
            fail(ESP_ERR_INVALID_STATE);
            return false;
        }
        if (esp_timer_get_time() >= deadline_us_) {
            fail(ESP_ERR_TIMEOUT);
            return false;
        }
        return true;
    }

    HalError fail(esp_err_t error)
    {
        if (error_ == ESP_OK) error_ = error;
        pending_size_ = 0;
        return error_ == ESP_ERR_TIMEOUT ? HalError::TIMEOUT_ERROR : HalError::I2C_BUS_ERROR;
    }

    size_t pendingSize() const { return pending_size_; }

    Result<size_t> append(const uint8_t* data, size_t length)
    {
        if (!ready()) return m5::stl::make_unexpected(HalError::I2C_BUS_ERROR);
        if ((!data && length) || length > pending_.size() - pending_size_) {
            return m5::stl::make_unexpected(fail(ESP_ERR_INVALID_SIZE));
        }
        if (length) std::memcpy(pending_.data() + pending_size_, data, length);
        pending_size_ += length;
        return length;
    }

    Result<void> flush()
    {
        if (!ready()) return m5::stl::make_unexpected(HalError::I2C_BUS_ERROR);
        if (!pending_size_) return {};
        // Discard interrupts from the previous poll immediately before WUPA.
        if (pending_size_ == 1 && pending_[0] == chip::command::CMD_TRANSMIT_WUPA) irq_seen_ = 0;
        const auto error = i2c_master_transmit(device_, pending_.data(), pending_size_, timeoutMs());
        pending_size_ = 0;
        if (error != ESP_OK) return m5::stl::make_unexpected(fail(error));
        return {};
    }

    Result<size_t> receive(uint8_t* data, size_t length)
    {
        if (!ready()) return m5::stl::make_unexpected(HalError::I2C_BUS_ERROR);
        if (!data || !length) return m5::stl::make_unexpected(fail(ESP_ERR_INVALID_ARG));
        const auto error = pending_size_
            ? i2c_master_transmit_receive(device_, pending_.data(), pending_size_, data, length, timeoutMs())
            : i2c_master_receive(device_, data, length, timeoutMs());
        if (error != ESP_OK) return m5::stl::make_unexpected(fail(error));
        // readInterrupts() consumes hardware flags inside the bool library API.
        // Retain those same flags to confirm an actual no-response completion.
        if (pending_size_ == 1) {
            const auto reg = pending_[0];
            if (reg == (chip::command::REG_MAIN_INTERRUPT | chip::OP_READ_REGISTER) && length == 2) {
                irq_seen_ |= (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16);
            } else if (reg == (chip::command::REG_ERROR_AND_WAKEUP_INTERRUPT | chip::OP_READ_REGISTER) && length == 1) {
                irq_seen_ |= static_cast<uint32_t>(data[0]) << 8;
            } else if (reg == (chip::command::REG_PASSIVE_TARGET_INTERRUPT | chip::OP_READ_REGISTER) && length == 1) {
                irq_seen_ |= data[0];
            }
        }
        pending_size_ = 0;
        return length;
    }

private:
    int timeoutMs() const
    {
        const auto remaining_ms = (deadline_us_ - esp_timer_get_time() + 999) / 1000;
        return static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(kTransferTimeoutMs, remaining_ms)));
    }

    i2c_master_dev_handle_t device_ = nullptr;
    NativeAccess access_;
    std::array<uint8_t, 514> pending_{}; // ST25R3916's 512-byte FIFO plus command prefix.
    size_t pending_size_ = 0;
    int64_t deadline_us_ = 0;
    esp_err_t error_ = ESP_OK;
    uint32_t irq_seen_ = 0;
    bool active_ = false;
};

NativeAccess::NativeAccess(NativeBus& bus)
    : I2CMasterAccessor(bus, m5::hal::bus::I2CMasterAccessConfig{}), bus_(bus)
{
}

Result<void> NativeAccess::startWrite()
{
    if (bus_.pendingSize()) return m5::stl::make_unexpected(bus_.fail(ESP_ERR_INVALID_STATE));
    reading_ = false;
    return {};
}

Result<void> NativeAccess::startRead()
{
    reading_ = true;
    return {};
}

Result<void> NativeAccess::stop()
{
    return reading_ ? Result<void>{} : bus_.flush();
}

Result<size_t> NativeAccess::write(const uint8_t* data, size_t length)
{
    return bus_.append(data, length);
}

Result<size_t> NativeAccess::readWithLastNackFlag(uint8_t* data, size_t length, bool /*last_nack*/)
{
    return bus_.receive(data, length);
}

}  // namespace

class Reader::Impl {
public:
    NativeBus bus;
    m5::unit::UnitST25R3916 unit{kAddress};
    bool initialized = false;

    bool healthy()
    {
        uint8_t control = 0, auxiliary = 0;
        constexpr uint8_t enabled = chip::regval::en | chip::regval::tx_en | chip::regval::rx_en;
        return unit.readOperationControl(control) && (control & enabled) == enabled &&
               unit.readAuxiliaryDisplay(auxiliary) && (auxiliary & chip::regval::osc_ok) != 0;
    }
};

Reader::Reader() = default;

Reader::~Reader()
{
    close();
    // close() retains an IDF device whose removal failed so a live caller can
    // retry. The bus destructor makes one final cleanup attempt on destruction.
    delete impl_;
}

esp_err_t Reader::init()
{
    close();
    if (impl_) return ESP_FAIL;
    impl_ = new (std::nothrow) Impl;
    if (!impl_) return ESP_ERR_NO_MEM;
    auto error = impl_->bus.open(hal_bridge::board_get_i2c_bus());
    if (error != ESP_OK) {
        close();
        return error;
    }
    impl_->bus.beginOperation(kInitBudgetMs);
    auto config = impl_->unit.config();
    config.mode = m5::nfc::NFC::A;
    config.using_irq = false;
    config.emulation = false;
    impl_->unit.config(config);
    if (!impl_->unit.assign(static_cast<m5::hal::bus::Bus*>(&impl_->bus)) ||
        !impl_->unit.begin() || !impl_->healthy() || !impl_->bus.ready()) {
        error = impl_->bus.error();
        ESP_LOGE(kTag, "ST25R3916 initialization failed: %s", esp_err_to_name(error == ESP_OK ? ESP_FAIL : error));
        close();
        return ESP_FAIL;
    }
    impl_->initialized = true;
    return ESP_OK;
}

esp_err_t Reader::poll(Card& card, bool& present)
{
    card = {};
    present = false;
    if (!impl_ || !impl_->initialized) return ESP_ERR_INVALID_STATE;
    auto& unit = impl_->unit;
    auto& bus = impl_->bus;
    const auto failed = [this]() -> esp_err_t {
        // Failed selection can leave a card ACTIVE, where it ignores WUPA.
        // Reinitialize (including field reset) before accepting an absence.
        impl_->initialized = false;
        return ESP_FAIL;
    };
    bus.beginOperation(kPollBudgetMs);
    if (!impl_->healthy() || !bus.ready()) return failed();

    m5::nfc::a::PICC picc{};
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (unit.nfcaWakeup(picc.atqa)) break;
        if (!bus.ready()) return failed();
        // The library's 4 ms software wait can end just before the hardware's
        // no-response flag. Allow one scheduler tick, then read actual flags.
        vTaskDelay(pdMS_TO_TICKS(2) + 1);
        uint32_t final_irq = 0;
        if (!unit.readInterrupts(final_irq) || !impl_->healthy() || !bus.ready()) return failed();
        const auto irq = bus.irqSeen();
        const auto completion = chip::regval::I_txe32 | chip::regval::I_nre32;
        if ((irq & completion) != completion || chip::has_irq32_error(irq) ||
            (irq & (chip::regval::I_rxs32 | chip::regval::I_rxe32 | chip::regval::I_col32))) {
            return failed();
        }
        if (attempt == 1) return ESP_OK;

        // A locally transmitted HLTA does not prove the card received it. A
        // held card left ACTIVE ignores WUPA just like an absent card. Reset
        // the RF field and require another clean no-response completion before
        // reporting removal; a present card can now answer from IDLE.
        if (!unit.disableField() || !bus.ready()) return failed();
        fieldGuard();
        uint8_t control = 0, auxiliary = 0;
        constexpr uint8_t field_control = chip::regval::tx_en | chip::regval::rx_en;
        constexpr uint8_t field_status = chip::regval::tx_on | chip::regval::rx_on;
        if (!unit.readOperationControl(control) || (control & field_control) != 0 ||
            !unit.readAuxiliaryDisplay(auxiliary) || (auxiliary & field_status) != 0 || !bus.ready()) {
            return failed();
        }
        if (!unit.enableField() || !bus.ready()) return failed();
        fieldGuard();
        if (!impl_->healthy() || !bus.ready()) return failed();
    }
    bool completed = false;
    for (uint8_t level = 1; level <= 3 && !completed; ++level) {
        if (!unit.nfcaSelectWithAnticollision(completed, picc, level) || !bus.ready()) return failed();
    }
    if (!completed || (picc.size != 4 && picc.size != 7 && picc.size != 10)) return failed();
    // HALT is a volatile RF state, not a card-data write. WUPA on the next poll
    // wakes this same held card, allowing the caller to distinguish removal.
    if (!unit.nfcaHlt() || !bus.ready()) return failed();
    std::copy_n(picc.uid, picc.size, card.uid.begin());
    card.uid_length = picc.size;
    present = true;
    return ESP_OK;
}

void Reader::close()
{
    if (!impl_) return;
    impl_->initialized = false;
    if (impl_->bus.attached() && impl_->unit.adapter()) {
        impl_->bus.beginOperation(100);
        if (!impl_->unit.disableField() || impl_->bus.error() != ESP_OK) {
            ESP_LOGW(kTag, "Could not confirm NFC RF field shutdown");
        }
    }
    const auto error = impl_->bus.closeDevice();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "I2C device removal failed: %s", esp_err_to_name(error));
        return;
    }
    delete impl_;
    impl_ = nullptr;
}

}  // namespace nfc_sense
