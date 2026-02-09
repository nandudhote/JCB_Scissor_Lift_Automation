#include <Wire.h>                  // I2C communication library (RTC + EEPROM)
#include <RTClib.h>                // Library for DS3231 RTC
#include <Adafruit_Fingerprint.h>  // Library for AS608 fingerprint sensor

/* ================= RTC & EEPROM ================= */
RTC_DS3231 rtc;  // Create RTC object for DS3231

#define EEPROM_ADDR 0x50  // I2C address of external EEPROM on RTC module (AT24C32/64)

/* ================= Fingerprint ================= */

HardwareSerial FingerSerial(2);  // Use ESP32 UART2 for fingerprint sensor

Adafruit_Fingerprint finger(&FingerSerial);  // Create fingerprint object using UART2

/* ================= EEPROM LOG STRUCTURE ================= */

// Structure to store one attendance log entry
struct LogEntry {
  uint8_t userID;  // Fingerprint ID
  uint16_t year;   // Year (e.g. 2026)
  uint8_t month;   // Month (1–12)
  uint8_t day;     // Day (1–31)
  uint8_t hour;    // Hour (0–23)
  uint8_t minute;  // Minute (0–59)
  uint8_t second;  // Second (0–59)
};

#define LOG_SIZE sizeof(LogEntry)  // Size of one log entry (calculated automatically)
#define MAX_LOGS 500               // Maximum number of logs (depends on EEPROM size)
#define LOG_COUNT_ADDR 4090        // EEPROM address where total log count is stored (near end of EEPROM)

uint16_t logIndex = 0;  // Variable to track current log index

/* ================= EEPROM LOW LEVEL WRITE ================= */

// Write a single byte to EEPROM
void eepromWriteByte(uint16_t addr, uint8_t data) {
  Wire.beginTransmission(EEPROM_ADDR);  // Start I2C communication with EEPROM
  Wire.write(addr >> 8);                // Send high byte of address
  Wire.write(addr & 0xFF);              // Send low byte of address
  Wire.write(data);                     // Send data byte
  Wire.endTransmission();               // End transmission
  delay(5);                             // EEPROM write delay (important)
}

/* ================= EEPROM LOW LEVEL READ ================= */

// Read a single byte from EEPROM
uint8_t eepromReadByte(uint16_t addr) {
  Wire.beginTransmission(EEPROM_ADDR);  // Start I2C communication
  Wire.write(addr >> 8);                // High byte of address
  Wire.write(addr & 0xFF);              // Low byte of address
  Wire.endTransmission();               // End transmission

  Wire.requestFrom(EEPROM_ADDR, 1);  // Request 1 byte from EEPROM
  return Wire.read();                // Return received byte
}

/* ================= SAVE LOG COUNT ================= */

// Store total number of logs in EEPROM
void saveLogCount(uint16_t count) {
  eepromWriteByte(LOG_COUNT_ADDR, count >> 8);        // Store high byte
  eepromWriteByte(LOG_COUNT_ADDR + 1, count & 0xFF);  // Store low byte
}

/* ================= READ LOG COUNT ================= */

// Read total log count from EEPROM
uint16_t readLogCount() {
  return (eepromReadByte(LOG_COUNT_ADDR) << 8) |  // Read high byte
         eepromReadByte(LOG_COUNT_ADDR + 1);      // Read low byte
}

/* ================= WRITE ONE LOG ================= */

// Write one LogEntry structure to EEPROM
void writeLog(uint16_t index, LogEntry log) {
  uint16_t addr = index * LOG_SIZE;  // Calculate EEPROM address
  uint8_t *p = (uint8_t *)&log;      // Convert structure to byte array

  // Write each byte of the structure
  for (uint8_t i = 0; i < LOG_SIZE; i++) {
    eepromWriteByte(addr + i, p[i]);
  }
}

/* ================= READ ONE LOG ================= */

// Read one LogEntry structure from EEPROM
LogEntry readLog(uint16_t index) {
  LogEntry log;                      // Create structure variable
  uint16_t addr = index * LOG_SIZE;  // Calculate EEPROM address
  uint8_t *p = (uint8_t *)&log;      // Pointer to structure bytes

  // Read each byte from EEPROM
  for (uint8_t i = 0; i < LOG_SIZE; i++) {
    p[i] = eepromReadByte(addr + i);
  }
  return log;  // Return filled structure
}

/* ================= STORE RTC LOG ================= */

// Store fingerprint match log with RTC timestamp
void storeRTCLog(uint8_t userID) {
  DateTime now = rtc.now();  // Read current date & time from RTC
  LogEntry log;              // Create log structure

  log.userID = userID;        // Store fingerprint ID
  log.year = now.year();      // Store year
  log.month = now.month();    // Store month
  log.day = now.day();        // Store day
  log.hour = now.hour();      // Store hour
  log.minute = now.minute();  // Store minute
  log.second = now.second();  // Store second

  Serial.println("Loging Details : " + String(userID) + " : " + String(now.year()) + ":" + String(now.month()) + ":" + String(now.day()) + ":" + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()));

  writeLog(logIndex, log);  // Write log to EEPROM
  logIndex++;               // Increment log counter
  saveLogCount(logIndex);   // Save updated count

  Serial.println("Log stored in RTC EEPROM");
}

/* ================= PRINT ALL LOGS ================= */

// Print all stored logs via Serial Monitor
void printLogs() {
  Serial.println("---- ATTENDANCE LOGS ----");

  for (uint16_t i = 0; i < logIndex; i++) {
    LogEntry log = readLog(i);  // Read log from EEPROM

    Serial.print("ID ");
    Serial.print(log.userID);
    Serial.print("  ");

    Serial.print(log.year);
    Serial.print("-");
    Serial.print(log.month);
    Serial.print("-");
    Serial.print(log.day);
    Serial.print(" ");

    Serial.print(log.hour);
    Serial.print(":");
    Serial.print(log.minute);
    Serial.print(":");
    Serial.println(log.second);
  }
}

/* ================= FINGERPRINT SEARCH ================= */

// Scan fingerprint and log attendance if matched
void searchFingerprint() {
  int p = -1;

  Serial.println("Place finger");

  // Wait until fingerprint image is captured
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    delay(50);
  }

  finger.image2Tz(1);         // Convert image to template
  p = finger.fingerSearch();  // Search in sensor database

  if (p == FINGERPRINT_OK) {
    Serial.print("Match ID: ");
    Serial.println(finger.fingerID);
    storeRTCLog(finger.fingerID);  // Store log if matched
  } else {
    Serial.println("No match found");
  }
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(9600);  // Initialize Serial Monitor
  Wire.begin(21, 22);  // Initialize I2C (SDA=21, SCL=22)

  FingerSerial.begin(57600, SERIAL_8N1, 16, 17);  // Fingerprint UART
  finger.begin(57600);                            // Start fingerprint sensor

  if (!finger.verifyPassword()) {  // Verify sensor connection
    Serial.println("Fingerprint sensor error");
    while (1)
      ;  // Stop execution if error
  }

  if (!rtc.begin()) {  // Initialize RTC
    Serial.println("RTC not found");
    while (1)
      ;  // Stop execution if error
  }

  logIndex = readLogCount();  // Restore stored log count

  Serial.println("System Ready");
  Serial.println("s = Scan Finger");
  Serial.println("p = Print Logs");
}

/* ================= LOOP ================= */

void loop() {
  if (Serial.available()) {    // Check for Serial input
    char cmd = Serial.read();  // Read command

    if (cmd == 's') searchFingerprint();  // Scan fingerprint
    if (cmd == 'p') printLogs();          // Print stored logs
  }
}







// #include <Wire.h>
// #include <RTClib.h>
// #include <Adafruit_Fingerprint.h>

// /* ================= RTC & EEPROM ================= */
// RTC_DS3231 rtc;
// #define EEPROM_ADDR 0x57

// /* ================= Fingerprint ================= */
// HardwareSerial FingerSerial(2);
// Adafruit_Fingerprint finger(&FingerSerial);

// /* ================= EEPROM LOG ================= */
// struct LogEntry {
//   uint8_t userID;
//   uint16_t year;
//   uint8_t month;
//   uint8_t day;
//   uint8_t hour;
//   uint8_t minute;
//   uint8_t second;
// };

// #define LOG_SIZE sizeof(LogEntry)
// #define MAX_LOGS 500
// #define LOG_COUNT_ADDR 4090  // last bytes of EEPROM

// uint16_t logIndex = 0;

// /* ================= EEPROM LOW LEVEL ================= */
// void eepromWriteByte(uint16_t addr, uint8_t data) {
//   Wire.beginTransmission(EEPROM_ADDR);
//   Wire.write(addr >> 8);
//   Wire.write(addr & 0xFF);
//   Wire.write(data);
//   Wire.endTransmission();
//   delay(5);
// }

// uint8_t eepromReadByte(uint16_t addr) {
//   Wire.beginTransmission(EEPROM_ADDR);
//   Wire.write(addr >> 8);
//   Wire.write(addr & 0xFF);
//   Wire.endTransmission();
//   Wire.requestFrom(EEPROM_ADDR, 1);
//   return Wire.read();
// }

// /* ================= LOG COUNT ================= */
// void saveLogCount(uint16_t count) {
//   eepromWriteByte(LOG_COUNT_ADDR, count >> 8);
//   eepromWriteByte(LOG_COUNT_ADDR + 1, count & 0xFF);
// }

// uint16_t readLogCount() {
//   return (eepromReadByte(LOG_COUNT_ADDR) << 8) | eepromReadByte(LOG_COUNT_ADDR + 1);
// }

// /* ================= WRITE LOG ================= */
// void writeLog(uint16_t index, LogEntry log) {
//   uint16_t addr = index * LOG_SIZE;
//   uint8_t *p = (uint8_t *)&log;

//   for (uint8_t i = 0; i < LOG_SIZE; i++) {
//     eepromWriteByte(addr + i, p[i]);
//   }
// }

// /* ================= READ LOG ================= */
// LogEntry readLog(uint16_t index) {
//   LogEntry log;
//   uint16_t addr = index * LOG_SIZE;
//   uint8_t *p = (uint8_t *)&log;

//   for (uint8_t i = 0; i < LOG_SIZE; i++) {
//     p[i] = eepromReadByte(addr + i);
//   }
//   return log;
// }

// /* ================= STORE RTC LOG ================= */
// void storeRTCLog(uint8_t userID) {
//   DateTime now = rtc.now();

//   LogEntry log;
//   log.userID = userID;
//   log.year = now.year();
//   log.month = now.month();
//   log.day = now.day();
//   log.hour = now.hour();
//   log.minute = now.minute();
//   log.second = now.second();

//   writeLog(logIndex, log);
//   logIndex++;
//   saveLogCount(logIndex);

//   Serial.println("Log stored in RTC EEPROM");
// }

// /* ================= PRINT ALL LOGS ================= */
// void printLogs() {
//   Serial.println("---- ATTENDANCE LOGS ----");

//   for (uint16_t i = 0; i < logIndex; i++) {
//     LogEntry log = readLog(i);

//     Serial.print("ID ");
//     Serial.print(log.userID);
//     Serial.print("  ");

//     Serial.print(log.year);
//     Serial.print("-");
//     Serial.print(log.month);
//     Serial.print("-");
//     Serial.print(log.day);
//     Serial.print(" ");

//     Serial.print(log.hour);
//     Serial.print(":");
//     Serial.print(log.minute);
//     Serial.print(":");
//     Serial.println(log.second);
//   }
// }

// /* ================= FINGER SEARCH ================= */
// void searchFingerprint() {
//   int p = -1;
//   Serial.println("Place finger");

//   while (p != FINGERPRINT_OK) {
//     p = finger.getImage();
//     delay(50);
//   }

//   finger.image2Tz(1);
//   p = finger.fingerSearch();

//   if (p == FINGERPRINT_OK) {
//     Serial.print("Match ID: ");
//     Serial.println(finger.fingerID);
//     storeRTCLog(finger.fingerID);
//   } else {
//     Serial.println("No match found");
//   }
// }

// /* ================= SETUP ================= */
// void setup() {
//   Serial.begin(9600);
//   Wire.begin(21, 22);

//   FingerSerial.begin(57600, SERIAL_8N1, 16, 17);
//   finger.begin(57600);

//   if (!finger.verifyPassword()) {
//     Serial.println("Fingerprint sensor error");
//     while (1)
//       ;
//   }

//   if (!rtc.begin()) {
//     Serial.println("RTC not found");
//     while (1)
//       ;
//   }

//   logIndex = readLogCount();

//   Serial.println("System Ready");
//   Serial.println("s = Scan Finger");
//   Serial.println("p = Print Logs");
// }

// /* ================= LOOP ================= */
// void loop() {
//   if (Serial.available()) {
//     char cmd = Serial.read();

//     if (cmd == 's') searchFingerprint();
//     if (cmd == 'p') printLogs();
//   }
// }
