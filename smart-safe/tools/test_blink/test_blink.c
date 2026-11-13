/* TEST BLINK - ATmega32A. LED on PB0 (pin 1): ON 1 s, OFF 1 s.
   If it blinks 8x faster or slower, the clock is not 1 MHz: tell Claude. */
#define F_CPU 1000000UL
#include <avr/io.h>
#include <util/delay.h>
int main(void) {
    DDRB |= (1 << PB0);
    for (;;) { PORTB ^= (1 << PB0); _delay_ms(1000); }
}
