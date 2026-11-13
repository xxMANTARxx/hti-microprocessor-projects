/* 16x2 LCD with PCF8574 I2C board, for ATmega32 (TWI: PC0 = SCL, PC1 = SDA) */
#ifndef LCD_I2C_H
#define LCD_I2C_H
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

static uint8_t LCD_ADDR = 0x27;   /* found automatically by lcd_init() */
#define LCD_BL 0x08
#define LCD_EN 0x04
#define LCD_RS 0x01

static void twi_init(void) {
    TWSR = 0;                                   /* prescaler 1 */
    TWBR = (uint8_t)(((F_CPU / 50000UL) - 16) / 2);   /* ~50 kHz */
    if (TWBR < 2) TWBR = 2;
    TWCR = (1 << TWEN);
}
static void twi_start(void) { TWCR = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN); while (!(TWCR & (1<<TWINT))); }
static void twi_stop(void)  { TWCR = (1<<TWINT)|(1<<TWSTO)|(1<<TWEN); }
static void twi_write(uint8_t d) { TWDR = d; TWCR = (1<<TWINT)|(1<<TWEN); while (!(TWCR & (1<<TWINT))); }
static uint8_t twi_probe(uint8_t a) {      /* 1 if a device answers at address a */
    twi_start(); twi_write(a << 1);
    uint8_t ok = ((TWSR & 0xF8) == 0x18);
    twi_stop(); _delay_us(100);
    return ok;
}

static void pcf_write(uint8_t d) { twi_start(); twi_write(LCD_ADDR << 1); twi_write(d | LCD_BL); twi_stop(); }

static void lcd_nibble(uint8_t n, uint8_t rs) {
    uint8_t d = (n & 0xF0) | rs;
    pcf_write(d | LCD_EN); _delay_us(1);
    pcf_write(d);          _delay_us(50);
}
static void lcd_byte(uint8_t b, uint8_t rs) { lcd_nibble(b & 0xF0, rs); lcd_nibble(b << 4, rs); }
static void lcd_cmd(uint8_t c)  { lcd_byte(c, 0); if (c < 4) _delay_ms(2); }
static void lcd_char(char c)    { lcd_byte((uint8_t)c, LCD_RS); }
static void lcd_print(const char *s) { while (*s) lcd_char(*s++); }
static void lcd_goto(uint8_t col, uint8_t row) { lcd_cmd(0x80 | (col + (row ? 0x40 : 0))); }
static void lcd_clear(void) { lcd_cmd(0x01); }

static uint8_t lcd_init(void) {          /* returns 1 if the LCD was found */
    twi_init();
    _delay_ms(50);
    uint8_t found = 0;
    for (uint8_t a = 0x20; a <= 0x3F && !found; a++) {
        if (a == 0x28) a = 0x38;           /* PCF8574: 0x20-0x27, PCF8574A: 0x38-0x3F */
        if (twi_probe(a)) { LCD_ADDR = a; found = 1; }
    }
    lcd_nibble(0x30, 0); _delay_ms(5);
    lcd_nibble(0x30, 0); _delay_us(150);
    lcd_nibble(0x30, 0);
    lcd_nibble(0x20, 0);            /* 4-bit mode */
    lcd_cmd(0x28);                  /* 2 lines, 5x8 */
    lcd_cmd(0x0C);                  /* display on, no cursor */
    lcd_cmd(0x06);                  /* move right */
    lcd_clear();
    return found;
}
/* print two lines (pads with spaces, no flicker) */
static void lcd_show(const char *l1, const char *l2) {
    uint8_t i;
    lcd_goto(0, 0); for (i = 0; i < 16; i++) lcd_char(*l1 ? *l1++ : ' ');
    lcd_goto(0, 1); for (i = 0; i < 16; i++) lcd_char(*l2 ? *l2++ : ' ');
}
#endif
