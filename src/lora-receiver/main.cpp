#include <Arduino.h>

#include "../app/app.h"

const p1app::Product& receiver_product();

void setup()
{
    p1app::app_setup(receiver_product());
}

void loop()
{
    p1app::app_loop();
}
