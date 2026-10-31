#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <esp_err.h>

namespace nfc_sense {

struct Card {
    std::array<uint8_t, 10> uid{};
    size_t uid_length = 0;
};

// A single worker owns init/poll/close. No method starts a task or owns the
// board's shared I2C bus. Repeated successful polls report a held card present.
class Reader {
public:
    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    esp_err_t init();
    // Only ESP_OK makes present meaningful. Errors clear the output card but
    // must not be interpreted as card removal or used to rearm a trigger.
    // A failed poll requires init() before trying another RF operation.
    esp_err_t poll(Card& card, bool& present);
    void close();

private:
    class Impl;
    Impl* impl_ = nullptr;
};

}  // namespace nfc_sense
