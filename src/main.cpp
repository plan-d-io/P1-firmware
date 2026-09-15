#include <Arduino.h>

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("P1-LoRa");
    Serial.println("pioarduino toolchain test");
}

void loop()
{
    Serial.printf(
        "Uptime: %lu ms | Free heap: %u bytes\n",
        millis(),
        ESP.getFreeHeap()
    );

    delay(1000);
}