#include <Arduino.h>
#include <rtc_utils.h>

void setup() {
    Serial.begin(115200);
    Serial.println();

    uint16_t count = 0;
    rtc_read(&count);

    Serial.println(count);

    count++;
    rtc_write(&count);
}

void loop() {
}