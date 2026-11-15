#include "P1LoRaRadio.h"

#include <SPI.h>

namespace p1lora {

Radio::Radio(const RadioPins& pins)
    : pins_(pins), module_(pins.nss, pins.irq, pins.rst, pins.dio1), radio_(&module_)
{
}

int8_t Radio::boosted(const Profile& profile, int8_t power_dbm) const
{
    int8_t power = clamp_tx_power(profile.id, power_dbm);
    if (power < kPaBoostMinDbm) {
        power = kPaBoostMinDbm;
    }
    return power;
}

bool Radio::begin(const Profile& profile, uint8_t step, int8_t power_dbm)
{
    if (step >= profile.step_count) {
        status_ = RADIOLIB_ERR_INVALID_SPREADING_FACTOR;
        return false;
    }
    // ss = -1 so the default SPI pins (GPIO 12 and 15) stay untouched.
    // GPIO 27 is passed only as MOSI.
    SPI.begin(pins_.sck, pins_.miso, pins_.mosi, -1);
    const float mhz = static_cast<float>(profile.hz) / 1000000.0f;
    const float bw_khz = static_cast<float>(profile.steps[step].bw_hz) / 1000.0f;
    const int8_t power = boosted(profile, power_dbm);
    status_ = radio_.begin(mhz, bw_khz, profile.steps[step].sf, 5, profile.sync_word, power, 8, 0);
    if (status_ != RADIOLIB_ERR_NONE) {
        return false;
    }
    status_ = radio_.explicitHeader();
    if (status_ != RADIOLIB_ERR_NONE) {
        return false;
    }
    status_ = radio_.setCRC(true);
    if (status_ != RADIOLIB_ERR_NONE) {
        return false;
    }
    // RadioLib enables LDRO when the symbol is 16 ms or longer. That matches
    // the airtime formula on every step of both profiles (none lands on exactly 16 ms).
    status_ = radio_.autoLDRO();
    if (status_ != RADIOLIB_ERR_NONE) {
        return false;
    }
    status_ = radio_.setOutputPower(power, false);
    began_ = status_ == RADIOLIB_ERR_NONE;
    return began_;
}

bool Radio::configure(const Profile& profile, uint8_t step, int8_t power_dbm)
{
    if (!began_ || step >= profile.step_count) {
        return false;
    }
    const float bw_khz = static_cast<float>(profile.steps[step].bw_hz) / 1000.0f;
    const int8_t power = boosted(profile, power_dbm);
    status_ = radio_.setSpreadingFactor(profile.steps[step].sf);
    if (status_ != RADIOLIB_ERR_NONE) {
        return false;
    }
    status_ = radio_.setBandwidth(bw_khz);
    if (status_ != RADIOLIB_ERR_NONE) {
        return false;
    }
    status_ = radio_.setOutputPower(power, false);
    return status_ == RADIOLIB_ERR_NONE;
}

bool Radio::set_sync_word(uint8_t sync)
{
    if (!began_) {
        return false;
    }
    status_ = radio_.setSyncWord(sync);
    return status_ == RADIOLIB_ERR_NONE;
}

bool Radio::send(const uint8_t* data, size_t len, uint32_t& measured_us)
{
    if (!began_ || data == nullptr || len == 0) {
        return false;
    }
    const uint32_t started = micros();
    status_ = radio_.transmit(const_cast<uint8_t*>(data), len);
    measured_us = micros() - started;
    return status_ == RADIOLIB_ERR_NONE;
}

bool Radio::listen()
{
    if (!began_) {
        return false;
    }
    status_ = radio_.startReceive();
    return status_ == RADIOLIB_ERR_NONE;
}

Rx Radio::poll(uint8_t* buf, size_t cap, size_t& len, float& rssi, float& snr)
{
    len = 0;
    if (!began_ || buf == nullptr || cap == 0 || digitalRead(pins_.irq) == LOW) {
        return Rx::None;
    }
    const size_t packet = radio_.getPacketLength();
    status_ = radio_.readData(buf, cap);
    rssi = radio_.getRSSI(true);
    snr = radio_.getSNR();
    listen();
    if (status_ == RADIOLIB_ERR_CRC_MISMATCH) {
        return Rx::Crc;
    }
    if (status_ != RADIOLIB_ERR_NONE) {
        return Rx::None;
    }
    len = packet > cap ? cap : packet;
    return Rx::Packet;
}

}  // namespace p1lora
