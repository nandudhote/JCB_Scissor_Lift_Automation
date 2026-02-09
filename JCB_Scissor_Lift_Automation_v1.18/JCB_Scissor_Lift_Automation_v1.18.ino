#include <Adafruit_Fingerprint.h>  // Include Adafruit fingerprint sensor library
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <EEPROM.h>
#include <Wire.h>    // I2C communication library (RTC + EEPROM)
#include <RTClib.h>  // Library for DS3231 RTC
#include "Adafruit_Keypad.h"
#include "logo.h"
#include "logoHeader.h"

// define your specific keypad here via PID
#define KEYPAD_PID3844

/* ================= KEYPAD PINS ================= */
#define R1 27
#define R2 5
#define R3 18
#define R4 19
#define C1 32
#define C2 33
#define C3 25
#define C4 26

/* ================= RELAY PINS ================= */
#define Relay 23
#define button 35

/* ================= DISPALY DIMENSION ================= */
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

/* ================= I2C EEPROM ================= */
#define eepromAddr 0x50  // I2C address of external EEPROM on RTC module (AT24C32/64)
#define SDA 21
#define SCL 22

/* ================= PASSWORD ================= */
unsigned long newAdminPassword = 1234;
unsigned long defaultAdminPassword = 1234;

// leave this import after the above configuration
#include "keypad_config.h"

/* ================= HARDWARE ================= */
Preferences prefs;
TFT_eSPI tft = TFT_eSPI();
Adafruit_Keypad customKeypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);  //initialize an instance of class NewKeypad
RTC_DS3231 rtc;                                                                                  // Create RTC object for DS3231

HardwareSerial FingerSerial(2);              // Create HardwareSerial object using UART2 of ESP32
Adafruit_Fingerprint finger(&FingerSerial);  // Create fingerprint object using UART2

// Structure to store one attendance log entry
struct LogEntry {
  uint32_t employeeID;  // Employee ID (e.g. 5678)
  uint16_t year;        // Year (e.g. 2026)
  uint8_t month;        // Month (1–12)
  uint8_t day;          // Day (1–31)
  uint8_t hour;         // Hour (0–23)
  uint8_t minute;       // Minute (0–59)
  uint8_t second;       // Second (0–59)
};

/* ================= GLOBAL ================= */
uint8_t fingerID = 0;     // Variable to store fingerprint ID (1–127)
uint32_t employeeID = 0;  // Variable to store employee ID

enum FingerState {
  FP_idle,
  FP_waitFinger,
  FP_imageConvert,
  FP_search,
  FP_done
};

FingerState fingerState = FP_idle;

unsigned long fpStartTime = 0;
const unsigned long FP_timeout = 10000;  // 10 sec

struct SplitData {
  String indexOneData;
  String indexTwoData;
  String indexThreeData;
};

/* ==== For Loges ====*/
#define logSize sizeof(LogEntry)  // Size of one log entry (calculated automatically)
#define eepromSize 4096
#define logCountAddr 0  // EEPROM address where total log count is stored (near end of EEPROM)
#define logStartAddr 2
#define maxLogs ((eepromSize - logStartAddr) / logSize)  // Maximum number of logs (depends on EEPROM size)

uint32_t logIndex = 0;  // Variable to track current log index

String keyBuffer = "";
int enteredNumber = 0;

/*==== User Setup ====*/
bool menuActive = false;
bool modeSelected = false;
bool waitingForPassword = false;
bool waitingForID = false;
bool isRegisterMode = false;
bool isDeleteMode = false;

/* ===== LONG PRESS (#) DETECTION ===== */
unsigned long hashPressStart = 0;
bool hashPressed = false;
bool adminTriggered = false;
bool adminMenuActive = false;
bool adminModeSelected = false;
bool waitingForAdminPassword = false;
bool waitingForAdminID = false;
bool isFingerprintMode = false;
bool isPasswordMode = false;
bool waitingForPreviousePasswordForConformation = false;
bool waitingForNewPassword = false;
unsigned long HASH_HOLD_TIME = 5000;  // 5 seconds

/* ===== LONG PRESS (*) DETECTION ===== */
unsigned long starPressStart = 0;
bool starPressed = false;
bool passwordResetDone = false;

unsigned long starResetTime = 15000;  // 15 seconds

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
  unsigned long lastRTCUpdate = 0;

  while (1) {
    if (!menuActive && !modeSelected && !waitingForPassword && !waitingForID && !isRegisterMode && !isDeleteMode && !adminMenuActive && !waitingForNewPassword) {
      // Update time every 1 second
      if (millis() - lastRTCUpdate >= 1000) {
        lastRTCUpdate = millis();
        dateAndTimeDisplayOnScreen();
        bool state = digitalRead(button);
        if (!state) {
          digitalWrite(Relay, LOW);
          Serial.println("Relay OFF");
        }
        controlThroughSerial();
      }

      // Fingerprint runs continuously
      searchFingerNonBlocking();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);    // Start USB serial communication at 9600 baud
  Wire.begin(SDA, SCL);  // Initialize I2C (SDA=21, SCL=22)
  pinMode(Relay, OUTPUT);
  pinMode(button, INPUT);


  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found");
  }

  FingerSerial.begin(57600, SERIAL_8N1, 16, 17);         // Start UART2 at 57600 baud, RX=16, TX=17
  finger.begin(57600);                                   // Initialize fingerprint sensor communication
  Serial.println("\nAS608 Fingerprint System Started");  // Print startup message

  if (!finger.verifyPassword()) {  // Check if sensor responds correctly
    Serial.println("Fingerprint sensor not detected");
  } else {
    Serial.println("Fingerprint sensor detected");  // Print success message
  }

  logIndex = readLogCount();  // Restore stored log count

  /* ---- Load password on boot ---- */
  newAdminPassword = loadAdminPassword();
  Serial.print("Current ");
  Serial.println("Admin Password: " + String(newAdminPassword));

  displayInit();
  logoScreen();
  delay(5000);
  logoInHeaderScreen();

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

/* ---- TFT Display Init ---- */
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

/* ---- Display Date And Time On Screen ---- */
void dateAndTimeDisplayOnScreen() {
  // tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
  DateTime now = rtc.now();
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  char timeBuff[9];   // HH:MM:SS
  char dateBuff[11];  // DD/MM/YYYY
  // Format with leading zeros
  sprintf(timeBuff, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  sprintf(dateBuff, "%02d/%02d/%04d", now.day(), now.month(), now.year());
  // Calculate text width
  int timeWidth = tft.textWidth(timeBuff);
  int dateWidth = tft.textWidth(dateBuff);
  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;
  // Display centered
  tft.setCursor(centerX - (timeWidth / 2), centerY - 20);
  tft.print(timeBuff);
  tft.setCursor(centerX - (dateWidth / 2), centerY + 10);
  tft.print(dateBuff);
}

/* ---- Display Registration And Delete Option On Screen ---- */
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
  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);
  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}

/* ---- Display Registration And Delete Option with Registration Select Option On Screen ---- */
void registratinAndDeleteOptionDisplayOnlyRegisrationOnScreen() {
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
  tft.fillRoundRect(60, 75, 200, 40, r, TFT_LIGHTGREY);  // REG
  tft.fillRoundRect(60, 125, 200, 40, r, TFT_WHITE);     // DELETE
  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);
  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}

/* ---- Display Registration And Delete Option with Delete Select Option On Screen ---- */
void registratinAndDeleteOptionDisplayOnlyDeletOnScreen() {
  // tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
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
  tft.fillRoundRect(60, 75, 200, 40, r, TFT_WHITE);       // REG
  tft.fillRoundRect(60, 125, 200, 40, r, TFT_LIGHTGREY);  // DELETE
  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);
  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}

/* ---- Display Messages On Screen ---- */
void displayMassegesOnScreen(String msg) {
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);  // <-- NO background color
  int regWidth = tft.textWidth(msg);
  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;
  int r = 26 / 2;
  // Center text inside buttons
  // tft.setCursor(centerX - (regWidth / 2), 90);
  tft.setCursor(5, 90);
  tft.print(msg);
}

/* ---- Display Admin Page with FINGERPRINT and PASSWORD option On Screen ---- */
void adminMenuDisplayOnScreen() {
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);  // <-- NO background color
  String regBuff = "FINGERPRINT";
  String delBuff = "PASSWORD";
  // Calculate text widths
  int regWidth = tft.textWidth(regBuff);
  int delWidth = tft.textWidth(delBuff);
  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;
  int r = 26 / 2;
  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);
  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}

/* ---- Display FINGERPRINT And PASSWORD Option with FINGERPRINT Select Option On Screen ---- */
void fingerprintAndpasswordOptionDisplayAndFingerprintModeSelectOnScreen() {
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);  // <-- NO background color
  String regBuff = "FINGERPRINT";
  String delBuff = "PASSWORD";
  // Calculate text widths
  int regWidth = tft.textWidth(regBuff);
  int delWidth = tft.textWidth(delBuff);
  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;
  int r = 26 / 2;
  // Draw buttons (Light Grey background)
  tft.fillRoundRect(60, 75, 200, 40, r, TFT_LIGHTGREY);  // Fingerprint
  tft.fillRoundRect(60, 125, 200, 40, r, TFT_WHITE);     // password
  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);
  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}

/* ---- Display FINGERPRINT And PASSWORD Option with PASSWORD Select Option On Screen ---- */
void fingerprintAndpasswordOptionDisplayAndPasswordModeSelectOnScreen() {
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);  // <-- NO background color
  String regBuff = "FINGERPRINT";
  String delBuff = "PASSWORD";
  // Calculate text widths
  int regWidth = tft.textWidth(regBuff);
  int delWidth = tft.textWidth(delBuff);
  // Screen center
  int centerX = 320 / 2;
  int centerY = 240 / 2;
  int r = 26 / 2;
  // Draw buttons (Light Grey background)
  tft.fillRoundRect(60, 75, 200, 40, r, TFT_WHITE);       // Fingerprint
  tft.fillRoundRect(60, 125, 200, 40, r, TFT_LIGHTGREY);  // Password
  // Center text inside buttons
  tft.setCursor(centerX - (regWidth / 2), 90);
  tft.print(regBuff);
  tft.setCursor(centerX - (delWidth / 2), 140);
  tft.print(delBuff);
}

/* ================= PASSWORD MASK FUNCTION ================= */
String maskPassword(int length) {
  String masked = "";
  for (int i = 0; i < length; i++) {
    masked += '*';
  }
  return masked;
}

/* ================= Display Password or Ids On Screen ================= */
void displayPasswordOrIdMassegesOnScreen(String msg) {
  tft.fillRect(0, 110, 320, 30, TFT_WHITE);
  tft.setCursor(130, 125);
  tft.print(msg);
}

/* ================= Display Password or Ids with Updated On Screen ================= */
void updatePasswordOrIdsOnScreen() {
  if (waitingForPassword) {
    displayPasswordOrIdMassegesOnScreen(maskPassword(keyBuffer.length()));
  } else if (waitingForPreviousePasswordForConformation) {
    displayPasswordOrIdMassegesOnScreen(maskPassword(keyBuffer.length()));
  } else if (waitingForNewPassword) {
    displayPasswordOrIdMassegesOnScreen(maskPassword(keyBuffer.length()));
  } else if (isRegisterMode || isDeleteMode) {
    tft.fillRect(0, 110, 320, 30, TFT_WHITE);
    tft.setCursor(130, 125);
    tft.print(keyBuffer);
  } else {
    // pass
  }
}

/* ================= Check Keypad ================= */
void checkKeypad() {
  while (customKeypad.available()) {
    keypadEvent e = customKeypad.read();
    char key = (char)e.bit.KEY;
    if (e.bit.EVENT != KEY_JUST_PRESSED) return;

    // Serial.println(key + " pressed");

    /* ===== LONG PRESS DETECTION FOR # ===== */
    if (key == '#') {
      if (e.bit.EVENT == KEY_JUST_PRESSED) {
        hashPressed = true;
        hashPressStart = millis();
        adminTriggered = false;
        return;
      }

      if (e.bit.EVENT == KEY_JUST_RELEASED) {
        hashPressed = false;
        hashPressStart = 0;
        adminTriggered = false;
        return;
      }
    }

    /* ========== DIGITS ========== */
    if (key >= '0' && key <= '9') {
      keyBuffer += key;
      Serial.println("Buffer: " + String(keyBuffer));
      updatePasswordOrIdsOnScreen();
      return;
    }

    /* ===== STAR (*) LONG PRESS FOR PASSWORD RESET ===== */
    if (key == '*') {
      if (e.bit.EVENT == KEY_JUST_PRESSED) {
        starPressed = true;
        passwordResetDone = false;
        starPressStart = millis();
        return;  // DO NOT clear buffer yet
      }

      if (e.bit.EVENT == KEY_JUST_RELEASED) {
        starPressed = false;

        // Short press → clear buffer
        if (!passwordResetDone) {
          keyBuffer = "";
          updatePasswordOrIdsOnScreen();
          Serial.println("Cleared");
        }

        return;
      }
    }

    /* ========== OPEN MENU For Registration And Delet ========== */
    if (key == 'A') {
      menuActive = true;
      Serial.println("Menu Opened");
      registratinAndDeleteOptionDisplayOnScreen();
      return;
    }

    /* ========== SELECT REGISTRATION MENU ========== */
    if (key == 'B' && menuActive) {
      isRegisterMode = true;
      isDeleteMode = false;
      Serial.println("Register Selected");
      registratinAndDeleteOptionDisplayOnlyRegisrationOnScreen();
      return;
    }

    /* ========== SELECT DELETE MENU ========== */
    if (key == 'C' && menuActive) {
      isDeleteMode = true;
      isRegisterMode = false;
      Serial.println("Delete Selected");
      registratinAndDeleteOptionDisplayOnlyDeletOnScreen();
      return;
    }

    /* ========== SELECT Mode Of Admin Fingerprint ========== */
    if (key == 'B' && adminMenuActive) {
      isFingerprintMode = true;
      isPasswordMode = false;
      Serial.println("Fingerprint Selected");
      fingerprintAndpasswordOptionDisplayAndFingerprintModeSelectOnScreen();
      return;
    }

    /* ========== SELECT Mode Of Admin password ========== */
    if (key == 'C' && adminMenuActive) {
      isPasswordMode = true;
      isFingerprintMode = false;
      Serial.println("password Selected");
      fingerprintAndpasswordOptionDisplayAndPasswordModeSelectOnScreen();
      return;
    }

    /* ========== SET (D KEY) ========== */
    if (key == 'D') {

      /* ---- STEP 1: CONFIRM MODE ---- */
      if (menuActive && !modeSelected) {
        if (!isRegisterMode && !isDeleteMode) {
          Serial.println("Select Register or Delete first");
          return;
        }

        modeSelected = true;
        waitingForPassword = true;
        keyBuffer = "";

        Serial.println("Mode Confirmed");
        Serial.println("Enter Admin Password");
        displayMassegesOnScreen("Enter Admin Password");
        return;
      }

      /* ---- STEP 1: CONFIRM MODE For Admin Page---- */
      if (adminMenuActive && !adminModeSelected) {
        if (!isFingerprintMode && !isPasswordMode) {
          Serial.println("Select Fingerprint or Password Mode first");
          return;
        }

        adminModeSelected = true;

        Serial.println("Mode Confirmed");
        if (isPasswordMode) {
          waitingForPreviousePasswordForConformation = true;
          keyBuffer = "";
          Serial.println("Enter Previous  Admin Password");
          displayMassegesOnScreen("Enter Previous Password");
          return;
        } else if (isFingerprintMode) {
          waitingForPassword = true;
          Serial.println("Mode Confirmed");
          Serial.println("Enter Admin Password");
          displayMassegesOnScreen("Enter Admin Password");
          return;
        }
      }

      if (waitingForPreviousePasswordForConformation) {
        enteredNumber = keyBuffer.toInt();
        keyBuffer = "";

        if (enteredNumber == newAdminPassword) {
          Serial.println("Password OK");
          displayMassegesOnScreen("Admin Password OK!");
          waitingForPreviousePasswordForConformation = false;
          waitingForNewPassword = true;
          Serial.println("Enter New Password");
          displayMassegesOnScreen("Enter Admin New Password");
        } else {
          Serial.println("Wrong Password!");
          displayMassegesOnScreen("Wrong Admin Password!");
          delay(1000);
          tft.fillRect(0, 50, 320, 190, TFT_WHITE);
          resetAllStates();
        }
        return;
      }

      if (waitingForNewPassword) {
        newAdminPassword = keyBuffer.toInt();
        keyBuffer = "";

        saveAdminPassword(newAdminPassword);
        Serial.println("New Admin Password Updated");
        displayMassegesOnScreen("New Admin Password Updated");
        delay(1000);
        tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
        resetAllStates();
        return;
      }

      /* ---- STEP 2: PASSWORD ---- */
      if (waitingForPassword) {
        enteredNumber = keyBuffer.toInt();
        keyBuffer = "";

        if (enteredNumber == newAdminPassword) {
          Serial.println("Admin Password OK");
          displayMassegesOnScreen("Admin Password OK!");
          waitingForPassword = false;
          waitingForID = true;
          if (isFingerprintMode) {
            isRegisterMode = true;
          }
          Serial.println("Enter ID then press ENTER Key");
          displayMassegesOnScreen("Enter Employee ID");
        } else {
          Serial.println("Wrong Password!");
          displayMassegesOnScreen("Wrong Admin Password!");
          delay(1000);
          tft.fillRect(0, 50, 320, 190, TFT_WHITE);
          resetAllStates();
        }
        return;
      }

      /* ---- STEP 3: ID For Registration OR Delete ---- */
      if (waitingForID) {
        enteredNumber = keyBuffer.toInt();
        keyBuffer = "";

        employeeID = enteredNumber;

        if (employeeID == 0) {
          Serial.println("Invalid Employee ID");
          displayMassegesOnScreen("Invalid Employee ID!");
          return;
        }

        if (isRegisterMode) {
          fingerID = getNextFreeFingerID();

          if (fingerID == 0) {
            Serial.println("Fingerprint memory full");
            displayMassegesOnScreen("Fingerprint Memory Full");
            resetAllStates();
            return;
          }
          Serial.print("Auto Finger ID: ");
          Serial.println(fingerID);
          enrollFinger(fingerID);
          saveEmployeeID(fingerID, employeeID);
        } else if (isDeleteMode) {
          fingerID = findFingerIDByEmployee(employeeID);
          Serial.println("fingerID : " + String(fingerID));
          if (fingerID) {
            deleteFinger(fingerID);
            deleteEmployeeID(fingerID);
          }
          displayMassegesOnScreen("Fingerprint Deleted!");
        }
        delay(2000);                               // For refresh the page
        tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
        resetAllStates();
        return;
      }
    }
  }

  /* ===== CHECK # HOLD TIME AND ADMIN PAGE  ===== */
  if (hashPressed && !adminTriggered) {
    if (millis() - hashPressStart >= HASH_HOLD_TIME) {
      adminTriggered = true;
      hashPressed = false;
      adminMenuActive = true; /* ENTER ADMIN MODE */
      Serial.println(">>> ADMIN MODE ACTIVATED <<<");
      adminMenuDisplayOnScreen();
    }
  }

  /* ===== CHECK * HOLD TIME AND ADMIN PAGE  ===== */
  if (starPressed && !passwordResetDone) {
    if (millis() - starPressStart >= starResetTime) {
      passwordResetDone = true;
      starPressed = false;

      resetAdminPassword();
      resetAllStates();
    }
  }
}

/* ===== Reset All Parameters  ===== */
void resetAllStates() {
  keyBuffer = "";
  enteredNumber = 0;

  // For User Page
  menuActive = false;
  modeSelected = false;
  waitingForPassword = false;
  waitingForID = false;
  isRegisterMode = false;
  isDeleteMode = false;

  // For Admin Page
  adminMenuActive = false;
  adminModeSelected = false;
  waitingForAdminPassword = false;
  waitingForAdminID = false;
  isFingerprintMode = false;
  isPasswordMode = false;
  waitingForPreviousePasswordForConformation = false;
  waitingForNewPassword = false;

  // For Admin Password Reset
  bool starPressed = false;
  bool passwordResetDone = false;
}

/* ===== Reset Admin Password  ===== */
void resetAdminPassword() {
  newAdminPassword = defaultAdminPassword;
  saveAdminPassword(newAdminPassword);
  Serial.println("ADMIN PASSWORD RESET TO DEFAULT");
  displayMassegesOnScreen("Admin Password Reset!");
  delay(2000);
  tft.fillRect(0, 50, 320, 190, TFT_WHITE);  // Clear display area
}

/* ===== Get Employee ID From there Finger Ids  ===== */
uint16_t getEmployeeID(uint8_t fingerID) {
  prefs.begin("empmap", true);
  char key[8];
  sprintf(key, "fp_%d", fingerID);
  uint16_t empID = prefs.getUShort(key, 0);
  prefs.end();
  return empID;
}

/* ===== Save Employee Ids On Flash Memory  ===== */
void saveEmployeeID(uint8_t fingerID, uint16_t empID) {
  prefs.begin("empmap", false);
  char key[8];
  sprintf(key, "fp_%d", fingerID);
  prefs.putUShort(key, empID);
  prefs.end();

  Serial.println("Employee ID " + String(empID) + " mapped to Finger ID " + String(fingerID));
}

/* ===== Delete Employee Ids From Flash Memory using Finger Ids ===== */
void deleteEmployeeID(uint8_t fingerID) {
  prefs.begin("empmap", false);
  char key[8];
  sprintf(key, "fp_%d", fingerID);
  prefs.remove(key);
  prefs.end();

  Serial.println("Employee ID mapping deleted");
}

/* ===== Find Finger Id By Employee From Flash Memeory ===== */
uint8_t findFingerIDByEmployee(uint16_t empID) {
  prefs.begin("empmap", true);
  for (uint8_t id = 1; id <= 127; id++) {
    char key[8];
    sprintf(key, "fp_%d", id);
    if (prefs.getUShort(key, 0) == empID) {
      prefs.end();
      return id;
    }
  }
  prefs.end();
  return 0;
}

/* ===== Get Free Finger Id For Registration ===== */
uint8_t getNextFreeFingerID() {
  for (uint8_t id = 1; id <= 127; id++) {
    if (finger.loadModel(id) != FINGERPRINT_OK) {
      return id;  // Free slot
    }
  }
  return 0;  // No free slot
}

/* ===== Controlling Through Serial ===== */
void controlThroughSerial() {
  if (Serial.available()) {    // Check if any serial input is available
    char cmd = Serial.read();  // Read the command character

    if (cmd == 'e') {  // If command is enroll
      // bool result = searchFinger();
      bool result = searchFingerNonBlocking();
      if (result) {
        Serial.println("Fingerprint Already Enroll");
      } else {
        Serial.println("Enter ID (1-127):");   // Ask user for fingerprint ID
        fingerID = readID();                   // Read ID from serial
        if (fingerID < 1 || fingerID > 127) {  // Validate ID range
          Serial.println("Invalid ID! Try again.");
          return;  // Exit loop iteration
        }
        Serial.println("Enrolling ID: " + String(fingerID));  // Print enrolling ID
        enrollFinger(fingerID);                               // Call enroll function
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
    if (cmd == 's') searchFingerNonBlocking();  // If command is search, call search function
    // if (cmd == 'c') countFinger();              // Count fingerprints
    if (cmd == 'x') deleteAllFinger();  // Delete all fingerprints
    if (cmd == 'l') listStoredIDs();    // List all stored fingerprint IDs
    if (cmd == 'p') printLogs();        // Print stored logs
    if (cmd == 'c') {
      fullEEPROMErase();
    }
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

/* ================= List Of Stored Ids  ================= */
void listStoredIDs() {
  Serial.println("Stored Fingerprint IDs:");  // Print header
  bool foundAny = false;                      // Flag to track stored IDs

  for (uint8_t id = 1; id <= 127; id++) {  // Loop through all possible IDs
    uint8_t p = finger.loadModel(id);      // Try loading fingerprint model
    if (p == FINGERPRINT_OK) {             // If model exists
      Serial.println("ID " + String(id) + " exists");
      foundAny = true;  // Mark that at least one ID exists
    }
  }

  if (!foundAny) {
    Serial.println("No fingerprints stored");  // No IDs found
  }
}

/* ================= Enroll New FingerPrint  ================= */
void enrollFinger(uint8_t id) {
  int p = -1;  // Variable for sensor response

  Serial.println("Place finger");  // Ask user to place finger
  displayMassegesOnScreen("Place Finger");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();  // Capture fingerprint image
  }
  finger.image2Tz(1);               // Convert image to template buffer 1
  Serial.println("Remove Finger");  // Ask user to remove finger
  displayMassegesOnScreen("Remove finger");
  delay(2000);                                // Wait for finger removal
  Serial.println("Place same Finger again");  // Ask for second scan
  displayMassegesOnScreen("Place Finger again");
  while (finger.getImage() != FINGERPRINT_OK)
    ;                  // Wait for image
  finger.image2Tz(2);  // Convert second image to buffer 2
  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Finger Mismatch");  // If fingerprints don't match
    displayMassegesOnScreen("Fingerprint Mismatch");
    return;
  }
  if (finger.storeModel(id) == FINGERPRINT_OK) {
    Serial.println("Fingerprint stored successfully");
    displayMassegesOnScreen("FP stored successfully!");  // Store success
  } else {
    Serial.println("Store failed");
    displayMassegesOnScreen("Fingerprint Store failed");
  }  // Store failed
}

/* ================= Search Fingerprint  ================= */
bool searchFingerNonBlocking() {
  int p;
  switch (fingerState) {

    case FP_idle:
      Serial.println("Place finger to search");
      fpStartTime = millis();
      fingerState = FP_waitFinger;
      break;

    case FP_waitFinger:
      if (millis() - fpStartTime > FP_timeout) {
        Serial.println("Finger timeout");
        fingerState = FP_idle;
        return false;
      }

      p = finger.getImage();
      if (p == FINGERPRINT_OK) {
        fingerState = FP_imageConvert;
      }
      break;

    case FP_imageConvert:
      p = finger.image2Tz(1);
      if (p == FINGERPRINT_OK) {
        fingerState = FP_search;
      } else {
        fingerState = FP_idle;
      }
      break;

    case FP_search:
      p = finger.fingerSearch();
      if (p == FINGERPRINT_OK) {
        digitalWrite(Relay, HIGH);
        Serial.println("Relay ON");
        Serial.println("Found ID: " + String(finger.fingerID) + " Confidence: " + String(finger.confidence));

        uint16_t empID = getEmployeeID(finger.fingerID);

        Serial.println("Employee ID: " + String(empID));

        displayMassegesOnScreen("Fingerprint Match!");
        delay(1000);
        tft.fillRect(0, 50, 320, 190, TFT_WHITE);

        storeRTCLog(empID);
        fingerState = FP_done;
        return true;
      } else {
        Serial.println("No match found");
        displayMassegesOnScreen("Fingerprint Not Match!");
        delay(1000);
        tft.fillRect(0, 50, 320, 190, TFT_WHITE);
        fingerState = FP_idle;
        return false;
      }

    case FP_done:
      fingerState = FP_idle;
      break;
  }
  return false;
}

/* ================= Delete FingerPrint With their Ids ================= */
void deleteFinger(uint8_t id) {
  if (finger.deleteModel(id) == FINGERPRINT_OK) Serial.println("Fingerprint deleted");  // Delete success
  else Serial.println("Delete failed");                                                 // Delete failed
}

/* ================= Count FingerPrints  ================= */
void countFinger() {
  finger.getTemplateCount();                                              // Get fingerprint count
  Serial.println("Total fingerprints: " + String(finger.templateCount));  // Print count
}

/* ================= Delete All FingerPrints  ================= */
void deleteAllFinger() {
  if (finger.emptyDatabase() == FINGERPRINT_OK)
    Serial.println("All fingerprints deleted");  // Database cleared
  else
    Serial.println("Database clear failed");  // Clear failed
}

/* ================= EEPROM CLEAR For Logs Clear ================= */
void fullEEPROMErase() {
  Serial.println("FULL EEPROM ERASE START");
  for (uint16_t i = 0; i < eepromSize; i++) {
    eepromWriteByte(i, 0xFF);
  }

  logIndex = 0;
  saveLogCount(0);

  Serial.println("✅ EEPROM completely erased");
}

/* ================= EEPROM LOW LEVEL WRITE ================= */
// Write a single byte to EEPROM
void eepromWriteByte(uint16_t addr, uint8_t data) {
  Wire.beginTransmission(eepromAddr);  // Start I2C communication with EEPROM
  Wire.write(addr >> 8);               // Send high byte of address
  Wire.write(addr & 0xFF);             // Send low byte of address
  Wire.write(data);                    // Send data byte
  Wire.endTransmission();              // End transmission
  delay(5);                            // EEPROM write delay (important)
}

/* ================= EEPROM LOW LEVEL READ ================= */

// Read a single byte from EEPROM
uint8_t eepromReadByte(uint16_t addr) {
  Wire.beginTransmission(eepromAddr);  // Start I2C communication
  Wire.write(addr >> 8);               // High byte of address
  Wire.write(addr & 0xFF);             // Low byte of address
  Wire.endTransmission();              // End transmission

  Wire.requestFrom(eepromAddr, 1);  // Request 1 byte from EEPROM
  return Wire.read();               // Return received byte
}

/* ================= SAVE LOG COUNT ================= */

// Store total number of logs in EEPROM
void saveLogCount(uint16_t count) {
  eepromWriteByte(logCountAddr, count >> 8);        // Store high byte
  eepromWriteByte(logCountAddr + 1, count & 0xFF);  // Store low byte
}

/* ================= READ LOG COUNT ================= */
uint16_t readLogCount() {
  uint16_t count =
    (eepromReadByte(logCountAddr) << 8) | eepromReadByte(logCountAddr + 1);

  if (count > maxLogs) {
    Serial.println("EEPROM invalid, resetting log count");
    count = 0;
    saveLogCount(0);
  }
  return count;
}

/* ================= WRITE ONE LOG ================= */

// Write one LogEntry structure to EEPROM
void writeLog(uint16_t index, LogEntry log) {
  if (index >= maxLogs) {
    Serial.println("EEPROM FULL");
    return;
  }

  uint16_t addr = logStartAddr + (index * logSize);
  uint8_t *p = (uint8_t *)&log;

  for (uint8_t i = 0; i < logSize; i++) {
    eepromWriteByte(addr + i, p[i]);
  }
}

/* ================= READ ONE LOG ================= */

// Read one LogEntry structure from EEPROM
LogEntry readLog(uint16_t index) {
  LogEntry log;
  uint16_t addr = logStartAddr + (index * logSize);
  uint8_t *p = (uint8_t *)&log;

  for (uint8_t i = 0; i < logSize; i++) {
    p[i] = eepromReadByte(addr + i);
  }
  return log;
}

/* ================= STORE RTC LOG ================= */

// Store fingerprint match log with RTC timestamp
// void storeRTCLog(uint8_t userID) {
void storeRTCLog(uint32_t employeeID) {
  if (logIndex >= maxLogs) {
    Serial.println("Log memory full");
    return;
  }

  DateTime now = rtc.now();
  LogEntry log;

  // log.userID = userID;
  log.employeeID = employeeID;
  log.year = now.year();
  log.month = now.month();
  log.day = now.day();
  log.hour = now.hour();
  log.minute = now.minute();
  log.second = now.second();

  Serial.println("Logging: " + String(employeeID) + " " + String(log.year) + "-" + String(log.month) + "-" + String(log.day) + " " + String(log.hour) + ":" + String(log.minute) + ":" + String(log.second));

  writeLog(logIndex, log);
  logIndex++;
  saveLogCount(logIndex);

  Serial.println("✅ Log stored");
}

// Print all stored logs via Serial Monitor
void printLogs() {
  Serial.println("---- ATTENDANCE LOGS ----");

  for (uint16_t i = 0; i < logIndex; i++) {
    LogEntry log = readLog(i);  // Read log from EEPROM

    Serial.println("ID " + String(log.employeeID) + "  " + String(log.year) + "-" + String(log.month) + "-" + String(log.day) + " " + String(log.hour) + ":" + String(log.minute) + ":" + String(log.second));
  }
}

/* ================= LOAD ADMIN PASSWORD ================= */
unsigned long loadAdminPassword() {
  prefs.begin("admin", false);  // namespace "admin"
  unsigned long pass = prefs.getULong("password", 1234);
  prefs.end();

  Serial.println("Admin password loaded from internal storage");
  return pass;
}

/* ================= SAVE ADMIN PASSWORD ================= */
void saveAdminPassword(unsigned long pass) {
  prefs.begin("admin", false);
  prefs.putULong("password", pass);
  prefs.end();

  Serial.println("Admin password saved to internal storage");
}

SplitData splitStringByColon(const String &data) {
  SplitData mqttMsg;
  int firstIndex = data.indexOf(':');
  if (firstIndex != -1) {
    mqttMsg.indexOneData = data.substring(0, firstIndex);
    int secondIndex = data.indexOf(':', firstIndex + 1);
    if (secondIndex != -1) {
      mqttMsg.indexTwoData = data.substring(firstIndex + 1, secondIndex);
      mqttMsg.indexThreeData = data.substring(secondIndex + 1);
      if (mqttMsg.indexThreeData.length() > 0) {
      }
    } else {
      mqttMsg.indexTwoData = data.substring(firstIndex + 1);
    }
  } else {
    mqttMsg.indexOneData = data.substring(firstIndex + 1);
  }
  return mqttMsg;
}
