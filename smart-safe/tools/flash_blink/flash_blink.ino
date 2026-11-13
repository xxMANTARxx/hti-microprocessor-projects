/*
  ESP32 -> ATmega32A FLASHER : Blink test
  Everything is inside this one file. Just open it in Arduino IDE and Upload.
  Board: "ESP32 Dev Module". Then open Serial Monitor at 115200.

  Wiring (ATmega32 powered from ESP32 3V3 while flashing):
    ESP32 G23 -> ATmega pin 6  (MOSI)
    ESP32 G19 -> ATmega pin 7  (MISO)
    ESP32 G18 -> ATmega pin 8  (SCK)
    ESP32 G5  -> ATmega pin 9  (RESET, 10k pull-up to + stays)
    ESP32 G4  -> ATmega pin 13 (XTAL1, clock from the ESP32, helps chips set for a crystal)
    ESP32 3V3 -> + line,  ESP32 GND -> - line
  Press the EN (reset) button on the ESP32 to flash again.
*/
#include <Arduino.h>

const int P_MOSI = 23, P_MISO = 19, P_SCK = 18, P_RST = 5, P_CLK = 4;

void startClock() {                         // 1 MHz square wave on G4 -> ATmega XTAL1 (pin 13)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(P_CLK, 1000000, 1);
  ledcWrite(P_CLK, 1);
#else
  ledcSetup(0, 1000000, 1);
  ledcAttachPin(P_CLK, 0);
  ledcWrite(0, 1);
#endif
}
const uint16_t PAGE_WORDS = 64;            // ATmega32: 64 words = 128 bytes per page

const uint32_t FW_SIZE = 142;
const uint8_t FW[] PROGMEM = {
  0x0C, 0x94, 0x2A, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00,
  0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00,
  0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00,
  0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00,
  0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00, 0x0C, 0x94, 0x34, 0x00,
  0x0C, 0x94, 0x34, 0x00, 0x11, 0x24, 0x1F, 0xBE, 0xCF, 0xE5, 0xD8, 0xE0, 0xDE, 0xBF, 0xCD, 0xBF,
  0x0E, 0x94, 0x36, 0x00, 0x0C, 0x94, 0x45, 0x00, 0x0C, 0x94, 0x00, 0x00, 0xB8, 0x9A, 0x91, 0xE0,
  0x88, 0xB3, 0x89, 0x27, 0x88, 0xBB, 0x2F, 0xE3, 0x3D, 0xE0, 0x83, 0xE0, 0x21, 0x50, 0x30, 0x40,
  0x80, 0x40, 0xE1, 0xF7, 0x00, 0xC0, 0x00, 0x00, 0xF3, 0xCF, 0xF8, 0x94, 0xFF, 0xCF,
};

uint8_t xfer(uint8_t out) {                 // bit-banged SPI, mode 0, ~60 kHz (safe for a 1 MHz chip)
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(P_MOSI, (out >> i) & 1);
    delayMicroseconds(8);
    digitalWrite(P_SCK, HIGH);
    delayMicroseconds(8);
    in = (in << 1) | digitalRead(P_MISO);
    digitalWrite(P_SCK, LOW);
  }
  return in;
}
uint8_t cmd(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { xfer(a); xfer(b); xfer(c); return xfer(d); }

bool enterProgramming() {
  for (int tries = 0; tries < 5; tries++) {
    digitalWrite(P_SCK, LOW);
    digitalWrite(P_RST, HIGH); delay(5);
    digitalWrite(P_RST, LOW);  delay(30);
    xfer(0xAC); xfer(0x53); uint8_t echo = xfer(0x00); xfer(0x00);
    if (echo == 0x53) return true;
    Serial.printf("  try %d: no answer (got 0x%02X)\n", tries + 1, echo);
  }
  return false;
}

void finish(bool ok) {
  digitalWrite(P_RST, HIGH);
  pinMode(P_MOSI, INPUT); pinMode(P_SCK, INPUT); pinMode(P_RST, INPUT);   // let the ATmega run
  Serial.println(ok ? "\n==== DONE: verify 100% OK ====" : "\n==== FAILED ====");
  Serial.println(ok ? "Now follow the next step in the guide." : "Check: 3V3 and GND wires, pins 10/30 to +, 11/31 to -, G23->6, G19->7, G18->8, G5->9, G4->13. Then press EN.");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nESP32 -> ATmega32A flasher: Blink test");
  pinMode(P_MOSI, OUTPUT); pinMode(P_SCK, OUTPUT); pinMode(P_RST, OUTPUT); pinMode(P_MISO, INPUT);
  startClock();
  delay(50);
  Serial.printf("MISO line idle level: %d\n", digitalRead(P_MISO));

  if (!enterProgramming()) { Serial.println("The ATmega32 does not answer."); finish(false); return; }
  uint8_t s0 = cmd(0x30, 0, 0, 0), s1 = cmd(0x30, 0, 1, 0), s2 = cmd(0x30, 0, 2, 0);
  Serial.printf("Signature: %02X %02X %02X %s\n", s0, s1, s2, (s0 == 0x1E && s1 == 0x95 && s2 == 0x02) ? "(ATmega32 OK)" : "(UNEXPECTED)");
  uint8_t lf = cmd(0x50, 0, 0, 0), hf = cmd(0x58, 0x08, 0, 0);
  Serial.printf("Fuses: low=%02X high=%02X  (factory = E1 99, internal 1 MHz)\n", lf, hf);
  if (s0 != 0x1E || s1 != 0x95) { finish(false); return; }

  Serial.println("Erasing...");
  cmd(0xAC, 0x80, 0, 0); delay(20);
  enterProgramming();

  uint32_t words = (FW_SIZE + 1) / 2;
  Serial.printf("Writing %u bytes...\n", (unsigned)FW_SIZE);
  for (uint32_t page = 0; page * PAGE_WORDS < words; page++) {
    for (uint16_t i = 0; i < PAGE_WORDS; i++) {
      uint32_t w = page * PAGE_WORDS + i;
      uint8_t lo = (2 * w < FW_SIZE) ? pgm_read_byte(&FW[2 * w]) : 0xFF;
      uint8_t hi = (2 * w + 1 < FW_SIZE) ? pgm_read_byte(&FW[2 * w + 1]) : 0xFF;
      cmd(0x40, 0, i, lo);
      cmd(0x48, 0, i, hi);
    }
    uint16_t a = page * PAGE_WORDS;
    cmd(0x4C, a >> 8, a & 0xFF, 0);
    delay(10);
    Serial.print('#');
  }
  Serial.println("\nVerifying...");
  uint32_t bad = 0;
  for (uint32_t w = 0; w < words; w++) {
    uint8_t lo = cmd(0x20, w >> 8, w & 0xFF, 0), hi = cmd(0x28, w >> 8, w & 0xFF, 0);
    if (lo != pgm_read_byte(&FW[2 * w])) bad++;
    if (2 * w + 1 < FW_SIZE && hi != pgm_read_byte(&FW[2 * w + 1])) bad++;
  }
  Serial.printf("Verify: %u bytes wrong\n", (unsigned)bad);
  finish(bad == 0);
}

void loop() {}
