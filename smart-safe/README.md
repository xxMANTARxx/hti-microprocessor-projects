# 🔐 Smart Safe – ATmega32A

An electronic safe built around the **ATmega32A** and written in **bare-metal AVR C** (no Arduino libraries).

- Open with the **keypad password + `#`** or with **3 claps**
- Lock with **key `C`** or **2 claps**
- **30-second lock-out** after 3 wrong passwords
- Change the password with **`A`** (4–8 digits), saved in **EEPROM**
- 16×2 **I2C LCD** (PCF8574, address found automatically), **SG90 servo** lock, buzzer, green/red LEDs
- Programmed with an **ESP32 acting as an AVR in-system programmer** (no USBasp needed)

![Smart Safe](images/p_safe.jpg)

## Hardware

| Signal | ATmega32A pin |
|---|---|
| Keypad rows 1–4 | PA0–PA3 (pins 40–37) |
| Keypad columns 1–4 | PA4–PA7 (pins 36–33) |
| LCD SCL / SDA | PC0 (22) / PC1 (23) |
| Servo signal (OC1A) | PD5 (19) |
| Sound sensor DO | PD2 (16) |
| Green LED / Red LED / Buzzer | PB0 (1) / PB1 (2) / PB2 (3) |
| VCC, AVCC / GND | 10, 30 / 11, 31 |
| RESET | 9 (10 kΩ to +5 V) |

Clock: internal RC **1 MHz** (factory fuses `E1 / 99`). Supply: 5 V from the ESP32 `VIN` pin.
Also needed: 2 × 220 Ω (LEDs), 100 nF + 470 µF capacitors.

![Wiring](images/s_wiring_full.png)
![Block diagram](images/s_block.png)

## Firmware

| File | What it does |
|---|---|
| [`firmware/smart_safe/smart_safe.c`](firmware/smart_safe/smart_safe.c) | Main program: keypad, password, lock-out, EEPROM, claps, servo |
| [`firmware/common/lcd_i2c.h`](firmware/common/lcd_i2c.h) | TWI driver + HD44780 LCD in 4-bit mode over PCF8574 |
| [`firmware/common/millis.h`](firmware/common/millis.h) | Timer0 CTC → 1 ms tick |
| [`firmware/smart_safe/smart_safe.hex`](firmware/smart_safe/smart_safe.hex) | Ready-to-flash program (2,620 bytes) |

Build it yourself:

```bash
cd firmware/smart_safe
avr-gcc -mmcu=atmega32 -Os -DF_CPU=1000000UL smart_safe.c -o smart_safe.elf
avr-objcopy -O ihex -R .eeprom smart_safe.elf smart_safe.hex
```

Memory: flash 2,620 B (8 %), SRAM 376 B (18 %), EEPROM 9 B.

![Main flow](images/s_main_flow.png)

## Programming the ATmega32A with an ESP32

[`programmer/flash_safe/flash_safe.ino`](programmer/flash_safe/flash_safe.ino) already contains the firmware as a byte array.
Upload it to an ESP32 ("ESP32 Dev Module"), open the Serial Monitor at **115200** and wait for `DONE: verify 100% OK`.

| ESP32 | ATmega32A |
|---|---|
| G23 | pin 6 (MOSI) |
| G19 → 1 kΩ | pin 7 (MISO) |
| G18 | pin 8 (SCK) |
| G5 | pin 9 (RESET) |
| G4 | pin 13 (XTAL1, 1 MHz clock) |
| VIN / GND | + / – rails |

After flashing, remove the 5 programming wires; the ATmega32A runs alone.
The [`tools/`](tools/) folder has helper sketches (`check_wiring`, `find_pins`) and a blink test used while debugging the programmer.

## How to use

| Key / action | Effect |
|---|---|
| `0–9` then `#` | Enter password (default **1234**) |
| `*` | Clear |
| `C` / 2 claps | Lock |
| 3 claps | Open |
| `A` (while open) | New password, then `#` |

📄 Full details: [Technical report (PDF)](docs/Smart_Safe_Technical_Report.pdf) · [Arabic build guide](docs/build_guide_ar.html) (download and open in a browser)

---

<div dir="rtl">

## بالعربي

خزنة إلكترونية متحكم فيها **ATmega32A** ومكتوبة بلغة **C**.

- **بتتفتح:** بالباسورد وبعده `#`، أو بـ **3 تصقيفات**.
- **بتتقفل:** بزرار `C`، أو بـ **تصقيفتين**.
- لو الباسورد اتكتب غلط 3 مرات، الخزنة **بتقفل نفسها 30 ثانية**.
- **تغيير الباسورد:** وهي مفتوحة دوس `A`، اكتب الباسورد الجديد، ودوس `#`. الباسورد بيتحفظ في الـ EEPROM حتى لو الكهربا فصلت.
- **الباسورد الافتراضي:** 1234

**الحرق على الشريحة:** ارفع ملف `programmer/flash_safe/flash_safe.ino` على الـ ESP32، ووصّل الخمس سلوك اللي في الجدول فوق، وافتح الـ Serial Monitor. لما تظهر كلمة `DONE` شيل السلوك.

</div>
