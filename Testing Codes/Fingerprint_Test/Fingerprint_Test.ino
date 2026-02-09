#include <Adafruit_Fingerprint.h>

/* ================= HARDWARE ================= */
HardwareSerial FingerSerial(2);  // ESP32 UART2
Adafruit_Fingerprint finger(&FingerSerial);

/* ================= GLOBAL ================= */
uint8_t fingerID;

/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);
  FingerSerial.begin(57600, SERIAL_8N1, 16, 17);

  Serial.println("\nAS608 Fingerprint System Started");

  finger.begin(57600);

  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not detected");
    // while (1)
    //   ;
  }

  Serial.println("Fingerprint sensor detected");

  finger.getTemplateCount();
  Serial.print("Stored fingerprints: ");
  Serial.println(finger.templateCount);

  Serial.println("\nCommands:");
  Serial.println("e = Enroll");
  Serial.println("s = Search");
  Serial.println("d = Delete ID");
  Serial.println("c = Count");
  Serial.println("x = Delete ALL");
  Serial.println("l = List of All STored");
}

/* ================= LOOP ================= */
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
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

    if (cmd == 's') searchFinger();

    if (cmd == 'd') {
      Serial.println("Enter ID to delete:");
      fingerID = readID();
      if (fingerID < 1 || fingerID > 127) {
        Serial.println("Invalid ID! Try again.");
        return;
      }
      Serial.println("Deleting ID: " + String(fingerID));
      deleteFinger(fingerID);
    }

    if (cmd == 'c') countFinger();

    if (cmd == 'x') deleteAllFinger();

    if (cmd == 'l') listStoredIDs();
  }
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



/* ================= FUNCTIONS ================= */

void listStoredIDs() {
  Serial.println("Stored Fingerprint IDs:");

  bool foundAny = false;

  for (uint8_t id = 1; id <= 127; id++) {
    uint8_t p = finger.loadModel(id);
    if (p == FINGERPRINT_OK) {
      Serial.print("ID ");
      Serial.print(id);
      Serial.println(" exists");
      foundAny = true;
    }
  }

  if (!foundAny) {
    Serial.println("No fingerprints stored");
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

void searchFinger() {
  int p = -1;

  Serial.println("Place finger to search");

  // WAIT until finger is detected
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    // Serial.println("Image Getting : " + String(p));
    delay(50);
  }

  p = finger.image2Tz(1);  // Convert image to template buffer 1
  Serial.println("Convert image to template : " + String(p));
  if (p != FINGERPRINT_OK) {
    Serial.println("Image conversion failed");
    return;
  }

  p = finger.fingerSearch();  // Search database
  Serial.println("Search database : " + String(p));
  if (p == FINGERPRINT_OK) {
    Serial.print("Found ID: ");
    Serial.print(finger.fingerID);
    Serial.print("  Confidence: ");
    Serial.println(finger.confidence);
  } else {
    Serial.println("No match found");
  }
}

void deleteFinger(uint8_t id) {
  if (finger.deleteModel(id) == FINGERPRINT_OK) Serial.println("Fingerprint deleted");
  else Serial.println("Delete failed");
}

void countFinger() {
  finger.getTemplateCount();
  Serial.print("Total fingerprints: ");
  Serial.println(finger.templateCount);
}

void deleteAllFinger() {
  if (finger.emptyDatabase() == FINGERPRINT_OK)
    Serial.println("All fingerprints deleted");
  else
    Serial.println("Database clear failed");
}
