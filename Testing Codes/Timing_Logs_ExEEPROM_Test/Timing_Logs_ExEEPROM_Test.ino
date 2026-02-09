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
#define EEPROM_SIZE 4096
#define LOG_COUNT_ADDR 0
#define LOG_START_ADDR 2
#define MAX_LOGS ((EEPROM_SIZE - LOG_START_ADDR) / LOG_SIZE)

/* ================= GLOBAL ================= */
uint8_t fingerID;
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

uint16_t readLogCount() {
  uint16_t count =
    (eepromReadByte(LOG_COUNT_ADDR) << 8) | eepromReadByte(LOG_COUNT_ADDR + 1);

  if (count > MAX_LOGS) {
    Serial.println("⚠ EEPROM invalid, resetting log count");
    count = 0;
    saveLogCount(0);
  }
  return count;
}

void writeLog(uint16_t index, LogEntry log) {
  if (index >= MAX_LOGS) {
    Serial.println("❌ EEPROM FULL");
    return;
  }

  uint16_t addr = LOG_START_ADDR + (index * LOG_SIZE);
  uint8_t *p = (uint8_t *)&log;

  for (uint8_t i = 0; i < LOG_SIZE; i++) {
    eepromWriteByte(addr + i, p[i]);
  }
}

LogEntry readLog(uint16_t index) {
  LogEntry log;
  uint16_t addr = LOG_START_ADDR + (index * LOG_SIZE);
  uint8_t *p = (uint8_t *)&log;

  for (uint8_t i = 0; i < LOG_SIZE; i++) {
    p[i] = eepromReadByte(addr + i);
  }
  return log;
}

void fullEEPROMErase() {
  Serial.println("FULL EEPROM ERASE START");

  for (uint16_t i = 0; i < EEPROM_SIZE; i++) {
    eepromWriteByte(i, 0xFF);
  }

  logIndex = 0;
  saveLogCount(0);

  Serial.println("✅ EEPROM completely erased");
}


/* ================= STORE RTC LOG ================= */

void storeRTCLog(uint8_t userID) {
  if (logIndex >= MAX_LOGS) {
    Serial.println("❌ Log memory full");
    return;
  }

  DateTime now = rtc.now();
  LogEntry log;

  log.userID = userID;
  log.year = now.year();
  log.month = now.month();
  log.day = now.day();
  log.hour = now.hour();
  log.minute = now.minute();
  log.second = now.second();

  Serial.println("Logging: " + String(userID) + " " + String(log.year) + "-" + String(log.month) + "-" + String(log.day) + " " + String(log.hour) + ":" + String(log.minute) + ":" + String(log.second));

  writeLog(logIndex, log);
  logIndex++;
  saveLogCount(logIndex);

  Serial.println("✅ Log stored");
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

void enrollFinger(uint8_t id) {
  int p = -1;
  Serial.println("Place finger");

  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
  }

  finger.image2Tz(1);
  Serial.println("Remove finger");
  delay(2000);

  Serial.println("Place same finger again");
  while (finger.getImage() != FINGERPRINT_OK)
    ;

  finger.image2Tz(2);

  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Finger mismatch");
    return;
  }

  if (finger.storeModel(id) == FINGERPRINT_OK) Serial.println("Fingerprint stored successfully");
  else Serial.println("Store failed");
}

uint8_t readID() {
  String input = "";

  // Clear any leftover data (like '\n' from command)
  while (Serial.available()) {
    Serial.read();
  }

  // Wait until a NON-empty number is entered
  while (true) {
    while (!Serial.available()) {
      delay(10);
    }

    input = Serial.readStringUntil('\n');
    input.trim();  // remove CR/LF/spaces

    if (input.length() > 0) {
      break;  // valid input received
    }
  }

  return input.toInt();
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

    if (cmd == 'c') {
      fullEEPROMErase();
    }

    if (cmd == 's') searchFingerprint();  // Scan fingerprint
    if (cmd == 'p') printLogs();          // Print stored logs
    if (cmd == 'e') {
      Serial.println("Enter ID (1-127):");
      fingerID = readID();
      if (fingerID < 1 || fingerID > 127) {
        Serial.println("Invalid ID! Try again.");
        return;
      }
      Serial.println("Enrolling ID: " + String(fingerID));
      enrollFinger(fingerID);
    }
  }
}
