#include <Adafruit_Fingerprint.h>  // Include Adafruit fingerprint sensor library
#include <TFT_eSPI.h>
#include <EEPROM.h>
#include <Wire.h>    // I2C communication library (RTC + EEPROM)
#include <RTClib.h>  // Library for DS3231 RTC
#include "Adafruit_Keypad.h"
#include "logo.h"
#include "logoHeader.h"

// define your specific keypad here via PID
#define KEYPAD_PID3844

#define R1 27
#define R2 5
#define R3 18
#define R4 19
#define C1 32
#define C2 33
#define C3 25
#define C4 26

#define SDA 21
#define SCL 22

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define EEPROM_ADDR 0x57  // I2C address of external EEPROM on RTC module (AT24C32/64)

// leave this import after the above configuration
#include "keypad_config.h"

TFT_eSPI tft = TFT_eSPI();
Adafruit_Keypad customKeypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);  //initialize an instance of class NewKeypad
RTC_DS3231 rtc;                                                                                  // Create RTC object for DS3231

/* ================= HARDWARE ================= */
HardwareSerial FingerSerial(2);              // Create HardwareSerial object using UART2 of ESP32
Adafruit_Fingerprint finger(&FingerSerial);  // Create fingerprint object using UART2

/* ================= GLOBAL ================= */
uint8_t fingerID;  // Variable to store fingerprint ID (1–127)

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

String keyBuffer = "";  // stores pressed keys
int enteredNumber = 0;  // final integer value

/* ================= CORE 0 TASK ================= */
/* Fingerprint Sensor Logic */
void FingerprintSensorTask(void *pvParameters) {
  FingerSerial.begin(57600, SERIAL_8N1, 16, 17);         // Start UART2 at 57600 baud, RX=16, TX=17
  finger.begin(57600);                                   // Initialize fingerprint sensor communication
  Serial.println("\nAS608 Fingerprint System Started");  // Print startup message

  if (!finger.verifyPassword()) {  // Check if sensor responds correctly
    Serial.println("Fingerprint sensor not detected");
  } else {
    Serial.println("Fingerprint sensor detected");  // Print success message
  }

  logIndex = readLogCount();  // Restore stored log count

  finger.getTemplateCount();              // Get number of stored fingerprints
  Serial.print("Stored fingerprints: ");  // Print label
  Serial.println(finger.templateCount);   // Print number of stored fingerprints

  Serial.println("\nCommands:");             // Print available commands
  Serial.println("e = Enroll");              // Enroll a new fingerprint
  Serial.println("s = Search");              // Search a fingerprint
  Serial.println("d = Delete ID");           // Delete a fingerprint by ID
  Serial.println("c = Count");               // Count stored fingerprints
  Serial.println("x = Delete ALL");          // Delete all fingerprints
  Serial.println("l = List of All Stored");  // List all stored fingerprint IDs
  Serial.println("p = Print Logs");

  while (1) {
    controlThroughSerial();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/* KeyPad Logic */
void KeypadTask(void *pvParameters) {
  customKeypad.begin();

  while (1) {
    customKeypad.tick();

    checkKeypad();

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/* ================= CORE 1 TASK ================= */
/* Display / UI Logic */
void displayTask(void *pvParameters) {

  while (1) {
    // dateAndTimeDisplayOnScreen();
    registratinAndDeleteOptionDisplayOnScreen();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);    // Start USB serial communication at 9600 baud
  Wire.begin(SDA, SCL);  // Initialize I2C (SDA=21, SCL=22)

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found");
  }

  displayInit();
  logoScreen();
  delay(5000);
  logoInHeaderScreen();


  /* -------- CORE 0 : Fingerprint Sensor -------- */
  xTaskCreatePinnedToCore(
    FingerprintSensorTask,
    "Fingerprint Sensor Task",
    4096,
    // 2048,
    NULL,
    3,
    NULL,
    0  // 🔴 CORE 0
  );

  /* -------- CORE 0 : Keypad -------- */
  xTaskCreatePinnedToCore(
    KeypadTask,
    "Keypad Task",
    // 4096,
    2048,
    NULL,
    3,
    NULL,
    0  // 🔴 CORE 0
  );

  /* -------- CORE 1 : DISPLAY -------- */
  xTaskCreatePinnedToCore(
    displayTask,
    "Display Task",
    2048,
    NULL,
    1,
    NULL,
    1  // 🔵 CORE 1
  );
}

/* ================= LOOP ================= */
void loop() {
}

void displayInit() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
}

// ================= LOGO SCREEN =================
void logoScreen() {
  tft.fillScreen(TFT_WHITE);
  int x = (SCREEN_WIDTH - LOGO_WIDTH) / 2;
  int y = (SCREEN_HEIGHT - LOGO_HEIGHT) / 2;
  tft.setSwapBytes(true);  // CRITICAL for RGB565
  tft.pushImage(x, y, SCREEN_WIDTH, SCREEN_HEIGHT, Logo);
}

// ================= LOGO SCREEN =================
void logoInHeaderScreen() {
  tft.fillScreen(TFT_WHITE);
  int x = (SCREEN_WIDTH - LOGO_WIDTH) / 2;
  int y = (SCREEN_HEIGHT - LOGO_HEIGHT) / 2;
  tft.setSwapBytes(true);  // CRITICAL for RGB565
  tft.pushImage(x, y, SCREEN_WIDTH, SCREEN_HEIGHT, LogoHeader);
}

void dateAndTimeDisplayOnScreen() {
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
  DateTime now = rtc.now();

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);

  String timeBuff = String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());  // Create time and date strings
  String dateBuff = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year());

  // Calculate text width
  int timeWidth = tft.textWidth(timeBuff);
  int dateWidth = tft.textWidth(dateBuff);

  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;

  // Set cursor so text is centered
  tft.setCursor(centerX - (timeWidth / 2), centerY - 20);
  tft.print(timeBuff);
  tft.setCursor(centerX - (dateWidth / 2), centerY + 10);
  tft.print(dateBuff);
}

// void registratinAndDeleteOptionDisplayOnScreen() {
//   tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area

//   tft.setTextSize(2);
//   tft.setTextColor(TFT_BLACK, TFT_WHITE);

//   String regBuff = "REGISTRATION";  // Create time and date strings
//   String delBuff = "DELETE";

//   // Calculate text width
//   int timeWidth = tft.textWidth(regBuff);
//   int dateWidth = tft.textWidth(delBuff);

//   // Screen center
//   int centerX = 320 / 2;
//   int centerY = 240 / 2;

//   // tft.fillRoundRect(x, y, w, h, r, TFT_LIGHTGREY);
//   int r = 26 / 2;
//   tft.fillRoundRect(60, 90, 200, 35, r, TFT_LIGHTGREY);

//   // Set cursor so text is centered
//   tft.setCursor(centerX - (timeWidth / 2), centerY - 20);
//   tft.print(regBuff);
//   tft.setCursor(centerX - (dateWidth / 2), centerY + 20);
//   tft.print(delBuff);
// }

void registratinAndDeleteOptionDisplayOnScreen() {
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area

  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);  // <-- NO background color

  String regBuff = "REGISTRATION";
  String delBuff = "DELETE";

  // Calculate text widths
  int regWidth = tft.textWidth(regBuff);
  int delWidth = tft.textWidth(delBuff);

  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;

  int r = 26 / 2;

  // Draw buttons (Light Grey background)
  tft.fillRoundRect(60, 75, 200, 40, r, TFT_LIGHTGREY);   // REG
  tft.fillRoundRect(60, 125, 200, 40, r, TFT_LIGHTGREY);  // DELETE

  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);

  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}


// void checkKeypad() {
//   while (customKeypad.available()) {
//     keypadEvent e = customKeypad.read();
//     Serial.print((char)e.bit.KEY);
//     if (e.bit.EVENT == KEY_JUST_PRESSED) Serial.println(" pressed");
//     else if (e.bit.EVENT == KEY_JUST_RELEASED) Serial.println(" released");
//   }
// }

void checkKeypad() {
  while (customKeypad.available()) {
    keypadEvent e = customKeypad.read();
    char key = (char)e.bit.KEY;
    if (e.bit.EVENT == KEY_JUST_PRESSED) {
      Serial.print(key);
      Serial.println(" pressed");
      // Accept only numeric keys (0–9)
      if (key >= '0' && key <= '9') {
        keyBuffer += key;  // append key
      } else if (key == '#') {
        enteredNumber = keyBuffer.toInt();  // convert to int
        Serial.print("Final number = ");
        Serial.println(enteredNumber);
        keyBuffer = "";
        enteredNumber = 0;
      } else if (key == 'A') {
        Serial.println("Registration Or Delete Button Pressed");
      } else if (key == 'B') {
        Serial.println("UP Arrow Button Pressed");
      } else if (key == 'C') {
        Serial.println("DOWN Arrow Button Pressed");
      } else if (key == 'D') {
        Serial.println("SET Button Pressed");
      } else {
        //pass
      }
    } else if (e.bit.EVENT == KEY_JUST_RELEASED) {
      Serial.print(key);
      Serial.println(" released");
    }
  }
}

void controlThroughSerial() {
  if (Serial.available()) {    // Check if any serial input is available
    char cmd = Serial.read();  // Read the command character

    if (cmd == 'e') {  // If command is enroll
      bool result = searchFinger();
      if (result) {
        Serial.println("Fingerprint Already Enroll");
      } else {
        Serial.println("Enter ID (1-127):");   // Ask user for fingerprint ID
        fingerID = readID();                   // Read ID from serial
        if (fingerID < 1 || fingerID > 127) {  // Validate ID range
          Serial.println("Invalid ID! Try again.");
          return;  // Exit loop iteration
        }
        Serial.println("Enrolling ID: " + String(fingerID));
        // Print enrolling ID
        enrollFinger(fingerID);  // Call enroll function
      }
    }
    if (cmd == 'd') {  // If command is delete
      Serial.println("Enter ID to delete:");
      fingerID = readID();                   // Read ID from serial
      if (fingerID < 1 || fingerID > 127) {  // Validate ID range
        Serial.println("Invalid ID! Try again.");
        return;
      }
      Serial.println("Deleting ID: " + String(fingerID));
      // Print deleting ID
      deleteFinger(fingerID);  // Call delete function
    }
    if (cmd == 's') searchFinger();     // If command is search, call search function
    if (cmd == 'c') countFinger();      // Count fingerprints
    if (cmd == 'x') deleteAllFinger();  // Delete all fingerprints
    if (cmd == 'l') listStoredIDs();    // List all stored fingerprint IDs
    if (cmd == 'p') printLogs();        // Print stored logs
  }
}

/* ================= READ ID ================= */
uint8_t readID() {
  String input = "";  // String to hold user input
  // Clear any leftover data (like '\n' from command)
  while (Serial.available()) {
    Serial.read();  // Flush serial buffer
  }

  // Wait until a NON-empty number is entered
  while (true) {
    while (!Serial.available()) {  // Wait for serial input
      delay(10);
    }
    input = Serial.readStringUntil('\n');  // Read input until newline
    input.trim();                          // Remove spaces and newline
    if (input.length() > 0) {              // If valid input received
      break;
    }
  }
  return input.toInt();  // Convert input to integer and return
}

/* ================= FUNCTIONS ================= */

void listStoredIDs() {
  Serial.println("Stored Fingerprint IDs:");  // Print header
  bool foundAny = false;                      // Flag to track stored IDs

  for (uint8_t id = 1; id <= 127; id++) {  // Loop through all possible IDs
    uint8_t p = finger.loadModel(id);      // Try loading fingerprint model
    if (p == FINGERPRINT_OK) {             // If model exists
      Serial.print("ID ");
      Serial.print(id);
      Serial.println(" exists");
      foundAny = true;  // Mark that at least one ID exists
    }
  }

  if (!foundAny) {
    Serial.println("No fingerprints stored");  // No IDs found
  }
}

void enrollFinger(uint8_t id) {
  int p = -1;  // Variable for sensor response

  Serial.println("Place finger");  // Ask user to place finger
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();  // Capture fingerprint image
  }
  finger.image2Tz(1);                         // Convert image to template buffer 1
  Serial.println("Remove finger");            // Ask user to remove finger
  delay(2000);                                // Wait for finger removal
  Serial.println("Place same finger again");  // Ask for second scan
  while (finger.getImage() != FINGERPRINT_OK)
    ;                  // Wait for image
  finger.image2Tz(2);  // Convert second image to buffer 2
  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Finger mismatch");  // If fingerprints don't match
    return;
  }
  if (finger.storeModel(id) == FINGERPRINT_OK) Serial.println("Fingerprint stored successfully");  // Store success
  else Serial.println("Store failed");                                                             // Store failed
}

bool searchFinger() {
  int p = -1;                                // Variable for sensor response
  Serial.println("Place finger to search");  // Ask user to place finger
  // WAIT until finger is detected
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();  // Capture fingerprint image
    delay(50);
  }
  p = finger.image2Tz(1);  // Convert image to template buffer
  Serial.println("Convert image to template : " + String(p));
  p = finger.fingerSearch();  // Search fingerprint database
  Serial.println("Search database : " + String(p));
  if (p == FINGERPRINT_OK) {
    Serial.print("Found ID: ");
    Serial.print(finger.fingerID);  // Print matched ID
    Serial.print("  Confidence: ");
    Serial.println(finger.confidence);  // Print confidence score
    // storeRTCLog(finger.fingerID);       // Store log if matched
    return true;  // Match found
  } else {
    Serial.println("No match found");  // No match
    return false;
  }
}

void deleteFinger(uint8_t id) {
  if (finger.deleteModel(id) == FINGERPRINT_OK) Serial.println("Fingerprint deleted");  // Delete success
  else Serial.println("Delete failed");                                                 // Delete failed
}

void countFinger() {
  finger.getTemplateCount();  // Get fingerprint count
  Serial.print("Total fingerprints: ");
  Serial.println(finger.templateCount);  // Print count
}

void deleteAllFinger() {
  if (finger.emptyDatabase() == FINGERPRINT_OK)
    Serial.println("All fingerprints deleted");  // Database cleared
  else
    Serial.println("Database clear failed");  // Clear failed
}

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

  writeLog(logIndex, log);  // Write log to EEPROM
  logIndex++;               // Increment log counter
  saveLogCount(logIndex);   // Save updated count

  Serial.println("Log stored in RTC EEPROM");
}

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
