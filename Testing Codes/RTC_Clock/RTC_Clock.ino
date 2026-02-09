#include <Wire.h>    // I2C communication
#include <RTClib.h>  // RTC library

RTC_DS3231 rtc;  // Create RTC object

void setup() {
  Serial.begin(9600);  // Start serial monitor
  delay(1000);

  Wire.begin(21, 22);  // ESP32 I2C pins (SDA, SCL)

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found");
    // while (1)
    //   ;  // Stop execution
  }

  // rtc.adjust(DateTime(2026, 1, 20, 17, 30, 0));  // year, month, day, hour, minute, second

  // // Check if RTC lost power
  // if (rtc.lostPower()) {
  //   Serial.println("RTC lost power, setting time!");

  //   // // Set RTC time to compile time
  //   // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  //   rtc.adjust(DateTime(2026, 1, 20, 17, 28, 0));  // year, month, day, hour, minute, second
  // }
}

void loop() {
  DateTime now = rtc.now();  // Read current time

  Serial.print("Date: ");
  Serial.print(now.day());
  Serial.print("/");
  Serial.print(now.month());
  Serial.print("/");
  Serial.print(now.year());

  Serial.print("  Time: ");
  Serial.print(now.hour());
  Serial.print(":");
  Serial.print(now.minute());
  Serial.print(":");
  Serial.print(now.second());

  Serial.println();

  delay(1000);  // Update every second
}
