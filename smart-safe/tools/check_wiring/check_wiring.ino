/*
  CHECK WIRING  -  finds why the ATmega32 does not answer.
  Same wires as the flasher, PLUS one extra test wire:
     ESP32 G34 -> ATmega pin 10 (VCC)      (only for this test)
  Upload, open Serial Monitor at 115200, press EN, send a screenshot.
*/
#include <Arduino.h>
const int P_MOSI = 23, P_MISO = 19, P_SCK = 18, P_RST = 5, P_CLK = 4, P_VCC = 34;

void clockOn(uint32_t hz) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(P_CLK); ledcAttach(P_CLK, hz, 1); ledcWrite(P_CLK, 1);
#else
  ledcSetup(0, hz, 1); ledcAttachPin(P_CLK, 0); ledcWrite(0, 1);
#endif
}
uint8_t xfer(uint8_t out, int us) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(P_MOSI, (out >> i) & 1); delayMicroseconds(us);
    digitalWrite(P_SCK, HIGH);           delayMicroseconds(us);
    in = (in << 1) | digitalRead(P_MISO);
    digitalWrite(P_SCK, LOW);
  }
  return in;
}
bool tryEnable(int us) {
  digitalWrite(P_SCK, LOW);
  digitalWrite(P_RST, HIGH); delay(5);
  digitalWrite(P_RST, LOW);  delay(50);
  xfer(0xAC, us); xfer(0x53, us); uint8_t e = xfer(0, us); xfer(0, us);
  Serial.printf("    speed %4d us: echo 0x%02X %s\n", us, e, e == 0x53 ? "<-- OK!" : "");
  return e == 0x53;
}

void setup() {
  Serial.begin(115200); delay(1500);
  Serial.println("\n===== CHECK WIRING =====");

  // 1. power
  analogSetAttenuation(ADC_11db);
  int mv = analogReadMilliVolts(P_VCC);
  Serial.printf("1) ATmega pin 10 voltage: %.2f V  -> %s\n", mv / 1000.0,
    mv > 2800 ? "OK" : mv > 500 ? "LOW: bad + wire or short" : "NO POWER: pin 10 not on +, or test wire G34 missing");

  // 2. reset pull-up (G5 not driven)
  pinMode(P_RST, INPUT);
  delay(5);
  int r = digitalRead(P_RST);
  Serial.printf("2) RESET pin 9 level: %d  -> %s\n", r, r ? "OK (10k pull-up works)" : "LOW: 10k not to +, or G5 not on pin 9");

  // 3. MISO line: does it follow the ESP32 internal pull resistors?
  pinMode(P_MISO, INPUT_PULLUP);   delay(5); int up = digitalRead(P_MISO);
  pinMode(P_MISO, INPUT_PULLDOWN); delay(5); int dn = digitalRead(P_MISO);
  pinMode(P_MISO, INPUT);
  Serial.printf("3) MISO (pin 7) with pull-up=%d pull-down=%d -> %s\n", up, dn,
    (up == 1 && dn == 0) ? "floating (normal before programming mode)" :
    (up == 0) ? "held LOW: G19 touches GND or a wrong pin" : "held HIGH: G19 touches + or a wrong pin");

  // 4. try programming mode with different speeds and clocks
  pinMode(P_MOSI, OUTPUT); pinMode(P_SCK, OUTPUT); pinMode(P_RST, OUTPUT);
  bool ok = false;
  const uint32_t clocks[] = {0, 1000000, 8000000};
  const int speeds[] = {8, 50, 200};
  for (int c = 0; c < 3 && !ok; c++) {
    if (clocks[c]) clockOn(clocks[c]);
    Serial.printf("4) clock on G4: %s\n", clocks[c] ? (clocks[c] == 1000000 ? "1 MHz" : "8 MHz") : "OFF");
    for (int s = 0; s < 3 && !ok; s++) ok = tryEnable(speeds[s]);
  }
  digitalWrite(P_RST, HIGH);
  Serial.println(ok ? "\nRESULT: the chip ANSWERS. Send this screenshot to Claude."
                    : "\nRESULT: still no answer. Send this screenshot to Claude.");
}
void loop() {}
