#include <Arduino.h>
#include <TFT_eSPI.h> // Seeed_GFX; provides EPaper when the ePaper combo is selected

EPaper epaper;

const uint16_t COLORS[6] = {TFT_BLACK, TFT_WHITE, TFT_RED,
                            TFT_YELLOW, TFT_GREEN, TFT_BLUE};
const char *COLOR_NAMES[6] = {"BLACK", "WHITE", "RED",
                              "YELLOW", "GREEN", "BLUE"};

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("ee02-playground: task 2 — six-color test pattern");

    epaper.begin();
    Serial.printf("panel: %d x %d\n", epaper.width(), epaper.height());

    epaper.fillScreen(TFT_WHITE);

    // Six vertical bars, 200 px each across the 1200 px width
    for (int i = 0; i < 6; i++) {
        epaper.fillRect(i * 200, 0, 200, 1200, COLORS[i]);
    }

    // Labels under the bars, on white background
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    for (int i = 0; i < 6; i++) {
        epaper.drawString(COLOR_NAMES[i], i * 200 + 20, 1250, 4);
    }
    epaper.drawString("ee02-playground / milestone 1", 20, 1400, 4);

    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done");
}

void loop() {}
