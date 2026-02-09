#include <Adafruit_Fingerprint.h>  // Include Adafruit fingerprint sensor library

/* ================= HARDWARE ================= */
HardwareSerial FingerSerial(2);              // Create HardwareSerial object using UART2 of ESP32
Adafruit_Fingerprint finger(&FingerSerial);  // Create fingerprint object using UART2

/* ================= GLOBAL ================= */
uint8_t fingerID;  // Variable to store fingerprint ID (1–127)


/* ================= CORE 0 TASK ================= */
/* Fingerprint Sensor Logic */
void FingerprintSensorTask(void *pvParameters) {

  while (1) {
    controlThroughSerial();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/* ================= CORE 1 TASK ================= */
/* Display / UI Logic */
void displayTask(void *pvParameters) {

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}



/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);                                    // Start USB serial communication at 9600 baud
  FingerSerial.begin(57600, SERIAL_8N1, 16, 17);         // Start UART2 at 57600 baud, RX=16, TX=17
  Serial.println("\nAS608 Fingerprint System Started");  // Print startup message
  finger.begin(57600);                                   // Initialize fingerprint sensor communication

  if (!finger.verifyPassword()) {  // Check if sensor responds correctly
    Serial.println("Fingerprint sensor not detected");
    // Print error if sensor not found
    // while (1)
    //   ;
  } else {
    Serial.println("Fingerprint sensor detected");  // Print success message
  }

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

  /* -------- CORE 0 : MOTOR -------- */
  xTaskCreatePinnedToCore(
    FingerprintSensorTask,
    "Fingerprint Sensor Task",
    4096,
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

    if (cmd == 's') searchFinger();  // If command is search, call search function

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

    if (cmd == 'c') countFinger();  // Count fingerprints

    if (cmd == 'x') deleteAllFinger();  // Delete all fingerprints

    if (cmd == 'l') listStoredIDs();  // List all stored fingerprint IDs
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

  finger.image2Tz(1);  // Convert image to template buffer 1

  Serial.println("Remove finger");  // Ask user to remove finger
  delay(2000);                      // Wait for finger removal

  Serial.println("Place same finger again");  // Ask for second scan
  while (finger.getImage() != FINGERPRINT_OK)
    ;  // Wait for image

  finger.image2Tz(2);  // Convert second image to buffer 2

  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Finger mismatch");  // If fingerprints don't match
    return;
  }

  if (finger.storeModel(id) == FINGERPRINT_OK)
    Serial.println("Fingerprint stored successfully");  // Store success
  else
    Serial.println("Store failed");  // Store failed
}

bool searchFinger() {
  int p = -1;  // Variable for sensor response

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
    return true;                        // Match found
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
