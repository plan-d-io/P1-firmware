#include <Arduino.h>

#include "../app/app.h"

const p1app::Product& dongle_product();

void setup()
{
    p1app::app_setup(dongle_product());
}

void loop()
{
    p1app::app_loop();
}
