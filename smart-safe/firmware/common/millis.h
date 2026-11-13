/* 1 ms system tick with Timer0 (CTC) */
#ifndef MILLIS_H
#define MILLIS_H
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
static volatile uint32_t ms_ticks = 0;
ISR(TIMER0_COMP_vect) { ms_ticks++; }
static void millis_init(void) {
#if F_CPU <= 2000000UL
    TCCR0 = (1 << WGM01) | (1 << CS01);                 /* CTC, prescaler 8  (1 MHz -> OCR0 = 124) */
    OCR0  = (uint8_t)(F_CPU / 8 / 1000 - 1);
#else
    TCCR0 = (1 << WGM01) | (1 << CS01) | (1 << CS00);   /* CTC, prescaler 64 (8 MHz -> OCR0 = 124) */
    OCR0  = (uint8_t)(F_CPU / 64 / 1000 - 1);
#endif
    TIMSK |= (1 << OCIE0);
    sei();
}
static uint32_t millis(void) { uint32_t m; ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { m = ms_ticks; } return m; }
#endif
