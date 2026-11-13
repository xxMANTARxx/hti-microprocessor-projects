/*
  FIND PINS  -  tells you which wire the G34 test wire is touching.
  Keep all flasher wires. Move the G34 test wire into the row of one ATmega pin,
  read the Serial Monitor (115200), then move it to the next pin.
  Expected:  pin 6 -> G23   pin 7 -> G19   pin 8 -> G18   pin 9 -> G5
             pin 13 -> G4   pin 10/30 -> + (3.3V)   pin 11/31 -> GND (0V)
*/
#include <Arduino.h>
const int PROBE = 34;
const int SIG[] = {23, 19, 18, 5, 4};
const char *NAME[] = {"G23 (should be pin 6)", "G19 (should be pin 7)", "G18 (should be pin 8)", "G5 (should be pin 9)", "G4 (should be pin 13)"};

int readMv() { int s = 0; for (int i = 0; i < 8; i++) s += analogReadMilliVolts(PROBE); return s / 8; }

void setup() {
  Serial.begin(115200); delay(1000);
  analogSetAttenuation(ADC_11db);
  for (int i = 0; i < 5; i++) { pinMode(SIG[i], OUTPUT); digitalWrite(SIG[i], LOW); }
  Serial.println("\n===== FIND PINS =====");
}

void loop() {
  int base = readMv();                   // all signals LOW
  int hits = -1, nhits = 0;
  for (int i = 0; i < 5; i++) {
    digitalWrite(SIG[i], HIGH); delay(20);
    int v = readMv();
    digitalWrite(SIG[i], LOW); delay(20);
    if (v - base > 1500) { hits = i; nhits++; }
  }
  if (nhits == 1)       Serial.printf("Test wire touches: %s\n", NAME[hits]);
  else if (nhits > 1)   Serial.println("Test wire touches MORE THAN ONE signal: two wires share a row!");
  else if (base > 2500) Serial.printf("Test wire touches + power (%.2f V)\n", base / 1000.0);
  else if (base < 150)  Serial.println("Test wire touches GND or nothing (0 V)");
  else                  Serial.printf("Test wire reads %.2f V (not a clear signal)\n", base / 1000.0);
  delay(800);
}
