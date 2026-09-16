#include <Arduino.h>

// Pins from archive/P1-dongle/P1-dongle.ino:
//   HWSERIAL.begin(115200, SERIAL_8N1, 21, 22);
//   TRIGGER 25, HIGH for continuous telegrams.
static constexpr int kP1Rx = 21;
static constexpr int kP1Tx = 22;
static constexpr int kDataRequest = 25;

void setup()
{
    Serial.begin(115200);

    pinMode(kDataRequest, OUTPUT);
    digitalWrite(kDataRequest, HIGH);

    Serial1.begin(115200, SERIAL_8N1, kP1Rx, kP1Tx);

    Serial.println("P1 passthrough");
}

void loop()
{
    uint8_t buf[64];
    size_t n = 0;
    while (n < sizeof(buf)) {
        const int b = Serial1.read();
        if (b < 0) {
            break;
        }
        buf[n++] = static_cast<uint8_t>(b);
    }
    if (n > 0) {
        Serial.write(buf, n);
    }
}
