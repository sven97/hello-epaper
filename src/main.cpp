#include <Arduino.h>

constexpr uint8_t LED_PIN = 21; // XIAO user LED, active-LOW

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    // USB-CDC needs a moment before prints are visible
    delay(2000);
    Serial.println("ee02-playground: task 1 — serial hello");
}

void loop() {
    digitalWrite(LED_PIN, LOW);  // LED on
    delay(500);
    digitalWrite(LED_PIN, HIGH); // LED off
    delay(500);
    Serial.println("heartbeat");
}
