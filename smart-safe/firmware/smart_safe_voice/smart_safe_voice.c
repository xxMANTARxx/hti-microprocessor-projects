/*
  SMART SAFE  -  ATmega32A (AVR C)
  Opens with the keypad password OR 3 claps. Locks with 2 claps or key C.
  Keys: 0-9 password, # enter, * clear, C lock, A change password (while open).

  Clock: factory default = internal 1 MHz. If you set fuses for 8 MHz, change F_CPU below.

  PINS
    Keypad rows 1-4 -> PA0..PA3      Keypad cols 1-4 -> PA4..PA7
    LCD I2C SCL -> PC0, SDA -> PC1   Servo signal -> PD5 (OC1A)
    Sound sensor DO -> PD2           Buzzer -> PB2
    Green LED -> PB0 (220 ohm)       Red LED -> PB1 (220 ohm)
*/
#ifndef F_CPU
#define F_CPU 1000000UL
#endif
#include <avr/io.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <string.h>
#include "../common/lcd_i2c.h"
#include "../common/millis.h"

/* ---------- settings ---------- */
#define SERVO_LOCKED_US 1000     /* pulse width when locked (us), change after testing */
#define SERVO_OPEN_US   2000     /* pulse width when open */
#define CLAP_DEBOUNCE   120      /* ms */
#define CLAP_WINDOW     1200     /* ms of silence that ends a clap group */
#define MAX_WRONG       3
#define LOCKOUT_MS      30000UL

#define BUZ_ON()   (PORTB |=  (1 << PB2))
#define BUZ_OFF()  (PORTB &= ~(1 << PB2))
#define GREEN(x)   ((x) ? (PORTB |= (1 << PB0)) : (PORTB &= ~(1 << PB0)))
#define RED(x)     ((x) ? (PORTB |= (1 << PB1)) : (PORTB &= ~(1 << PB1)))
#define SOUND_HIGH() (PIND & (1 << PD2))

static const char KEYS[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static char password[9] = "1234";
static char typed[9] = "";
static uint8_t typedLen = 0;
static uint8_t isOpen = 0, changingPass = 0, wrongCount = 0;
static uint32_t lockoutUntil = 0;
static uint8_t EEMEM ee_pass[9];

static void beep(uint16_t ms) { BUZ_ON(); while (ms--) _delay_ms(1); BUZ_OFF(); }
static void wait_ms(uint16_t ms) { while (ms--) _delay_ms(1); }

/* ---------- servo: Timer1 fast PWM, 50 Hz on OC1A (PD5) ---------- */
static void servo_init(void) {
    DDRD |= (1 << PD5);
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
#if F_CPU <= 2000000UL
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);      /* prescaler 1: 1 tick = 1 us at 1 MHz */
    ICR1 = (uint16_t)(F_CPU / 50) - 1;
#else
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);      /* prescaler 8: 1 tick = 1 us at 8 MHz */
    ICR1 = (uint16_t)(F_CPU / 8 / 50) - 1;
#endif
}
static void servo_us(uint16_t us) {
#if F_CPU <= 2000000UL
    OCR1A = (uint16_t)((uint32_t)us * (F_CPU / 1000000UL));
#else
    OCR1A = (uint16_t)((uint32_t)us * (F_CPU / 8 / 1000000UL));
#endif
}

/* ---------- keypad: rows out (PA0-3), cols in with pull-up (PA4-7) ---------- */
static char keypad_scan(void) {
    for (uint8_t r = 0; r < 4; r++) {
        PORTA = (PORTA | 0x0F) & ~(1 << r);   /* one row low */
        _delay_us(30);
        uint8_t cols = (PINA >> 4) & 0x0F;
        for (uint8_t c = 0; c < 4; c++)
            if (!(cols & (1 << c))) return KEYS[r][c];
    }
    return 0;
}
static char keypad_get(void) {          /* returns a key once per press */
    static char last = 0;
    static uint32_t t = 0;
    char k = keypad_scan();
    if (k != last && millis() - t > 40) { t = millis(); last = k; return k; }
    return 0;
}

/* ---------- screens ---------- */
static void show_idle(void) {
    if (isOpen) lcd_show("SAFE: OPEN", "C=lock A=newpass");
    else        lcd_show("SAFE: LOCKED", "Pass or voice ID");
}
static void open_safe(const char *how) {
    servo_us(SERVO_OPEN_US); isOpen = 1; wrongCount = 0;
    GREEN(1); RED(0);
    beep(80); wait_ms(60); beep(80);
    lcd_show("OPENED", how); wait_ms(1200); show_idle();
}
static void close_safe(const char *how) {
    servo_us(SERVO_LOCKED_US); isOpen = 0; changingPass = 0;
    GREEN(0); RED(1);
    beep(250);
    lcd_show("LOCKED", how); wait_ms(1200); show_idle();
}
static void clear_typed(void) { typedLen = 0; typed[0] = 0; }

static void handle_key(char k) {
    if (millis() < lockoutUntil) { lcd_show("LOCKED OUT", "Please wait"); return; }
    beep(20);
    if (k == '*') { clear_typed(); lcd_show(changingPass ? "New password:" : "Enter password:", ""); return; }
    if (k == 'C') { if (isOpen) close_safe("by key C"); clear_typed(); return; }
    if (k == 'A') {
        if (isOpen) { changingPass = 1; clear_typed(); lcd_show("New password:", "then press #"); }
        else lcd_show("Open first", "to change pass");
        return;
    }
    if (k == '#') {
        if (changingPass) {
            if (typedLen >= 4) {
                strcpy(password, typed);
                eeprom_update_block(password, ee_pass, sizeof(password));
                changingPass = 0;
                lcd_show("Password saved", ""); beep(80); wait_ms(60); beep(80);
                wait_ms(1200); show_idle();
            } else lcd_show("Min 4 digits", "Try again");
        } else if (isOpen) {
            show_idle();
        } else if (strcmp(typed, password) == 0) {
            open_safe("by password");
        } else {
            wrongCount++;
            char l2[17] = "x tries left";
            l2[0] = '0' + (MAX_WRONG - wrongCount);
            lcd_show("Wrong password", l2); beep(400);
            if (wrongCount >= MAX_WRONG) {
                wrongCount = 0;
                lcd_show("WRONG x3 !", "Wait 30 sec");
                for (uint8_t i = 0; i < 6; i++) { beep(150); wait_ms(100); }
                lockoutUntil = millis() + LOCKOUT_MS;
            }
            wait_ms(1000); show_idle();
        }
        clear_typed();
        return;
    }
    if (k >= '0' && k <= '9' && typedLen < 8) {
        typed[typedLen++] = k; typed[typedLen] = 0;
        char l2[17];
        for (uint8_t i = 0; i < typedLen; i++) l2[i] = changingPass ? typed[i] : '*';
        l2[typedLen] = 0;
        lcd_show(changingPass ? "New password:" : "Enter password:", l2);
    }
}

/* ---------- claps ---------- */
static void handle_sound(void) {
    static uint8_t count = 0, lastState = 0;
    static uint32_t lastClap = 0;
    uint32_t now = millis();
    uint8_t s = SOUND_HIGH() ? 1 : 0;
    if (s && !lastState && now - lastClap > CLAP_DEBOUNCE) {
        count++; lastClap = now;
        lcd_goto(15, 0); lcd_char('0' + (count > 9 ? 9 : count));
    }
    lastState = s;
    if (count && now - lastClap > CLAP_WINDOW) {
        if (now >= lockoutUntil) {
            if (count == 3 && !isOpen)      open_safe("by voice ID");
            else if (count == 2 && isOpen)  close_safe("by voice lock");
            else { lcd_goto(15, 0); lcd_char(' '); }
        }
        count = 0;
    }
}

int main(void) {
    /* free PC2-PC5 from JTAG (must write twice within 4 cycles) */
    MCUCSR |= (1 << JTD); MCUCSR |= (1 << JTD);

    DDRA = 0x0F; PORTA = 0xFF;                  /* rows out high, cols pull-up */
    DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2);
    DDRD &= ~(1 << PD2); PORTD |= (1 << PD2);    /* voice input with internal pull-up (ESP32 pulls it LOW) */

    millis_init();
    if (!lcd_init()) {                          /* LCD not found: green LED blinks fast 10 times */
        for (uint8_t i = 0; i < 10; i++) { PORTB |= (1 << PB0); _delay_ms(100); PORTB &= ~(1 << PB0); _delay_ms(100); }
    } else {                                    /* LCD found: one long green flash */
        PORTB |= (1 << PB0); _delay_ms(1000); PORTB &= ~(1 << PB0);
    }
    servo_init();
    servo_us(SERVO_LOCKED_US);
    RED(1);

    eeprom_read_block(password, ee_pass, sizeof(password));
    if (password[0] < '0' || password[0] > '9') strcpy(password, "1234");   /* empty EEPROM */
    password[8] = 0;

    lcd_show("Smart Safe", "ATmega32");
    wait_ms(1000);
    show_idle();

    for (;;) {
        char k = keypad_get();
        if (k) handle_key(k);
        handle_sound();
    }
}
