#include "ble_link.h"

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>

namespace stackchan_demo {
namespace {

using namespace std::chrono_literals;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage::Streams;

const winrt::guid dance_service{"e2e5e5e0-1234-5678-1234-56789abcdef0"};
const std::array<winrt::guid, 3> characteristic_ids{
    winrt::guid{"e2e5e5e2-1234-5678-1234-56789abcdef0"},
    winrt::guid{"e2e5e5e1-1234-5678-1234-56789abcdef0"},
    winrt::guid{"e2e5e5e4-1234-5678-1234-56789abcdef0"},
};
constexpr auto operation_timeout = 15s;

std::runtime_error windows_error(const char* action, const winrt::hresult_error& error)
{
    return std::runtime_error(std::string(action) + ": " + winrt::to_string(error.message()) +
                              " (HRESULT " + std::to_string(error.code().value) + ")");
}

template<typename Async>
auto await_result(const Async& operation, const char* action)
{
    try {
        if (operation.wait_for(operation_timeout) == AsyncStatus::Started) {
            // Cancellation cannot stop every Windows Bluetooth connection attempt.
            // The caller still returns within the deadline and closes its session.
            try { operation.Cancel(); } catch (const winrt::hresult_error&) {}
            throw std::runtime_error(std::string(action) + " timed out after 15 seconds");
        }
        return operation.GetResults();
    } catch (const winrt::hresult_error& error) {
        throw windows_error(action, error);
    }
}

template<typename Result>
void check_gatt(const Result& result, const char* action)
{
    if (result.Status() == GattCommunicationStatus::Success) {
        return;
    }
    std::string reason;
    switch (result.Status()) {
    case GattCommunicationStatus::Unreachable: reason = "device unreachable"; break;
    case GattCommunicationStatus::AccessDenied: reason = "access denied"; break;
    case GattCommunicationStatus::ProtocolError:
        reason = "ATT protocol error";
        if (const auto code = result.ProtocolError()) {
            reason += " " + std::to_string(code.Value());
        }
        break;
    default: reason = "GATT status " + std::to_string(static_cast<int>(result.Status())); break;
    }
    throw std::runtime_error(std::string(action) + ": " + reason);
}

struct ScanState {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::uint64_t, Device> devices;
    bool accepting = true;
    bool stopped = false;
    BluetoothError stop_error = BluetoothError::Success;
    std::exception_ptr event_error;
};

} // namespace

std::vector<Device> scan(std::chrono::seconds duration)
{
    if (duration <= 0s) {
        throw std::invalid_argument("Scan duration must be positive");
    }

    // Each callback owns its state. Revocation and Stop do not need to assume
    // that an already dispatched callback has finished before this function exits.
    const auto state = std::make_shared<ScanState>();
    BluetoothLEAdvertisementWatcher watcher;
    watcher.ScanningMode(BluetoothLEScanningMode::Active);
    const auto received = watcher.Received(winrt::auto_revoke,
        [state](const auto&, const BluetoothLEAdvertisementReceivedEventArgs& args) {
            try {
                const auto advertisement = args.Advertisement();
                auto name = winrt::to_string(advertisement.LocalName());
                auto lowercase = name;
                std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                bool matches = lowercase.find("stackchan") != std::string::npos;
                for (const auto& uuid : advertisement.ServiceUuids()) {
                    matches = matches || uuid == dance_service;
                }
                if (!matches) {
                    return;
                }
                const auto address = args.BluetoothAddress();
                std::lock_guard lock(state->mutex);
                if (state->accepting) {
                    auto [entry, inserted] = state->devices.try_emplace(address, Device{address, name});
                    if (!inserted && !name.empty()) {
                        entry->second.name = std::move(name);
                    }
                }
            } catch (...) {
                std::lock_guard lock(state->mutex);
                state->event_error = std::current_exception();
                state->changed.notify_all();
            }
        });
    const auto stopped = watcher.Stopped(winrt::auto_revoke,
        [state](const auto&, const BluetoothLEAdvertisementWatcherStoppedEventArgs& args) {
            std::lock_guard lock(state->mutex);
            try {
                state->stop_error = args.Error();
            } catch (...) {
                state->event_error = std::current_exception();
            }
            state->stopped = true;
            state->changed.notify_all();
        });

    try {
        watcher.Start();
        std::unique_lock lock(state->mutex);
        const bool ended_early = state->changed.wait_for(lock, duration,
            [&] { return state->stopped || state->event_error; });
        state->accepting = false;
        lock.unlock();
        watcher.Stop();
        lock.lock();
        if (!state->changed.wait_for(lock, 3s, [&] { return state->stopped; })) {
            throw std::runtime_error("Bluetooth scanner did not stop within 3 seconds");
        }
        if (state->event_error) {
            std::rethrow_exception(state->event_error);
        }
        if (state->stop_error != BluetoothError::Success) {
            throw std::runtime_error("Bluetooth scan failed (BluetoothError " +
                std::to_string(static_cast<int>(state->stop_error)) + ")");
        }
        if (ended_early) {
            throw std::runtime_error("Bluetooth scanning stopped before the requested duration");
        }
        std::vector<Device> devices;
        devices.reserve(state->devices.size());
        for (const auto& [address, device] : state->devices) {
            devices.push_back(device);
        }
        return devices;
    } catch (...) {
        {
            std::lock_guard lock(state->mutex);
            state->accepting = false;
        }
        try { watcher.Stop(); } catch (const winrt::hresult_error&) {}
        throw;
    }
}

struct BleLink::Impl {
    BluetoothLEDevice device{nullptr};
    GattSession session{nullptr};
    GattDeviceService service{nullptr};
    std::array<GattCharacteristic, 3> characteristics{nullptr, nullptr, nullptr};
    mutable std::mutex mutex;
    bool write_failed = false;

    ~Impl()
    {
        characteristics.fill(nullptr);
        if (service) {
            try { service.Close(); } catch (const winrt::hresult_error&) {}
        }
        if (session) {
            try { session.MaintainConnection(false); } catch (const winrt::hresult_error&) {}
            try { session.Close(); } catch (const winrt::hresult_error&) {}
        }
        if (device) {
            try { device.Close(); } catch (const winrt::hresult_error&) {}
        }
    }
};

BleLink::BleLink(std::uint64_t address) : impl_(std::make_unique<Impl>())
{
    if (address == 0) {
        throw std::invalid_argument("Bluetooth address must not be zero");
    }
    try {
        impl_->device = await_result(BluetoothLEDevice::FromBluetoothAddressAsync(address),
                                    "Open Bluetooth device");
        if (!impl_->device) {
            throw std::runtime_error("Bluetooth device unavailable; scan first and select its current address");
        }
        impl_->session = await_result(GattSession::FromDeviceIdAsync(impl_->device.BluetoothDeviceId()),
                                     "Open GATT session");
        if (!impl_->session || !impl_->session.CanMaintainConnection()) {
            throw std::runtime_error("This Bluetooth device cannot maintain a GATT connection");
        }
        impl_->session.MaintainConnection(true);
        const auto services = await_result(impl_->device.GetGattServicesForUuidAsync(
            dance_service, BluetoothCacheMode::Uncached), "Discover DANCE service");
        check_gatt(services, "Discover DANCE service");
        if (services.Services().Size() != 1) {
            throw std::runtime_error("Expected one DANCE service; select DANCE mode on StackChan");
        }
        impl_->service = services.Services().GetAt(0);
        for (std::size_t index = 0; index < characteristic_ids.size(); ++index) {
            const auto result = await_result(impl_->service.GetCharacteristicsForUuidAsync(
                characteristic_ids[index], BluetoothCacheMode::Uncached), "Discover DANCE characteristic");
            check_gatt(result, "Discover DANCE characteristic");
            if (result.Characteristics().Size() != 1) {
                throw std::runtime_error("Required DANCE characteristic is missing or ambiguous");
            }
            const auto characteristic = result.Characteristics().GetAt(0);
            if ((characteristic.CharacteristicProperties() & GattCharacteristicProperties::Write) ==
                GattCharacteristicProperties::None) {
                throw std::runtime_error("DANCE characteristic does not support writing with a response");
            }
            impl_->characteristics[index] = characteristic;
        }
    } catch (const winrt::hresult_error& error) {
        throw windows_error("Initialize DANCE connection", error);
    }
}

BleLink::~BleLink() = default;

void BleLink::send(Channel channel, const std::string& json)
{
    if (json.empty() || json.size() > 512) {
        throw std::invalid_argument("DANCE JSON must contain 1 to 512 UTF-8 bytes");
    }
    std::size_t index;
    switch (channel) {
    case Channel::avatar: index = 0; break;
    case Channel::motion: index = 1; break;
    case Channel::rgb: index = 2; break;
    default: throw std::invalid_argument("Unknown DANCE channel");
    }
    std::lock_guard lock(impl_->mutex);
    if (impl_->write_failed) {
        throw std::runtime_error("A previous DANCE write failed; reconnect before sending again");
    }
    try {
        DataWriter writer;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
        writer.WriteBytes({bytes, bytes + json.size()});
        // Keep a complete JSON value for firmware 1.5.1 compatibility. Windows
        // performs ATT long writes if necessary; no custom fragment fallback.
        const auto result = await_result(impl_->characteristics[index].WriteValueWithResultAsync(
            writer.DetachBuffer(), GattWriteOption::WriteWithResponse), "Write DANCE JSON");
        check_gatt(result, "Write DANCE JSON");
    } catch (const winrt::hresult_error& error) {
        impl_->write_failed = true;
        throw windows_error("Write DANCE JSON", error);
    } catch (...) {
        impl_->write_failed = true;
        throw;
    }
}

std::uint16_t BleLink::mtu() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->session.MaxPduSize();
}

} // namespace stackchan_demo
