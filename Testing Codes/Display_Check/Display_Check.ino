#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <EEPROM.h>

// // Touchscreen pins
// #define XPT2046_IRQ 36
// #define XPT2046_MOSI 32
// #define XPT2046_MISO 39
// #define XPT2046_CLK 25
// #define XPT2046_CS 33

// #define SCREEN_WIDTH 320
// #define SCREEN_HEIGHT 240
// #define FONT_SIZE 2

TFT_eSPI tft = TFT_eSPI();

// SPIClass touchscreenSPI = SPIClass(VSPI);
// XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

void setup() {
  Serial.begin(9600);
  // touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  // touchscreen.begin(touchscreenSPI);
  // touchscreen.setRotation(1);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);
  // tft.setCursor(20, 50);
  tft.setCursor(5, 90);
  tft.print("HELLO EVOLUZN INDIA NAGPUR");
}

void loop() {
  // put your main code here, to run repeatedly:
}
