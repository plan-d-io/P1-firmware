// Board check for a LilyGO TTGO T3 v1.6.1. Reports the chip, flash, I2C
// addresses and the SX1276 version register. The radio is only ever read.
// Nothing is written to a radio register, and the radio never transmits.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <P1LoRa.h>

#include <esp_app_format.h>
#include <esp_flash.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

namespace {

// Pins from the T3 v1.6.1 map. GPIO 27 is the radio SPI MOSI and is passed
// only to SPI.begin(); it is never configured as a plain GPIO.
constexpr int kRadioSck = 5;
constexpr int kRadioMiso = 19;
constexpr int kRadioMosi = 27;
constexpr int kRadioCs = 18;
constexpr int kRadioReset = 23;
constexpr uint32_t kSpiHz = 8000000;

constexpr int kI2cSda = 21;
constexpr int kI2cScl = 22;

constexpr int kLed = 25;     // onboard LED, active HIGH
constexpr int kButton = 4;   // external button, active HIGH, pull-down
constexpr uint32_t kBlinkMs = 500;
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kRadioPeriodMs = 10000;

constexpr uint8_t kRegVersion = 0x42;
constexpr uint8_t kExpectedVersion = 0x12;

bool g_spi_ready = false;
uint32_t g_led_at = 0;
bool g_led_on = false;
bool g_button_ready = false;
bool g_button_stable = false;
bool g_button_level = false;
uint32_t g_button_change = 0;
uint32_t g_radio_at = 0;

const char* flashModeName(FlashMode_t mode)
{
    switch (mode) {
    case FM_QIO:
        return "QIO";
    case FM_QOUT:
        return "QOUT";
    case FM_DIO:
        return "DIO";
    case FM_DOUT:
        return "DOUT";
    case FM_FAST_READ:
        return "FAST_READ";
    case FM_SLOW_READ:
        return "SLOW_READ";
    default:
        return "unknown";
    }
}

const char* resetReasonName(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_EXT:
        return "external pin";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "unknown";
    }
}

void printBanner()
{
    Serial.println("ttgo-boardtest");
    Serial.printf("build: %s %s\n", __DATE__, __TIME__);

    const uint16_t rev = ESP.getChipRevision();
    Serial.printf("chip: %s rev %u (%u.%u), %u cores, %u MHz\n",
                  ESP.getChipModel(),
                  static_cast<unsigned>(rev),
                  static_cast<unsigned>(rev / 100),
                  static_cast<unsigned>(rev % 100),
                  static_cast<unsigned>(ESP.getChipCores()),
                  static_cast<unsigned>(ESP.getCpuFreqMHz()));

    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
        Serial.printf("mac: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        Serial.println("mac: read failed");
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_image_header_t header = {};
    bool header_ok = false;
    if (running != nullptr) {
        const esp_err_t err = esp_flash_read(esp_flash_default_chip, &header,
                                              running->address, sizeof(header));
        header_ok = (err == ESP_OK && header.magic == ESP_IMAGE_HEADER_MAGIC);
        if (!header_ok) {
            Serial.printf("flash configured: unavailable (header read %s, magic 0x%02X)\n",
                          esp_err_to_name(err), header.magic);
        }
    } else {
        Serial.println("flash configured: unavailable (no running partition)");
    }
    if (header_ok) {
        Serial.printf("flash configured: %u bytes, mode %s, speed %u Hz (image header)\n",
                      static_cast<unsigned>(ESP.magicFlashChipSize(header.spi_size)),
                      flashModeName(ESP.magicFlashChipMode(header.spi_mode)),
                      static_cast<unsigned>(ESP.magicFlashChipSpeed(header.spi_speed)));
    }

    uint32_t physical = 0;
    const esp_err_t size_err = esp_flash_get_physical_size(esp_flash_default_chip, &physical);
    if (size_err == ESP_OK) {
        Serial.printf("flash detected: %u bytes\n", static_cast<unsigned>(physical));
    } else {
        Serial.printf("flash detected: unavailable (%s)\n", esp_err_to_name(size_err));
    }
    Serial.printf("flash running: mode %s, %u MHz\n",
                  flashModeName(ESP.getFlashChipMode()),
                  static_cast<unsigned>(ESP.getFlashFrequencyMHz()));

    Serial.printf("psram: %u bytes\n", static_cast<unsigned>(ESP.getPsramSize()));

    const esp_reset_reason_t reset = esp_reset_reason();
    Serial.printf("reset: %s (%d)\n", resetReasonName(reset), static_cast<int>(reset));

    if (running != nullptr) {
        Serial.printf("partition: %s addr=0x%08X size=%u\n",
                      running->label, static_cast<unsigned>(running->address),
                      static_cast<unsigned>(running->size));
    } else {
        Serial.println("partition: none");
    }

    Serial.printf("heap: %u\n", static_cast<unsigned>(ESP.getFreeHeap()));
}

void i2cScan()
{
    Serial.println("i2c: scan 0x08-0x77");
    if (!Wire.begin(kI2cSda, kI2cScl)) {
        Serial.println("i2c: begin failed");
        Serial.println("i2c: 0x3C absent");
        return;
    }

    bool any = false;
    bool oled = false;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("i2c: 0x%02X\n", addr);
            any = true;
            if (addr == 0x3C) {
                oled = true;
            }
        }
    }
    if (!any) {
        Serial.println("i2c: none");
    }
    Serial.println(oled ? "i2c: 0x3C present" : "i2c: 0x3C absent");
}

// SX1276 read: mode 0, MSB first, CS low, address with bit 7 clear, then one
// dummy byte that clocks the reply in. The dummy byte is not a register write.
void radioReset()
{
    pinMode(kRadioReset, OUTPUT);
    digitalWrite(kRadioReset, LOW);
    delay(10);
    digitalWrite(kRadioReset, HIGH);
    delay(10);
}

uint8_t readRegVersion()
{
    digitalWrite(kRadioCs, LOW);
    SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
    SPI.transfer(kRegVersion & 0x7F);
    const uint8_t value = SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(kRadioCs, HIGH);
    return value;
}

void printRadio()
{
    if (!g_spi_ready) {
        Serial.println("radio: SPI begin failed");
        return;
    }
    const uint8_t value = readRegVersion();
    Serial.printf("radio: RegVersion=0x%02X expected 0x%02X %s\n",
                  value, kExpectedVersion,
                  value == kExpectedVersion ? "OK" : "MISMATCH");
}

void pollLed()
{
    const uint32_t now = millis();
    if (now - g_led_at < kBlinkMs) {
        return;
    }
    g_led_at = now;
    g_led_on = !g_led_on;
    digitalWrite(kLed, g_led_on ? HIGH : LOW);
}

void pollButton()
{
    const bool raw = digitalRead(kButton) == HIGH;
    const uint32_t now = millis();
    if (!g_button_ready) {
        g_button_stable = raw;
        g_button_level = raw;
        g_button_change = now;
        g_button_ready = true;
        return;
    }
    if (raw != g_button_stable) {
        g_button_stable = raw;
        g_button_change = now;
        return;
    }
    if (g_button_stable != g_button_level && (now - g_button_change) >= kDebounceMs) {
        g_button_level = g_button_stable;
        Serial.println(g_button_level ? "button: pressed" : "button: released");
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);

    printBanner();
    i2cScan();

    pinMode(kRadioCs, OUTPUT);
    digitalWrite(kRadioCs, HIGH);
    radioReset();
    // ss = -1: chip select stays a plain GPIO. Default SPI pins would claim
    // GPIO 12 and 15, which are left alone.
    g_spi_ready = SPI.begin(kRadioSck, kRadioMiso, kRadioMosi, -1);
    digitalWrite(kRadioCs, HIGH);
    printRadio();

    const char* crypto = p1lora::crypto_self_test();
    if (crypto == nullptr) {
        Serial.println("crypto: self-test OK");
    } else {
        Serial.printf("crypto: self-test FAILED %s\n", crypto);
    }

    Serial.println("BOARDTEST DONE");

    pinMode(kLed, OUTPUT);
    digitalWrite(kLed, LOW);
    pinMode(kButton, INPUT_PULLDOWN);
    const uint32_t now = millis();
    g_led_at = now;
    g_radio_at = now;
}

void loop()
{
    pollLed();
    pollButton();
    const uint32_t now = millis();
    if (now - g_radio_at >= kRadioPeriodMs) {
        g_radio_at = now;
        printRadio();
    }
}
