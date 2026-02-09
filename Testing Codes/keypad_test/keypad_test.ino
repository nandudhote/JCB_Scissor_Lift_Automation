
// #include <Keypad.h>

// const byte ROWS = 4; /* four rows */
// const byte COLS = 4; /* four columns */
// /* define the symbols on the buttons of the keypads */
// char hexaKeys[ROWS][COLS] = {
//   { '0', '1', '2', '3' },
//   { '4', '5', '6', '7' },
//   { '8', '9', 'A', 'B' },
//   { 'C', 'D', 'E', 'F' }
// };

// byte rowPins[ROWS] = { 32, 33, 25, 26 }; /* connect to the row pinouts of the keypad */
// byte colPins[COLS] = { 27, 5, 18, 19 }; /* connect to the column pinouts of the keypad */

// /* initialize an instance of class NewKeypad */
// Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// void setup() {
//   Serial.begin(9600);
// }

// void loop() {
//   char customKey = customKeypad.getKey();

//   if (customKey) {
//     Serial.println(customKey);
//   }
// }




// // Use this example with the Adafruit Keypad products.
// // You'll need to know the Product ID for your keypad.
// // Here's a summary:
// //   * PID3844 4x4 Matrix Keypad
// //   * PID3845 3x4 Matrix Keypad
// //   * PID1824 3x4 Phone-style Matrix Keypad
// //   * PID1332 Membrane 1x4 Keypad
// //   * PID419  Membrane 3x4 Matrix Keypad

#include "Adafruit_Keypad.h"

// define your specific keypad here via PID
#define KEYPAD_PID3844
// define your pins here
// can ignore ones that don't apply

// #define R1 27
// #define R2 5
// #define R3 18
// #define R4 19
// #define C1 32
// #define C2 33
// #define C3 25
// #define C4 26

#define R1 32
#define R2 33
#define R3 25
#define R4 26
#define C1 27
#define C2 5
#define C3 18
#define C4 19

// leave this import after the above configuration
#include "keypad_config.h"

//initialize an instance of class NewKeypad
Adafruit_Keypad customKeypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(9600);
  customKeypad.begin();
}

void loop() {
  // put your main code here, to run repeatedly:
  customKeypad.tick();

  while (customKeypad.available()) {
    keypadEvent e = customKeypad.read();
    Serial.print((char)e.bit.KEY);
    if (e.bit.EVENT == KEY_JUST_PRESSED) Serial.println(" pressed");
    else if (e.bit.EVENT == KEY_JUST_RELEASED) Serial.println(" released");
  }

  delay(100);
}