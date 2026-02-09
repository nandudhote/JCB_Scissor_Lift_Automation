#include <Preferences.h>

Preferences prefs;

/* ================= PASSWORD ================= */
unsigned long ADMIN_PASSWORD = 1234;  // Default password

/* ================= LOAD PASSWORD ================= */
unsigned long loadAdminPassword() {
  prefs.begin("admin", false);  // namespace "admin"
  unsigned long pass = prefs.getULong("password", 1234);
  prefs.end();

  Serial.println("✅ Admin password loaded from internal storage");
  return pass;
}

/* ================= SAVE PASSWORD ================= */
void saveAdminPassword(unsigned long pass) {
  prefs.begin("admin", false);
  prefs.putULong("password", pass);
  prefs.end();

  Serial.println("✅ Admin password saved to internal storage");
}

/* ================= MASK DISPLAY ================= */
void printMaskedPassword() {
  // Serial.println("Admin Password: ****");
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);
  delay(5000);

  /* ---- Load password on boot ---- */
  ADMIN_PASSWORD = loadAdminPassword();
  Serial.print("Current ");
  Serial.println("Admin Password: " + String(ADMIN_PASSWORD));
  printMaskedPassword();

  /* ---- Example: Change password ---- */
  delay(3000);
  // unsigned long NEW_ADMIN_PASSWORD = 5678;
  unsigned long NEW_ADMIN_PASSWORD = 2580;

  Serial.println("Updating admin password...");
  saveAdminPassword(NEW_ADMIN_PASSWORD);

  /* ---- Reload to verify ---- */
  ADMIN_PASSWORD = loadAdminPassword();
  Serial.print("Verified ");
  Serial.println(ADMIN_PASSWORD);
  printMaskedPassword();

  Serial.println("✅ Done. Reboot ESP32 to test persistence.");
}

void loop() {
  // Nothing here
}
