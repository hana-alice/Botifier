#include "ble_link.h"

#include <windows.h>
#include <winrt/base.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using stackchan_demo::Channel;

namespace {
std::atomic<bool> cancelled = false;
BOOL WINAPI on_control(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        cancelled = true;
        return TRUE;
    }
    return FALSE;
}

void pause_for(int milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!cancelled && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(50ms);
    }
    if (cancelled) throw std::runtime_error("Demo cancelled; attempting the neutral pose.");
}

std::string avatar(int left, int right, int mouth, int left_rotation = 0, int right_rotation = 0)
{
    return "{\"leftEye\":{\"weight\":" + std::to_string(left) +
        ",\"rotation\":" + std::to_string(left_rotation) +
        "},\"rightEye\":{\"weight\":" + std::to_string(right) +
        ",\"rotation\":" + std::to_string(right_rotation) +
        "},\"mouth\":{\"weight\":" + std::to_string(mouth) + "}}";
}

std::string motion(int yaw, int pitch)
{
    // Official servo angles use tenths of a degree. Never use the PWM 'rotate' field.
    if (yaw < -80 || yaw > 80 || pitch < 410 || pitch > 490) {
        throw std::runtime_error("Pose exceeds this demo's small movement range.");
    }
    return "{\"yawServo\":{\"angle\":" + std::to_string(yaw) +
        ",\"speed\":300},\"pitchServo\":{\"angle\":" + std::to_string(pitch) + ",\"speed\":300}}";
}

std::string rgb(const std::string& color)
{
    return "{\"leftRgbColor\":\"" + color +
        "\",\"leftRgbDuration\":0.3,\"rightRgbColor\":\"" + color + "\",\"rightRgbDuration\":0.3}";
}

struct Frame {
    std::string label;
    std::string face;
    std::string pose;
    std::string light;
    int hold_ms;
};

std::vector<Frame> showcase()
{
    return {
        {"Hello / blue", avatar(100, 100, 0), motion(0, 450), rgb("#12404D"), 1400},
        {"Happy / green / look left", avatar(72, 72, 35, 1550, -1550), motion(-80, 450), rgb("#164D24"), 1800},
        {"Wink / violet / look right", avatar(100, 0, 20), motion(80, 450), rgb("#38184D"), 1600},
        {"Nod down", avatar(72, 72, 35, 1550, -1550), motion(0, 410), rgb("#4D3012"), 900},
        {"Nod up", avatar(100, 100, 35), motion(0, 490), rgb("#4D3012"), 900},
        {"Neutral / lights off", avatar(100, 100, 0), motion(0, 450), rgb("#000000"), 900}
    };
}

std::vector<Frame> reminder()
{
    return {
        {"Time to take a break / amber", avatar(72, 72, 35, 1550, -1550), motion(0, 450), rgb("#4D2810"), 1200},
        {"Reminder nod down", avatar(72, 72, 35, 1550, -1550), motion(0, 410), rgb("#4D2810"), 900},
        {"Reminder nod up", avatar(100, 100, 35), motion(0, 490), rgb("#4D2810"), 900},
        {"Neutral / lights off", avatar(100, 100, 0), motion(0, 450), rgb("#000000"), 900}
    };
}

std::string address_string(uint64_t address)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0') << std::setw(12) << address;
    return out.str();
}

uint64_t parse_address(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == ':' || c == '-'; }), text.end());
    uint64_t address = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), address, 16);
    if (text.size() != 12 || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || address == 0) {
        throw std::runtime_error("Address must contain exactly 12 hexadecimal digits.");
    }
    return address;
}

void send(stackchan_demo::BleLink* link, Channel channel, const std::string& json)
{
    const char* name = channel == Channel::avatar ? "avatar" : channel == Channel::motion ? "motion" : "rgb";
    if (json.size() > 512) throw std::runtime_error("A demo payload exceeds the verified long-write limit.");
    if (link) link->send(channel, json);
    std::cout << (link ? "GATT_OK " : "DRY_RUN ") << name << ' ' << json << '\n';
}

void play(stackchan_demo::BleLink* link, const std::vector<Frame>& frames)
{
    for (const auto& frame : frames) {
        if (cancelled) throw std::runtime_error("Demo cancelled.");
        std::cout << "SCENE " << frame.label << '\n';
        send(link, Channel::avatar, frame.face);
        send(link, Channel::rgb, frame.light);
        send(link, Channel::motion, frame.pose);
        if (link) pause_for(frame.hold_ms);
    }
}
} // namespace

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    std::string demo = "showcase";
    int seconds = 10;
    bool dry_run = false;
    std::optional<uint64_t> address;
    std::unique_ptr<stackchan_demo::BleLink> link;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--dry-run") dry_run = true;
            else if (arg == "--help") {
                std::cout << "stackchan_demo --demo showcase|timer|both|scan [--seconds 3..3600] [--address MAC] [--dry-run]\n";
                return 0;
            } else if ((arg == "--demo" || arg == "--seconds" || arg == "--address") && i + 1 < argc) {
                const std::string value = argv[++i];
                if (arg == "--demo") demo = value;
                else if (arg == "--address") address = parse_address(value);
                else {
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                        throw std::runtime_error("Seconds must be an integer.");
                    }
                }
            } else throw std::runtime_error("Unknown or incomplete option: " + arg);
        }
        if (demo != "showcase" && demo != "timer" && demo != "both" && demo != "scan") {
            throw std::runtime_error("Unknown demo. Choose showcase, timer, both or scan.");
        }
        if (seconds < 3 || seconds > 3600) throw std::runtime_error("Seconds must be in 3..3600.");
        SetConsoleCtrlHandler(on_control, TRUE);
        if (!dry_run) {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            if (!address || demo == "scan") {
                std::cout << "Scanning for StackChan for 10 seconds. Open DANCE on the robot.\n";
                const auto devices = stackchan_demo::scan(10s);
                for (const auto& device : devices) {
                    std::cout << "DEVICE " << address_string(device.address) << ' ' << device.name << '\n';
                }
                if (demo == "scan") {
                    if (devices.empty()) std::cout << "No StackChan advertisement found. No commands sent.\n";
                    return devices.empty() ? 2 : 0;
                }
                if (devices.size() != 1) {
                    throw std::runtime_error(devices.empty()
                        ? "No StackChan found. Open DANCE and disconnect the phone's BLE controller, then retry."
                        : "Several StackChan devices found. Select yours with --address MAC.");
                }
                address = devices.front().address;
            }
            std::cout << "Connecting to " << address_string(*address) << " (DANCE service only).\n";
            link = std::make_unique<stackchan_demo::BleLink>(*address);
            std::cout << "Connected, MTU=" << link->mtu() << ". First motion targets yaw=0, pitch=45 degrees.\n";
        } else {
            std::cout << "DRY RUN: no Bluetooth access or hardware commands.\n";
            if (demo == "scan") return 0;
        }
        if (demo == "showcase" || demo == "both") play(link.get(), showcase());
        if (demo == "timer" || demo == "both") {
            play(link.get(), {{"Focus timer started / blue", avatar(100, 100, 0), motion(0, 450), rgb("#102D40"), 300}});
            for (int remaining = seconds; remaining > 0; --remaining) {
                std::cout << "TIMER " << remaining << '\n';
                if (!dry_run) pause_for(1000);
            }
            play(link.get(), reminder());
        }
        std::cout << (dry_run ? "DRY_RUN_COMPLETE" : "DEMO_COMMANDS_COMPLETE")
                  << ": physical visual confirmation is separate from GATT acknowledgements.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
    } catch (const winrt::hresult_error& error) {
        std::cerr << "WINDOWS_ERROR " << winrt::to_string(error.message()) << '\n';
    }
    if (link) {
        try {
            send(link.get(), Channel::avatar, avatar(100, 100, 0));
            send(link.get(), Channel::rgb, rgb("#000000"));
            send(link.get(), Channel::motion, motion(0, 450));
        } catch (...) {
            std::cerr << "Could not confirm neutral pose after the interruption.\n";
        }
    }
    return 1;
}
