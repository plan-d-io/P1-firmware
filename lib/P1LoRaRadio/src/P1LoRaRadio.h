#pragma once

// SX1276 glue for the LoRa bridge. Not part of P1LoRa: the host tests
// must build without RadioLib. The caller owns the object. No heap.

#include <P1LoRa.h>

#include <RadioLib.h>

#include <stddef.h>
#include <stdint.h>

namespace p1lora {

struct RadioPins {
    int sck;
    int miso;
    int mosi;
    int nss;
    int irq;
    int rst;
    int dio1;
};

enum class Rx : int8_t { None = 0, Packet = 1, Crc = -1 };

// PA_BOOST is the only output wired to the antenna on these boards.
// RadioLib treats anything below 2 dBm as the RFO pin, which is not connected.
constexpr int8_t kPaBoostMinDbm = 2;

class Radio {
public:
    explicit Radio(const RadioPins& pins);

    bool begin(const Profile& profile, uint8_t step, int8_t power_dbm);
    bool configure(const Profile& profile, uint8_t step, int8_t power_dbm);
    bool set_sync_word(uint8_t sync);
    // Blocks until the TX-done interrupt. measured_us is the time spent in transmit().
    bool send(const uint8_t* data, size_t len, uint32_t& measured_us);
    bool listen();
    Rx poll(uint8_t* buf, size_t cap, size_t& len, float& rssi, float& snr);
    int16_t last_status() const { return status_; }

private:
    int8_t boosted(const Profile& profile, int8_t power_dbm) const;

    RadioPins pins_;
    Module module_;
    SX1276 radio_;
    int16_t status_ = 0;
    bool began_ = false;
};

}  // namespace p1lora
